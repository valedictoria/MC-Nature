# Nature(v2) campaign results — SIMD, time management, usability, Lazy SMP

Scope: Nature(v2) only (MC-Nature). Nothing here is `make install`ed — the live
Lichess bot is unchanged throughout. Full plan: `/Users/titojr/.claude/plans/cryptic-wondering-giraffe.md`.

Test protocol: each feature is its own commit-equivalent snapshot
(`tools/sprt.sh snapshot <tag>`, chained — each kept feature becomes the next
baseline). "Quick gate" = 30 games at `tc=30+0` (30s hyperbullet), a fast
**directional smoke-test, not a statistically rigorous SPRT** (the project's
own SPSA verification was previously cut at 40 games and called
"inconclusive" for the same reason — wide confidence intervals at this sample
size). Anything promising here should get a longer confirmatory run through
`tools/sprt.sh`'s full LLR-based SPRT before being trusted as a real Elo claim.

## Task 1 — the four original questions

1. **BT4 net?** No — architecturally incompatible (HalfKAv2_hm feature set vs.
   this engine's flat-768 king-bucketed NNUE, no SIMD, no net-format parser).
   Not attempted. Logged as a future from-scratch project, not a "swap".
2. **SIMD?** Implemented (NEON, prior session) — bit-identical to scalar,
   confirmed via bench before any Elo claim.
3. **Time management?** Implemented (score-drop soft-limit extension, prior
   session) — layered on existing best-move-stability scaling.
4. **Usability for an external GUI?** Implemented (MultiPV, seldepth,
   hashfull, ponder move; `go searchmoves`; graceful `go ponder`) — all
   correctness-checked, no Elo claim needed/expected at defaults.

## Task 2 — campaign results, per feature

| # | Feature | Bench (nodes) | Result |
|---|---|---|---|
| 1 | NEON SIMD (NNUE flatten/output + accumulator) | 1933988 (baseline, from prior session) | Bit-identical to scalar; nps improvement, no Elo regression |
| 2 | Time management: score-drop extension | 1933988 (no-op at defaults) | Correctness-checked |
| 3 | Usability: MultiPV/seldepth/hashfull/ponder | 1933988 (no-op at MultiPV=1) | Correctness-checked |
| 4 | `go searchmoves` + `go ponder` handling | 1933988 (no-op) | Correctness-checked. **Note:** `lichess-bot/config.yml` has `ponder: true` — the previously-installed binary was silently running an immediate timed search instead of pondering (no `ponder` case in `go()`'s token loop). Now handled; still not `make install`ed. |
| 5 | Correction-history expansion (non-pawn/material + continuation keys) | 1605316 → tuned to 1625091 (king-safety key dropped after regressing) | Kept, tuned down to the two-key version |
| 6 | **Razoring** (depth≤3, tunable `RazorMargin`) | 1703779 | Quick-gate: 10W–7L–13D, 55.0%, **+34.9 ±75.1 Elo**, LOS 82.4%. Directionally positive, wide CI — kept per your call, **not yet run through a longer confirmatory SPRT** |
| 7a | Lazy SMP: TT XOR-trick lockless sync | 1703779 (bit-identical) | `TTEntry::key16` now XOR-encoded against the rest of the entry's fields; a torn concurrent read/write self-corrects to a probe miss instead of a corrupted hit. Verified bit-identical single-threaded |
| 7b | Lazy SMP: SearchWorker refactor | 1703779 (bit-identical) | ~13 global search-state variables (history tables, PV, root move, limits, node counter, `TimeManager`) moved into a per-thread `SearchWorker` class; `TT`/`Reductions`/`Tune::*` stay shared. Verified bit-identical, `ucinewgame`/MultiPV/ponder all re-tested |
| — | **Discovered blocker 1:** NNUE accumulator state was a single global (`accStack`/`accHead`/`accActive`/diff buffers), not thread-safe | 1703779 (bit-identical) | Not in the original plan's risk list. Fixed: marked `thread_local` in `nnue.cpp`. Verified bit-identical |
| — | **Discovered blocker 2:** `Position`'s copy constructor was `delete`d; repetition detection walks a `StateInfo` linked list that a FEN-only reconstruction would truncate (crash risk on any real game with `rule50 > 0`) | 1703779 (bit-identical) | Fixed: enabled the (verified-safe) default shallow copy — the `StateInfo` ancestor chain is immutable once built, so a shallow copy safely diverges from a shared, read-only history. Documented the safety argument in `position.h` |
| 7c/7d | Lazy SMP: 2nd worker thread, `Threads` UCI option (max 2), aggregate node reporting | 1703779 (bit-identical at `Threads=1`) | `Threads=2` spawns a helper `SearchWorker` on its own `Position` copy, staggered starting depth, shares `TT`/`Reductions`/`Tune::*`. Quick-gate **Threads=2 vs Threads=1: 6W–2L–22D, 56.67%, +46.6 ±39.8 Elo, LOS 99.0%** — tighter CI and stronger LOS than razoring's gate; a credible signal at this TC, still just a 30-game smoke-test |
| 8 | Skill Level (depth cap 1..20) / Contempt (draw-score bias) | 1703779 (no-op at defaults: SkillLevel=20, Contempt=0) | Correctness-checked: `Skill Level 5` empirically capped search at depth 6 (=1+5) as designed; `Contempt`'s `draw_value()` formula verified by inspection (reduces to exact `VALUE_DRAW` at 0, proven via bit-identical bench) |

**Final bench signature: `1703779`** (from the SPSA-tuned baseline `1933988`,
moved by corrhist expansion + razoring; every step after razoring is a
verified no-op).

## What to trust vs. not

- **Correctness-verified, safe to rely on:** SIMD, time management,
  usability additions, `searchmoves`/`ponder`, the TT XOR sync, the
  SearchWorker refactor, the NNUE thread-safety fix, the Position copy-safety
  fix, Skill Level, Contempt. All either bit-identical no-ops or directly
  functionally tested.
- **Directionally positive, NOT yet statistically confirmed:** razoring
  (+34.9 ±75.1 Elo) and Lazy SMP 2-thread (+46.6 ±39.8 Elo). Both are quick
  30-game smoke-tests. Recommend a longer confirmatory SPRT
  (`tools/sprt.sh`, full LLR-based, `tc=8+0.08 elo0=0 elo1=10`) before either
  is treated as a real Elo claim — especially before `make install`.
- **Nothing in this campaign has been `make install`ed.** The live bot is
  running whatever was installed before this session; see `tools/baselines/`
  for every intermediate snapshot if you want to bisect or roll back.

## Backlog — not attempted this round

- **Confirmatory SPRTs** for razoring and Lazy SMP 2-thread (see above) —
  the natural next step before either goes live.
- **Node-based time management** (Tier-2, already on the project's own
  to-do list) — bigger redesign than this campaign's scope.
- **Scaling Lazy SMP beyond 2 threads**, if the 2-thread result holds up
  under a longer SPRT. The current implementation (shared `TT`, per-thread
  `SearchWorker`, `thread_local` NNUE) generalizes to N threads without
  further architectural changes — just raising `Threads`' max and spawning
  more helpers.
- **A from-scratch, properly-scoped HalfKA-style bigger-net project** (BT4 or
  otherwise) — explicitly not a "swap the net file" job (see Task 1, Q1).
  Would need its own feature set, accumulator-refresh design, real `.nnue`
  parser, and SIMD to be worth the wider net's cost.
- **King-bucket cache** for the NNUE incremental accumulator (avoid repeat
  full rebuilds when the king shuffles back and forth across a bucket
  boundary) — noted as a possible follow-up in the engine's own CLAUDE.md,
  unrelated to this campaign but adjacent to the accumulator code touched
  here.
- **The original SPSA-tuning SPRT verification** is still open from before
  this campaign (cut at 40 games, "inconclusive") — this campaign's razoring
  and Lazy-SMP quick-gates are in the same boat and could reasonably be
  batched into one longer confirmatory session.
