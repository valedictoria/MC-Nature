# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

CarelessChess is a from-scratch, **single-threaded** UCI chess engine (C++20).
Evaluation is a **bullet-trained NNUE** (embedded by default) with a
hand-crafted eval (HCE) fallback; it also does in-engine **Syzygy** tablebase
probing. This repo is the engine only — a separate `lichess-bot` harness (not in
this repo) runs the compiled binary as an external UCI process.

## Commands

```sh
make               # release build → build/carelesschess (-O3 -flto -mcpu=apple-m1)
make perft         # build + run the perft suite (the movegen correctness gate)
make install       # copy binary → ../lichess-bot/engines/carelesschess
                   # (on macOS also strips com.apple.provenance xattrs + re-signs,
                   #  without which lichess-bot's subprocess spawn is SIGKILL'd)
make debug         # -O0 -g build
make clean         # REQUIRED after editing any header — the Makefile has no
                   # header dependency tracking, so a stale LTO link can segfault

./build/carelesschess          # UCI mode
./build/carelesschess perft    # non-interactive perft suite; exit 0 = all pass
./build/carelesschess bench [d] # reproducible fixed-depth node "signature" (default 13)
```

Dev/UCI tokens (pipe or type): `position startpos|fen ... [moves ...]`,
`go depth|movetime|nodes|infinite|wtime ... btime ...`, `go perft <d>` /
`perft <d>` / `divide <d>`, `bench [d]`, `eval` (print static eval + HCE/NNUE),
`d`/`print` (dump FEN), `stop`, `quit`.

## Two non-negotiable correctness gates

1. **`make perft` must pass before any commit touching `src/`.** A broken movegen
   silently invalidates every search result. Cross-check failures with
   python-chess `divide` (`pip3 install --break-system-packages chess`).
2. **`bench` node signature.** The total is deterministic: a no-op refactor must
   reproduce it exactly; a real search/eval change moves it. `perft` guards
   movegen; `bench` guards everything else. Current signatures are logged in
   `tools/BENCH.txt` (e.g. HCE base 2592283; NNUE-active default ≈ 1995761 —
   the embedded net loads at startup, so the default bench reflects NNUE).

## Development discipline (from the campaign history)

**One feature per commit, SPRT-gated. Never batch unmeasured changes** — a prior
unmeasured batch regressed and had to be fully reverted. Save the current binary
to `tools/baselines/` before a change, then A/B it with fastchess (see below)
and keep the commit only if H1 is accepted (or the result is clearly positive at
the time cap). Search/eval changes use `tc=8+0.08 elo0=0 elo1=10` with two-sided
adjudication. Do **not** rebuild `build/carelesschess` while an SPRT is running —
fastchess re-spawns the binary per game and would switch mid-match.

## Architecture

Bootstrap order is fixed in `main.cpp:engine_init()`:
**`BB::init()` → `Magic::init()` → `Zobrist::init()` → `Search::init()` →
`NNUE::init()`**. Magic generation reads the `SquareDistance` table filled by
`BB::init()`, so bitboards must init before magics.

### Core (`src/`)
- **`types.h`** — `Square`/`Piece`/`Color`, packed 16-bit `Move` (to/from/promo/
  type). `Value` is a plain `int` alias (deliberate — do not make it an enum).
  LERF square layout (a1=0). `make_move` uses explicit `int()` casts because
  `bitboard.h` overloads `operator|(Bitboard, Square)`.
- **`bitboard`, `magic`, `attacks.h`, `zobrist`** — board geometry, fancy magic
  bitboards (searched at startup via PRNG; no PEXT on Apple Silicon), attack
  dispatch, and incremental Zobrist keys (piece/ep/castling/side).
- **`position`** — bitboard-per-{color,type} board; `do_move`/`undo_move` with
  incremental Zobrist **and an incremental pawn-only key (`StateInfo::pawnKey`)**
  used by correction history; `StateInfo` linked-list on the search stack embeds
  checkers/pins/check-squares refreshed by `set_check_info`. Standard chess only.
