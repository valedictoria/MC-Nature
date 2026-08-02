#pragma once
// Static Exchange Evaluation.

#include "types.h"

class Position;

// True iff the exchange initiated by m wins at least `threshold` centipawns.
bool see_ge(const Position& pos, Move m, int threshold);
