#pragma once
// Perft: move-generation correctness verification.

#include "position.h"
#include <cstdint>

uint64_t perft(Position& pos, int depth);
void     perft_divide(Position& pos, int depth);
bool     perft_suite(); // returns true iff all positions pass
