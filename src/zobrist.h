#pragma once
// Zobrist hashing keys (deterministically seeded at startup).

#include "types.h"

namespace Zobrist {

extern uint64_t psq[PIECE_NB][SQUARE_NB];
extern uint64_t enpassant[FILE_NB];
extern uint64_t castling[CASTLING_RIGHT_NB];
extern uint64_t side;

void init();

} // namespace Zobrist
