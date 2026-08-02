#!/usr/bin/env python3
"""SPSA tuner for CarelessChess search parameters.

fastchess has no built-in SPSA, so this drives it: every iteration it perturbs
ALL tunable params at once (Bernoulli +-1), plays a small node-limited batch of
plus-vs-minus games, and takes one simultaneous-perturbation gradient step.

Node-limited games (not wall-clock) are used on purpose: deterministic per-move
effort, no time-jitter noise, and reproducible -- ideal for tuning search params.
The result is only ever *kept* after a separate SPRT verification, so a mediocre
tune costs time, not strength.

Live progress: appends to tools/spsa.log and rewrites tools/spsa_state.json
(resumable) + tools/spsa_result.json (current best-estimate theta, ready to paste
as engine defaults). Ctrl-C stops cleanly and writes the result.

Usage:
  tools/spsa.py --iters 500            # real run
  tools/spsa.py --smoke                # 2 tiny iters to validate the pipeline
  tools/spsa.py --iters 500 --resume   # continue from tools/spsa_state.json
"""
import argparse, json, os, random, re, subprocess, sys, signal

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE_DIR = os.path.dirname(HERE)
BIN = os.path.join(ENGINE_DIR, "build", "carelesschess")
FASTCHESS = os.path.join(HERE, "fastchess", "fastchess")
BOOK = os.path.join(HERE, "books", "8moves_v3.pgn")
LOG = os.path.join(HERE, "spsa.log")
STATE = os.path.join(HERE, "spsa_state.json")
RESULT = os.path.join(HERE, "spsa_result.json")

# name, default, lo, hi  -- must mirror TUNE_PARAMS in src/search.cpp.
PARAMS = [
    ("RfpMargin",      80,   20,   200),
    ("RfpMarginImp",   60,   20,   200),
    ("NullBase",        3,    1,     6),
    ("NullDepthDiv",    6,    3,    12),
    ("NullEvalDiv",   200,   80,   400),
    ("ProbCutMargin", 250,  120,   400),
    ("ProbCutImp",     60,    0,   140),
    ("FutBase",       150,   50,   350),
    ("FutScale",      100,   40,   220),
    ("HistPruneMul", 4000, 1500,  8000),
    ("SeeCapMul",      50,   15,   130),
    ("SeeQuietMul",    60,   15,   130),
    ("LmrHistDiv",   8192, 3072, 16384),
    ("LmrDivX100",    210,  160,   280),
    ("AspDelta",       12,    6,    30),
    ("HistBonusMul",    4,    2,     9),
    ("HistBonusMax", 8000, 4000, 14000),
    ("SingularMul",     2,    1,     5),
    ("LmpBase",         3,    1,     7),
    ("LmpImpBase",      4,    1,     9),
]

# SPSA knobs.
NODES = 50000        # per-move node limit for each engine (fast, deterministic)
GAMES_PER_ITER = 8   # even; played as rounds = GAMES_PER_ITER/2 with -repeat
CONCURRENCY = 8      # node-limited games are contention-immune -> use all cores
LR0 = 0.5            # base learning-rate factor (fraction of a perturbation step)

_stop = False
def _sigint(*_):
    global _stop
    _stop = True
    print("\n[spsa] stop requested — finishing current iteration...", flush=True)
signal.signal(signal.SIGINT, _sigint)


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def c_of(lo, hi):
    """Fixed perturbation size for a param: ~10% of its range, at least 1."""
    return max(1, round(0.10 * (hi - lo)))


