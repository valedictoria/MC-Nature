#pragma once
// CarelessChess — core type definitions.
// Square layout is Little-Endian Rank-File (LERF): a1=0, h1=7, a8=56, h8=63.

#include <cstdint>

using Bitboard = uint64_t;

constexpr int MAX_PLY   = 128;
constexpr int MAX_MOVES = 256;

// ---- Colors ----------------------------------------------------------------
enum Color { WHITE, BLACK, COLOR_NB = 2 };
constexpr Color operator~(Color c) { return Color(c ^ BLACK); }

// ---- Piece types & pieces --------------------------------------------------
enum PieceType {
    NO_PIECE_TYPE, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    ALL_PIECES = 0,
    PIECE_TYPE_NB = 8
};

enum Piece {
    NO_PIECE,
    W_PAWN = 1, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = 9, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};

constexpr Piece     make_piece(Color c, PieceType pt) { return Piece((c << 3) + pt); }
constexpr PieceType type_of(Piece pc)                 { return PieceType(pc & 7); }
constexpr Color     color_of(Piece pc)                { return Color(pc >> 3); }
constexpr bool      piece_ok(Piece pc)                { return type_of(pc) != NO_PIECE_TYPE; }

// Rough material values (centipawns) used by SEE and MVV-LVA move ordering.
constexpr int PieceValue[PIECE_TYPE_NB] = { 0, 100, 320, 330, 500, 900, 0, 0 };

// ---- Squares ---------------------------------------------------------------
enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64,
    SQUARE_NB = 64
};

enum File : int { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H, FILE_NB };
enum Rank : int { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_NB };

constexpr File file_of(Square s) { return File(s & 7); }
constexpr Rank rank_of(Square s) { return Rank(s >> 3); }
constexpr Square make_square(File f, Rank r) { return Square((r << 3) + f); }
constexpr bool square_ok(Square s) { return s >= SQ_A1 && s <= SQ_H8; }

// Relative square/rank from a color's perspective (mirror for Black).
constexpr Square relative_square(Color c, Square s) { return Square(s ^ (c * 56)); }
constexpr Rank   relative_rank(Color c, Rank r)     { return Rank(r ^ (c * 7)); }
constexpr Rank   relative_rank(Color c, Square s)   { return relative_rank(c, rank_of(s)); }

// ---- Directions ------------------------------------------------------------
enum Direction : int {
    NORTH =  8, EAST =  1, SOUTH = -8, WEST = -1,
    NORTH_EAST = NORTH + EAST, SOUTH_EAST = SOUTH + EAST,
    SOUTH_WEST = SOUTH + WEST, NORTH_WEST = NORTH + WEST
};

constexpr Square operator+(Square s, Direction d) { return Square(int(s) + int(d)); }
constexpr Square operator-(Square s, Direction d) { return Square(int(s) - int(d)); }
constexpr Direction pawn_push(Color c) { return c == WHITE ? NORTH : SOUTH; }

// ---- Castling rights -------------------------------------------------------
enum CastlingRights {
    NO_CASTLING,
    WHITE_OO  = 1,
    WHITE_OOO = 2,
    BLACK_OO  = 4,
    BLACK_OOO = 8,
    KING_SIDE      = WHITE_OO  | BLACK_OO,
    QUEEN_SIDE     = WHITE_OOO | BLACK_OOO,
    WHITE_CASTLING = WHITE_OO  | WHITE_OOO,
    BLACK_CASTLING = BLACK_OO  | BLACK_OOO,
    ANY_CASTLING   = WHITE_CASTLING | BLACK_CASTLING,
    CASTLING_RIGHT_NB = 16
};

// ---- Moves (packed 16-bit: bits 0-5 to, 6-11 from, 12-13 promo, 14-15 type) -
enum MoveType {
    NORMAL,
    PROMOTION = 1 << 14,
    ENPASSANT = 2 << 14,
    CASTLING  = 3 << 14
};

using Move = uint16_t;

constexpr Move MOVE_NONE = 0;
constexpr Move MOVE_NULL = 65; // from==to==SQ_B1, harmless sentinel

constexpr Square to_sq(Move m)   { return Square(m & 0x3F); }
constexpr Square from_sq(Move m) { return Square((m >> 6) & 0x3F); }
constexpr int    from_to(Move m) { return m & 0xFFF; }
constexpr MoveType type_of_move(Move m) { return MoveType(m & (3 << 14)); }
constexpr PieceType promotion_type(Move m) { return PieceType(((m >> 12) & 3) + KNIGHT); }
constexpr bool is_ok(Move m) { return m != MOVE_NONE && m != MOVE_NULL; }

constexpr Move make_move(Square from, Square to) {
    return Move((int(from) << 6) | int(to));
}
template<MoveType T>
constexpr Move make_move(Square from, Square to, PieceType promo = KNIGHT) {
    return Move(int(T) | ((int(promo) - KNIGHT) << 12) | (int(from) << 6) | int(to));
}

// ---- Score / value constants (centipawns) ----------------------------------
// Value is a plain int so ordinary arithmetic Just Works.
using Value = int;

constexpr Value VALUE_ZERO     = 0;
constexpr Value VALUE_DRAW     = 0;
constexpr Value VALUE_NONE     = 32002;
constexpr Value VALUE_INFINITE = 32001;
constexpr Value VALUE_MATE     = 32000;
constexpr Value VALUE_MATE_IN_MAX_PLY  = VALUE_MATE - MAX_PLY;
constexpr Value VALUE_MATED_IN_MAX_PLY = -VALUE_MATE_IN_MAX_PLY;

constexpr Value mate_in(int ply)  { return VALUE_MATE - ply; }
constexpr Value mated_in(int ply) { return -VALUE_MATE + ply; }
constexpr bool  is_mate_score(int v) {
    return v >= VALUE_MATE_IN_MAX_PLY || v <= VALUE_MATED_IN_MAX_PLY;
}

// ---- Handy enum arithmetic helpers -----------------------------------------
#define ENABLE_INCR(T) \
    inline T& operator++(T& d) { return d = T(int(d) + 1); } \
    inline T& operator--(T& d) { return d = T(int(d) - 1); }
ENABLE_INCR(PieceType)
ENABLE_INCR(Square)
ENABLE_INCR(File)
ENABLE_INCR(Rank)
#undef ENABLE_INCR
