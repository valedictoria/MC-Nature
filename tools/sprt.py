#!/usr/bin/env python3
"""Minimal SPRT harness for two UCI engines.

Runs a self-play match between engine A ("dev") and engine B ("base"), applies
the Sequential Probability Ratio Test with H0: elo=elo0 vs H1: elo=elo1, and
stops as soon as the log-likelihood ratio crosses ±log((1-beta)/alpha) — the
standard Wald boundaries. Alternates colors, seeds each game from a random
opening line (Chess960-style random starts optional; here we use a small
built-in book of common openings).

Usage:
  python3 tools/sprt.py \\
      --dev  ../engine-src/build/carelesschess \\
      --base tools/baselines/cc-capthist-step6 \\
      --tc   8+0.08 \\
      --elo0 0  --elo1 10 \\
      --alpha 0.05 --beta 0.05

No third-party deps beyond python-chess (already in lichess-bot's env).
Not a replacement for fastchess/cutechess — no PGN output, no draw adjudication
beyond the engines' own claims, no crash recovery. But: works locally with zero
setup, which is the point.
"""
from __future__ import annotations

import argparse
import asyncio
import math
import random
import sys
import time
from dataclasses import dataclass, field

import chess
import chess.engine


# A small library of decent balanced openings — feeds the engines equivalent
# positions so noise from asymmetric starts doesn't dominate. Real SPRT setups
# use a much larger book (e.g., 8moves_v3.epd); this is enough for a quick
# smoke test at short TCs.
OPENING_BOOK = [
    ["e2e4", "e7e5", "g1f3", "b8c6", "f1b5"],
    ["e2e4", "c7c5", "g1f3", "d7d6", "d2d4"],
    ["d2d4", "g8f6", "c2c4", "e7e6", "g1f3"],
    ["d2d4", "d7d5", "c2c4", "e7e6", "b1c3"],
    ["c2c4", "e7e5", "b1c3", "g8f6", "g1f3"],
    ["e2e4", "e7e6", "d2d4", "d7d5", "b1c3"],
    ["e2e4", "c7c6", "d2d4", "d7d5", "b1c3"],
    ["g1f3", "d7d5", "d2d4", "g8f6", "c2c4"],
    ["e2e4", "g7g6", "d2d4", "f8g7", "b1c3"],
    ["d2d4", "f7f5", "g2g3", "g8f6", "f1g2"],
]


@dataclass
class Score:
    """Running match score. `wins`/`losses`/`draws` are from dev's perspective."""
    wins: int = 0
    losses: int = 0
    draws: int = 0

    def games(self) -> int:
        return self.wins + self.losses + self.draws

    def add(self, dev_result: float) -> None:
        # dev_result: 1.0 = dev win, 0.5 = draw, 0.0 = dev loss
        if dev_result == 1.0:
            self.wins += 1
        elif dev_result == 0.0:
            self.losses += 1
        else:
            self.draws += 1


def parse_tc(tc: str) -> tuple[float, float]:
    """Parse "10+0.1" → (base_seconds, increment_seconds)."""
    if "+" in tc:
        base, inc = tc.split("+")
        return float(base), float(inc)
    return float(tc), 0.0


def llr(w: int, l: int, d: int, elo0: float, elo1: float) -> float:
    """SPRT log-likelihood ratio using the BayesElo model.

    The trinomial-with-draws version has thicker math; the pentanomial version
    is what fastchess uses. This is the classic Elostat-style trinomial LLR —
    good enough for coarse gates at short TCs. If you want proper pentanomial,
    swap this for the Stockfish-testing formula and count paired games.
    """
    n = w + l + d
    if n == 0:
        return 0.0
    if w == 0 or l == 0:
        # Degenerate — return a tiny nudge so the caller can still make progress
        # instead of NaN.
        return 0.0 if n < 10 else (5.0 if l == 0 else -5.0)

    # Empirical win/loss/draw rates.
    W, L, D = w / n, l / n, d / n

    def elo_to_score(elo: float) -> float:
        # Bayes-Elo score expectation for one game (draws counted as 0.5).
        return 1.0 / (1.0 + 10 ** (-elo / 400.0))

    s0 = elo_to_score(elo0)
    s1 = elo_to_score(elo1)

    # Solve for (p_w, p_l, p_d) matching each score under fixed draw rate D.
    # p_w + p_l = 1 - D, p_w - p_l = 2s - 1 - 0*D + D*(0.5 - 0.5) = 2s - 1
    # Actually: score = p_w + 0.5*D; so p_w = s - 0.5*D and p_l = 1 - D - p_w.
    def probs(s: float) -> tuple[float, float, float]:
        pw = s - 0.5 * D
        pl = 1.0 - D - pw
        # Guard against negatives if the fixed D is inconsistent with s at the
        # extremes — clip and rely on n growing to smooth this out.
        pw = max(1e-6, min(1 - 1e-6, pw))
        pl = max(1e-6, min(1 - 1e-6, pl))
        return pw, pl, max(1e-6, D)

    pw0, pl0, pd0 = probs(s0)
    pw1, pl1, pd1 = probs(s1)

    # LLR = w*log(pw1/pw0) + l*log(pl1/pl0) + d*log(pd1/pd0)
    return (w * math.log(pw1 / pw0)
          + l * math.log(pl1 / pl0)
          + d * math.log(pd1 / pd0))