def play_batch(theta_plus, theta_minus, games):
    """Return y = (Wplus - Lplus) / games over a node-limited plus-vs-minus batch."""
    def opts(theta):
        out = []
        for (name, _d, lo, hi), t in zip(PARAMS, theta):
            out += [f"option.{name}={int(round(clamp(t, lo, hi)))}"]
        return out
    rounds = max(1, games // 2)
    cmd = [
        FASTCHESS,
        "-engine", f"cmd={BIN}", "name=plus",  f"nodes={NODES}", *opts(theta_plus),
        "-engine", f"cmd={BIN}", "name=minus", f"nodes={NODES}", *opts(theta_minus),
        "-each", "proto=uci", "option.Hash=32", "option.Threads=1",
        "-rounds", str(rounds), "-games", "2", "-repeat",
        "-concurrency", str(CONCURRENCY),
        "-openings", f"file={BOOK}", "format=pgn", "order=random", "plies=16",
        "-resign", "movecount=4", "score=700", "twosided=true",
        "-draw", "movenumber=40", "movecount=10", "score=8",
    ]
    p = subprocess.run(cmd, capture_output=True, text=True)
    out = p.stdout + p.stderr
    # fastchess prints, from the FIRST engine's (plus) perspective:
    #   "Games: N, Wins: W, Losses: L, Draws: D, Points: ..."
    m = None
    for line in out.splitlines():
        mm = re.search(r"Games:\s*\d+,\s*Wins:\s*(\d+),\s*Losses:\s*(\d+),\s*Draws:\s*(\d+)", line)
        if mm:
            m = mm  # keep the last (final tally)
    if not m:
        sys.stderr.write("[spsa] could not parse fastchess score:\n" + out[-800:] + "\n")
        return 0.0, (0, 0, 0)
    w, l, d = int(m.group(1)), int(m.group(2)), int(m.group(3))
    n = max(1, w + l + d)
    return (w - l) / n, (w, l, d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=500)
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--resume", action="store_true")
    args = ap.parse_args()

    for path, what in [(BIN, "engine binary"), (FASTCHESS, "fastchess"), (BOOK, "book")]:
        if not os.path.exists(path):
            sys.exit(f"[spsa] missing {what}: {path}")

    iters = 2 if args.smoke else args.iters
    games = 2 if args.smoke else GAMES_PER_ITER
    A = max(1, iters // 2)

    theta = [float(d) for (_n, d, _lo, _hi) in PARAMS]
    start = 0
    if args.resume and os.path.exists(STATE):
        st = json.load(open(STATE))
        theta = [float(st["theta"].get(n, d)) for (n, d, _lo, _hi) in PARAMS]
        start = st.get("iter", 0)
        print(f"[spsa] resumed at iter {start}", flush=True)

    print(f"[spsa] {len(PARAMS)} params, {iters} iters, {games} games/iter, "
          f"nodes={NODES}, conc={CONCURRENCY}", flush=True)

    for k in range(start, iters):
        if _stop:
            break
        lr = LR0 * A / (A + k)
        delta = [random.choice((-1, 1)) for _ in PARAMS]
        tp, tm = [], []
        for (name, _d, lo, hi), t, dl in zip(PARAMS, theta, delta):
            c = c_of(lo, hi)
            tp.append(clamp(t + c * dl, lo, hi))
            tm.append(clamp(t - c * dl, lo, hi))
        y, (w, l, d) = play_batch(tp, tm, games)
        # Gradient ascent on score: step each param toward the winning perturbation.
        for i, ((name, _d, lo, hi), dl) in enumerate(zip(PARAMS, delta)):
            c = c_of(lo, hi)
            theta[i] = clamp(theta[i] + lr * y * dl * c, lo, hi)

        rounded = {n: int(round(clamp(t, lo, hi)))
                   for (n, _d, lo, hi), t in zip(PARAMS, theta)}
        json.dump({"iter": k + 1, "theta": {n: t for (n, _d, _lo, _hi), t
                                            in zip(PARAMS, theta)}},
                  open(STATE, "w"), indent=2)
        json.dump(rounded, open(RESULT, "w"), indent=2)
        line = (f"iter {k+1:4d}/{iters}  y={y:+.3f} (W{w} L{l} D{d})  lr={lr:.3f}  "
                + " ".join(f"{n}={rounded[n]}" for n, *_ in PARAMS))
        with open(LOG, "a") as f:
            f.write(line + "\n")
        print(line, flush=True)

    print(f"[spsa] done. result -> {RESULT}", flush=True)


if __name__ == "__main__":
    main()
