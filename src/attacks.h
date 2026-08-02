#pragma once
// Unified attack lookups. Non-slider attacks come from precomputed tables;
// bishop/rook/queen route through magic bitboards.

#include "bitboard.h"
#include "magic.h"

// Occupancy-aware attacks for a given piece type on square s.
inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
    case BISHOP: return Magic::bishop_attacks(s, occ);
    case ROOK:   return Magic::rook_attacks(s, occ);
    case QUEEN:  return Magic::bishop_attacks(s, occ) | Magic::rook_attacks(s, occ);
    default:     return BB::PseudoAttacks[pt][s]; // KNIGHT / KING
    }
}

// Templated form for hot paths where the piece type is a compile-time constant.
template<PieceType Pt>
inline Bitboard attacks_bb(Square s, Bitboard occ) {
    static_assert(Pt != PAWN, "use pawn_attacks_bb for pawns");
    if constexpr (Pt == BISHOP) return Magic::bishop_attacks(s, occ);
    else if constexpr (Pt == ROOK) return Magic::rook_attacks(s, occ);
    else if constexpr (Pt == QUEEN)
        return Magic::bishop_attacks(s, occ) | Magic::rook_attacks(s, occ);
    else return BB::PseudoAttacks[Pt][s];
}