- **`movegen`** — templated per-color generation with a check-evasion target
  mask; every pseudo-legal move filtered through `pos.legal()` (pins, king
  safety, EP-discovered-check). `generate_legal` / `generate_tactical`.
- **`perft`** — perft + `perft_suite()` (6 positions incl. Kiwipete). The gate.
- **`see`** — `see_ge(pos, m, threshold)` static exchange evaluation.
- **`tt`** — 3-entry-cluster transposition table, depth+age replacement,
  mul-shift indexing. Global `TT`.
- **`timeman`** — soft/hard time split, bullet-aware divisor.

### Search (`search.{h,cpp}`) — single-threaded ID + fail-soft PVS
Aspiration windows, mate-distance pruning; ordering = TT move → captures
(SEE+MVV-LVA+capture history) → killers → countermove → combined quiet history
(butterfly + 1/2-ply continuation). Pruning/reductions (non-PV, not in check):
RFP, null-move, LMP, futility, history/SEE pruning, IIR, LMR from a precomputed
table adjusted by PV/cutNode/improving/history. **Singular extensions**
(reduced-depth verification search excluding the TT move; `excludedMove` on the
ply stack; double-ext + multi-cut). **Correction history** (`pawnCorrHist`,
keyed by `pos.pawn_key()`) de-biases the static eval used for pruning and the
qsearch stand-pat; raw eval still cached in the TT. History tables persist
across moves within a game — reset only on `ucinewgame` via `Search::clear()`.
Syzygy WDL/root probing is wired here (see below).

