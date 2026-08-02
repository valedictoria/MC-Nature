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