async def play_one(dev_path: str, base_path: str, dev_is_white: bool,
                   opening: list[str], base_time: float, inc: float,
                   move_limit: int = 400) -> float:
    """Play one game. Returns dev's score (1/0.5/0)."""
    board = chess.Board()
    for uci in opening:
        board.push_uci(uci)

    # popen_uci returns (transport, engine); we only need the engine handle
    # here — python-chess owns the transport for us.
    _, dev  = await chess.engine.popen_uci(dev_path)
    _, base = await chess.engine.popen_uci(base_path)
    white = dev if dev_is_white else base
    black = base if dev_is_white else dev

    white_clock = black_clock = base_time
    result_dev: float = 0.5

    try:
        for _ in range(move_limit):
            if board.is_game_over(claim_draw=True):
                break
            engine = white if board.turn == chess.WHITE else black
            my_clock  = white_clock if board.turn == chess.WHITE else black_clock
            op_clock  = black_clock if board.turn == chess.WHITE else white_clock

            limit = chess.engine.Limit(
                white_clock=white_clock, black_clock=black_clock,
                white_inc=inc, black_inc=inc,
            )
            t0 = time.monotonic()
            try:
                result = await asyncio.wait_for(engine.play(board, limit),
                                                 timeout=my_clock + 5.0)
            except (asyncio.TimeoutError, chess.engine.EngineError,
                    chess.engine.EngineTerminatedError):
                # Engine hung / crashed → loss for that side.
                result_dev = 0.0 if engine is dev else 1.0
                return result_dev
            elapsed = time.monotonic() - t0

            # Clock update — very approximate; python-chess doesn't return
            # actual thinking time so we use wall-clock. A real harness would
            # timestamp exactly around the play() call.
            if board.turn == chess.WHITE:
                white_clock = my_clock - elapsed + inc
                if white_clock <= 0:
                    return 0.0 if engine is dev else 1.0
            else:
                black_clock = my_clock - elapsed + inc
                if black_clock <= 0:
                    return 0.0 if engine is dev else 1.0
            _ = op_clock  # unused, kept for clarity above

            if result.move is None:
                # Engine resigned or returned no move.
                result_dev = 0.0 if engine is dev else 1.0
                return result_dev
            board.push(result.move)

        outcome = board.outcome(claim_draw=True)
        if outcome is None or outcome.winner is None:
            result_dev = 0.5
        elif outcome.winner == chess.WHITE:
            result_dev = 1.0 if dev_is_white else 0.0
        else:
            result_dev = 0.0 if dev_is_white else 1.0
    finally:
        await dev.quit()
        await base.quit()
    return result_dev


async def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dev",  required=True, help="path to engine A (dev)")
    ap.add_argument("--base", required=True, help="path to engine B (base)")
    ap.add_argument("--tc",   default="10+0.1", help="time control, base+inc")
    ap.add_argument("--elo0", type=float, default=0.0, help="H0 elo bound")
    ap.add_argument("--elo1", type=float, default=8.0, help="H1 elo bound")
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--beta",  type=float, default=0.05)
    ap.add_argument("--max-games", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=None)
    args = ap.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    base_time, inc = parse_tc(args.tc)

    # SPRT stopping boundaries.
    lower = math.log(args.beta / (1.0 - args.alpha))
    upper = math.log((1.0 - args.beta) / args.alpha)

    score = Score()
    dev_is_white = True
    game_idx = 0

    print(f"SPRT: elo0={args.elo0} elo1={args.elo1} "
          f"alpha={args.alpha} beta={args.beta} "
          f"tc={args.tc} max_games={args.max_games}")
    print(f"  bounds: [{lower:+.3f}, {upper:+.3f}]")

    while game_idx < args.max_games:
        opening = random.choice(OPENING_BOOK)
        r = await play_one(args.dev, args.base, dev_is_white, opening,
                           base_time, inc)
        score.add(r)
        game_idx += 1
        dev_is_white = not dev_is_white

        current_llr = llr(score.wins, score.losses, score.draws,
                          args.elo0, args.elo1)
        # elo point estimate
        n = score.games()
        if n > 0 and score.wins > 0 and score.losses > 0:
            wr = (score.wins + 0.5 * score.draws) / n
            wr = min(max(wr, 1e-6), 1 - 1e-6)
            elo_est = -400 * math.log10(1.0 / wr - 1.0)
        else:
            elo_est = float("nan")

        print(f"[{game_idx:4d}]  W-L-D {score.wins}-{score.losses}-{score.draws}"
              f"  elo≈{elo_est:+.1f}  llr={current_llr:+.3f}",
              flush=True)

        if current_llr >= upper:
            print(f"H1 accepted (elo > {args.elo0}) after {game_idx} games")
            return 0
        if current_llr <= lower:
            print(f"H0 accepted (elo < {args.elo1}) after {game_idx} games")
            return 1

    print(f"Inconclusive after {args.max_games} games "
          f"(llr={current_llr:+.3f}, elo≈{elo_est:+.1f})")
    return 2


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