### Evaluation — NNUE (default) with HCE fallback
`eval.cpp evaluate()` dispatches: **`if (NNUE::active()) return NNUE::evaluate(pos)`
else the HCE**. Both return a side-to-move-POV score.
- **`nnue/`** — port of akimbo's bullet-trained net (MIT; see `nnue/NOTICE` and
  `nnue/LICENSE-akimbo`): `(768→1024)x2→1`, 4 king-input buckets + horizontal
  mirroring (king file e–h → `sq^7`) + vertical flip for black (`sq^56`),
  SCReLU, QA=255 QB=64 SCALE=400, plus akimbo material output scaling. The
  default net `nnue/net.bin` is **embedded in the binary via `incbin.s`**
  (self-contained; the file must be present at assemble time). Inference uses
  an **incremental accumulator** (`nnue.cpp`'s `Accumulator` stack, pushed/
  popped alongside `do_move`/`undo_move` via `begin_move`/`end_move`/
  `push_null`/`pop`), refreshed from scratch only on a king-bucket change
  (`king_key` mismatch) or explicitly via `refresh()`; the from-scratch path
  in `evaluate()` remains as the non-search oracle (used by the `eval`
  command and when the accumulator isn't active) and both paths match
  bit-for-bit. A possible follow-up is a small king-bucket cache to avoid
  repeat full rebuilds when the king shuffles back and forth across a bucket
  boundary. UCI `EvalFile` loads an external net of identical architecture,
  overriding the embedded one; if a net fails to load, HCE is used.
- **`eval.cpp`** (HCE fallback) — tapered PeSTO material/PST plus mobility,
  pawn structure, rook files, bishop pair, king safety, threats, tempo.

### Syzygy tablebases (`syzygy/`)
Vendored **Fathom** (`tbprobe.c/.h`, `tbchess.c`, `tbconfig.h`, `stdendian.h`;
MIT, `syzygy/LICENSE-Fathom`) compiled as C11 with `-DTB_NO_THREADS`. Glue
`syzygy/syzygy.{h,cpp}` converts the `Position` to Fathom bitboards and exposes
`probe_wdl` (internal-node cutoff; gated on `rule50==0`, no castling rights, and
`popcount(pieces) <= MaxPieces`) and `probe_root` (root DTZ; plays the optimal
move directly, matched back to an engine `Move` via `generate_legal`). The UCI
`SyzygyPath` option calls `tb_init`. **Probing is off unless a path is set**, so
the default bench is unaffected. Tablebase data files are installed separately
(not in the repo): rsync/curl 3-4-5-piece `.rtbw/.rtbz` into a folder pointed to
by `SyzygyPath` (gitignored). On macOS the system rsync is openrsync — no
`--info=progress2`; use plain `rsync -a`.

### UCI (`uci.{h,cpp}`)
UCI loop; async search in a `std::thread` joined on `stop`/`quit`/new `position`.
`setoption` handles **Hash**, **Move Overhead**, **SyzygyPath**, **EvalFile**.
Threads/Ponder are accepted-and-ignored; unknown options silently ignored.

## Testing & strength (`tools/`)
- **A/B SPRT during development**: `tools/sprt.sh` (overridable `PREFIX`/`DEV_BIN`,
  defaults `tc=8+0.08 elo0=0 elo1=10` + two-sided adjudication) drives
  `tools/fastchess/fastchess` from `tools/books/8moves_v3.pgn`. Concurrency ≤7 on
  an 8-core machine (leave 1 core free) to avoid time-forfeits corrupting results.
- **Absolute anchor**: `tools/ref/` holds a downloaded Stockfish run with
  `UCI_LimitStrength`/`UCI_Elo`; score → Elo via the standard formula.
- `tools/{baselines,ref,fastchess,books}`, `build/`, `*.o`, and the Syzygy data
  folder are gitignored.

## Engine style
No exceptions, no RTTI (enforced by flags), C++20. Hot paths (`do_move`,
`legal`, movegen, attack lookups) avoid heap allocation; `StateInfo` lives on
the search stack. Single-threaded by design; Lazy SMP, MultiPV, and chess960 are
explicit non-goals.

## In-flight WIP (2026-07-31) — SPSA search-param tuning (UNVERIFIED)

**Status: paused mid-verification. The tuned defaults are baked in but NOT yet
proven; do not treat the current tree as a confirmed improvement.**

- **Tunable-param harness (`src/search.cpp`, `src/uci.cpp`):** ~20 pruning/
  reduction constants are now runtime-tunable via an X-macro table `TUNE_PARAMS`
  in `search.cpp` (`namespace Tune`), exposed as UCI spin options
  (`Search::print_tune_options`) and settable with `setoption` (routed through
  `Search::set_tune`; unknown names ignored). `LmrDivX100` = LMR log-divisor ×100
  and recomputes the `Reductions` table on change (`compute_reductions`). With
  the harness's *original* defaults the bench was bit-identical (`1842508`),
  proving the harness itself is a no-op.
- **SPSA driver `tools/spsa.py`** (fastchess has no built-in SPSA): simultaneous
  ±1 perturbation of all params, node-limited games (deterministic), gradient
  step. Writes `tools/spsa.log` + `tools/spsa_result.json` (resumable). A 150-iter
  run produced the tuned values now baked as the `TUNE_PARAMS` defaults.
- **Current tree = tuned defaults**, `make`d, perft-pass, **bench `1933988`**
  (moved from 1842508 — a real search change). Tuned binary saved at
  `tools/baselines/cc-1a-spsa`. The pre-tune baseline is `tools/baselines/latest
  -> cc-c4-incremental` (bench 1842508), untouched.
- **Verification incomplete:** the single 30s-hyperbullet SPRT (`cc-1a-spsa` vs
  `cc-c4-incremental`) was cut at **40 games: +43.7 ±69.6 Elo, 56.25%, LLR 0.24**
  — trending positive but statistically inconclusive (huge CI).
- **To resume:** rerun the SPRT to a real sample —
  `TC=30+0 CONCURRENCY=8 BASE=c4-incremental tools/sprt.sh` (build must be the
  tuned tree). **To revert the tune:** restore the original `TUNE_PARAMS`
  defaults (RfpMargin 80, RfpMarginImp 60, NullBase 3, NullDepthDiv 6,
  NullEvalDiv 200, ProbCutImp 60, FutBase 150, FutScale 100, HistPruneMul 4000,
  SeeCapMul 50, LmrHistDiv 8192, LmrDivX100 210, AspDelta 12, HistBonusMul 4,
  HistBonusMax 8000, LmpBase 3; others already at default) and rebuild → bench
  returns to 1842508. Nothing was `make install`ed, so the live bot is unchanged.
