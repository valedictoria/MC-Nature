#pragma once
// Bitboard primitives and precomputed attack/geometry tables.

#include "types.h"

namespace BB {

constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileBBB = FileABB << 1;
constexpr Bitboard FileCBB = FileABB << 2;
constexpr Bitboard FileDBB = FileABB << 3;
constexpr Bitboard FileEBB = FileABB << 4;
constexpr Bitboard FileFBB = FileABB << 5;
constexpr Bitboard FileGBB = FileABB << 6;
constexpr Bitboard FileHBB = FileABB << 7;

constexpr Bitboard Rank1BB = 0xFF;
constexpr Bitboard Rank2BB = Rank1BB << (8 * 1);
constexpr Bitboard Rank3BB = Rank1BB << (8 * 2);
constexpr Bitboard Rank4BB = Rank1BB << (8 * 3);
constexpr Bitboard Rank5BB = Rank1BB << (8 * 4);
constexpr Bitboard Rank6BB = Rank1BB << (8 * 5);
constexpr Bitboard Rank7BB = Rank1BB << (8 * 6);
constexpr Bitboard Rank8BB = Rank1BB << (8 * 7);

extern Bitboard SquareBB[SQUARE_NB];
extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB]; // knight/bishop/rook/queen/king
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];         // squares strictly between (exclusive)
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];            // full line through both (incl. endpoints)
extern uint8_t  SquareDistance[SQUARE_NB][SQUARE_NB];

void init();

} // namespace BB

// ---- Bit access ------------------------------------------------------------
constexpr Bitboard square_bb(Square s) { return 1ULL << s; }

inline Bitboard  operator&(Bitboard b, Square s) { return b & square_bb(s); }
inline Bitboard  operator|(Bitboard b, Square s) { return b | square_bb(s); }
inline Bitboard  operator^(Bitboard b, Square s) { return b ^ square_bb(s); }
inline Bitboard& operator|=(Bitboard& b, Square s) { return b |= square_bb(s); }
inline Bitboard& operator^=(Bitboard& b, Square s) { return b ^= square_bb(s); }

constexpr Bitboard file_bb(File f) { return BB::FileABB << f; }
constexpr Bitboard file_bb(Square s) { return file_bb(file_of(s)); }
constexpr Bitboard rank_bb(Rank r) { return BB::Rank1BB << (8 * r); }
constexpr Bitboard rank_bb(Square s) { return rank_bb(rank_of(s)); }

inline bool more_than_one(Bitboard b) { return b & (b - 1); }

// ---- Shifting --------------------------------------------------------------
template<Direction D>
constexpr Bitboard shift(Bitboard b) {
    return D == NORTH      ? b << 8
         : D == SOUTH      ? b >> 8
         : D == EAST       ? (b & ~BB::FileHBB) << 1
         : D == WEST       ? (b & ~BB::FileABB) >> 1
         : D == NORTH_EAST ? (b & ~BB::FileHBB) << 9
         : D == NORTH_WEST ? (b & ~BB::FileABB) << 7
         : D == SOUTH_EAST ? (b & ~BB::FileHBB) >> 7
         : D == SOUTH_WEST ? (b & ~BB::FileABB) >> 9
         : 0;
}

// Pawn attack spans for a whole bitboard of pawns of color C.
template<Color C>
constexpr Bitboard pawn_attacks_bb(Bitboard b) {
    return C == WHITE ? shift<NORTH_WEST>(b) | shift<NORTH_EAST>(b)
                      : shift<SOUTH_WEST>(b) | shift<SOUTH_EAST>(b);
}

inline Bitboard pawn_attacks_bb(Color c, Square s) { return BB::PawnAttacks[c][s]; }

// ---- Population / scan (compiler builtins) --------------------------------
inline int popcount(Bitboard b) { return __builtin_popcountll(b); }

inline Square lsb(Bitboard b) { return Square(__builtin_ctzll(b)); }
inline Square msb(Bitboard b) { return Square(63 ^ __builtin_clzll(b)); }

inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}

// ---- Geometry lookups ------------------------------------------------------
inline int      distance(Square a, Square b) { return BB::SquareDistance[a][b]; }
inline Bitboard between_bb(Square a, Square b) { return BB::BetweenBB[a][b]; }
inline Bitboard line_bb(Square a, Square b)    { return BB::LineBB[a][b]; }

// True if a, b, c are on a common line (rank/file/diagonal).
inline bool aligned(Square a, Square b, Square c) { return BB::LineBB[a][b] & c; }
