#include "bitboard.h"

#include <algorithm>

namespace BB {

Bitboard SquareBB[SQUARE_NB];
Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];
uint8_t  SquareDistance[SQUARE_NB][SQUARE_NB];

namespace {

// Slow on-the-fly slider generator used only during init (magic is used at runtime).
Bitboard sliding_attack(const int deltas[4], Square sq, Bitboard occupied) {
    Bitboard attack = 0;
    for (int i = 0; i < 4; ++i) {
        Square s = sq;
        int d = deltas[i];
        while (true) {
            Square next = Square(int(s) + d);
            if (!square_ok(next))
                break;
            // Guard against wrap-around: a single step never crosses > 2 files.
            if (BB::SquareDistance[s][next] > 2)
                break;
            attack |= square_bb(next);
            if (occupied & next)
                break;
            s = next;
        }
    }
    return attack;
}

} // namespace

void init() {
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
        SquareBB[s] = 1ULL << s;

    for (Square a = SQ_A1; a <= SQ_H8; ++a)
        for (Square b = SQ_A1; b <= SQ_H8; ++b)
            SquareDistance[a][b] =
                uint8_t(std::max(std::abs(file_of(a) - file_of(b)),
                                 std::abs(rank_of(a) - rank_of(b))));

    const int knightDeltas[8] = { 17, 15, 10, 6, -17, -15, -10, -6 };
    const int kingDeltas[8]   = { 8, 9, 1, -7, -8, -9, -1, 7 };
    const int bishopDeltas[4] = { NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST };
    const int rookDeltas[4]   = { NORTH, EAST, SOUTH, WEST };

    for (Square s = SQ_A1; s <= SQ_H8; ++s) {
        // Pawn attacks.
        Bitboard b = square_bb(s);
        PawnAttacks[WHITE][s] = pawn_attacks_bb<WHITE>(b);
        PawnAttacks[BLACK][s] = pawn_attacks_bb<BLACK>(b);

        // Knight / king via bounded deltas.
        for (int i = 0; i < 8; ++i) {
            Square to = Square(int(s) + knightDeltas[i]);
            if (square_ok(to) && SquareDistance[s][to] <= 2)
                PseudoAttacks[KNIGHT][s] |= square_bb(to);
            to = Square(int(s) + kingDeltas[i]);
            if (square_ok(to) && SquareDistance[s][to] <= 2)
                PseudoAttacks[KING][s] |= square_bb(to);
        }

        // Sliders on an empty board.
        PseudoAttacks[BISHOP][s] = sliding_attack(bishopDeltas, s, 0);
        PseudoAttacks[ROOK][s]   = sliding_attack(rookDeltas, s, 0);
        PseudoAttacks[QUEEN][s]  = PseudoAttacks[BISHOP][s] | PseudoAttacks[ROOK][s];
    }

    // Between / line tables from the two slider families.
    for (Square a = SQ_A1; a <= SQ_H8; ++a) {
        for (PieceType pt : { BISHOP, ROOK }) {
            const int* deltas = (pt == BISHOP) ? bishopDeltas : rookDeltas;
            for (Square b = SQ_A1; b <= SQ_H8; ++b) {
                if (!(PseudoAttacks[pt][a] & b))
                    continue;
                LineBB[a][b] = (sliding_attack(deltas, a, 0) & sliding_attack(deltas, b, 0))
                             | square_bb(a) | square_bb(b);
                BetweenBB[a][b] = sliding_attack(deltas, a, square_bb(b))
                                & sliding_attack(deltas, b, square_bb(a));
            }
        }
    }
}

} // namespace BB
