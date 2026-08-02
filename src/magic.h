#pragma once
// Fancy magic bitboards for sliding-piece attacks.
// Magics are searched at startup (no PEXT on Apple Silicon).

#include "bitboard.h"

namespace Magic {

struct MagicEntry {
    Bitboard  mask;
    Bitboard  magic;
    Bitboard* attacks;
    unsigned  shift;

    unsigned index(Bitboard occ) const {
        return unsigned(((occ & mask) * magic) >> shift);
    }
};

extern MagicEntry BishopMagics[SQUARE_NB];
extern MagicEntry RookMagics[SQUARE_NB];

void init();

inline Bitboard bishop_attacks(Square s, Bitboard occ) {
    const MagicEntry& m = BishopMagics[s];
    return m.attacks[m.index(occ)];
}
inline Bitboard rook_attacks(Square s, Bitboard occ) {
    const MagicEntry& m = RookMagics[s];
    return m.attacks[m.index(occ)];
}

} // namespace Magic
