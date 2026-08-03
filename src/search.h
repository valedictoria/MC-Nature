#pragma once
// Search driver: iterative deepening + PVS alpha-beta + quiescence.

#include "types.h"
#include "timeman.h"

#include <atomic>
#include <iosfwd>
#include <string>

class Position;
struct StateInfo;

namespace Search {

extern std::atomic<bool> stop;
extern TimeManager Time;
extern int64_t nodes;

// UCI MultiPV option (1..8, default 1). Setting it above 1 has zero effect on
// the bench signature or single-line search until it's actually raised.
void set_multipv(int n);

// UCI Threads option (1 or 2). 2 spawns a Lazy-SMP helper worker alongside
// the reporting thread for every think() call; default 1 is unchanged.
void set_threads(int n);

// Weaker-play / draw-bias polish for an external GUI. Both are no-ops at
// their defaults (SkillLevel 20, Contempt 0) — no Elo claim, correctness only.
void set_skill_level(int n);
void set_contempt(int n);

void init();            // precompute reduction table
void clear();           // reset history / killers / TT generation between games

// Runs iterative deepening on pos (blocking) and prints UCI info + bestmove.
void think(Position& pos, const Limits& limits);

// ---- Runtime-tunable search parameters (for SPSA) ----
// set_tune returns false for an unknown name (so the caller can ignore it).
// print_tune_options emits one UCI "option name ... type spin ..." line each.
bool set_tune(const std::string& name, int value);
void print_tune_options(std::ostream& os);

} // namespace Search
