#pragma once
// Hand-crafted (tapered) evaluation. Returns centipawns from the side-to-move POV.

#include "types.h"

class Position;

Value evaluate(const Position& pos);
