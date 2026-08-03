#pragma once
// Board state: bitboard-per-{color,type} representation with make/unmake and
// incremental Zobrist hashing.

#include "types.h"
#include "bitboard.h"
#include "attacks.h"
#include "zobrist.h"

#include <string>

// Per-move state saved for unmake and derived once per node.
struct StateInfo {
    // Copied / persistent across a move.
    uint64_t key;
    uint64_t pawnKey;   // Zobrist key over pawns only (for eval correction history).
    int      castlingRights;
    Square   epSquare;
    int      rule50;
    int      pliesFromNull;

    // Recomputed each move.
    Piece    capturedPiece;
    Bitboard checkersBB;
    Bitboard blockersForKing[COLOR_NB];
    Bitboard pinners[COLOR_NB];
    Bitboard checkSquares[PIECE_TYPE_NB];

    StateInfo* previous;
};

class Position {
public:
    Position() = default;
    // Every member is either a plain value (board/bitboards/counters) or the
    // `st` StateInfo-chain pointer. do_move never mutates an existing
    // StateInfo node in place — it only ever links a *new* one supplied by
    // the caller on top of the current chain — so a shallow copy safely
    // shares the (immutable) ancestor history with the original and then
    // diverges cleanly the moment either copy makes its own move. This is
    // what lets a Lazy-SMP helper thread get its own independent search
    // root without truncating repetition/50-move history.
    Position(const Position&) = default;
    Position& operator=(const Position&) = default;

    void set(const std::string& fen, StateInfo* si);
    std::string fen() const;

    // Board queries.
    Piece    piece_on(Square s) const { return board[s]; }
    bool     empty(Square s) const    { return board[s] == NO_PIECE; }
    Color    side_to_move() const     { return sideToMove; }
    Square   ep_square() const        { return st->epSquare; }

    Bitboard pieces() const                       { return byTypeBB[ALL_PIECES]; }
    Bitboard pieces(PieceType pt) const           { return byTypeBB[pt]; }
    Bitboard pieces(PieceType a, PieceType b) const { return byTypeBB[a] | byTypeBB[b]; }
    Bitboard pieces(Color c) const                { return byColorBB[c]; }
    Bitboard pieces(Color c, PieceType pt) const  { return byColorBB[c] & byTypeBB[pt]; }
    Bitboard pieces(Color c, PieceType a, PieceType b) const {
        return byColorBB[c] & (byTypeBB[a] | byTypeBB[b]);
    }
    template<PieceType Pt> Square square(Color c) const { return lsb(pieces(c, Pt)); }
    int count(Color c, PieceType pt) const { return popcount(pieces(c, pt)); }

    // Castling.
    int  castling_rights() const { return st->castlingRights; }
    bool can_castle(CastlingRights cr) const { return st->castlingRights & cr; }

    // Attack / check info.
    Bitboard attackers_to(Square s) const { return attackers_to(s, pieces()); }
    Bitboard attackers_to(Square s, Bitboard occ) const;
    Bitboard checkers() const { return st->checkersBB; }
    Bitboard blockers_for_king(Color c) const { return st->blockersForKing[c]; }
    Bitboard check_squares(PieceType pt) const { return st->checkSquares[pt]; }
    Bitboard slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const;

    // Move properties.
    bool  legal(Move m) const;
    bool  gives_check(Move m) const;
    Piece moved_piece(Move m) const { return board[from_sq(m)]; }
    bool  capture(Move m) const {
        return (!empty(to_sq(m)) && type_of_move(m) != CASTLING) || type_of_move(m) == ENPASSANT;
    }

    // Make / unmake.
    void do_move(Move m, StateInfo& newSt);
    void undo_move(Move m);
    void do_null_move(StateInfo& newSt);
    void undo_null_move();

    // Draw detection helpers.
    uint64_t key() const { return st->key; }
    uint64_t pawn_key() const { return st->pawnKey; }
    int      rule50_count() const { return st->rule50; }
    bool     is_draw(int ply) const;
    bool     has_repeated() const;

    const StateInfo* state() const { return st; }

private:
    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    void set_check_info();

    Piece    board[SQUARE_NB];
    Bitboard byTypeBB[PIECE_TYPE_NB];
    Bitboard byColorBB[COLOR_NB];
    int      castlingRightsMask[SQUARE_NB];
    Color    sideToMove;
    int      gamePly;
    StateInfo* st;
};
