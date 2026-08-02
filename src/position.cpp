#include "position.h"
#include "nnue/nnue.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace {
const std::string PieceChars = " PNBRQK  pnbrqk";

Piece char_to_piece(char c) {
    size_t i = PieceChars.find(c);
    return i == std::string::npos ? NO_PIECE : Piece(i);
}
} // namespace

// ---- Board mutators --------------------------------------------------------
void Position::put_piece(Piece pc, Square s) {
    board[s] = pc;
    byTypeBB[ALL_PIECES] |= s;
    byTypeBB[type_of(pc)] |= s;
    byColorBB[color_of(pc)] |= s;
}

void Position::remove_piece(Square s) {
    Piece pc = board[s];
    byTypeBB[ALL_PIECES] ^= s;
    byTypeBB[type_of(pc)] ^= s;
    byColorBB[color_of(pc)] ^= s;
    board[s] = NO_PIECE;
}

void Position::move_piece(Square from, Square to) {
    Piece pc = board[from];
    Bitboard fromTo = square_bb(from) | square_bb(to);
    byTypeBB[ALL_PIECES] ^= fromTo;
    byTypeBB[type_of(pc)] ^= fromTo;
    byColorBB[color_of(pc)] ^= fromTo;
    board[from] = NO_PIECE;
    board[to] = pc;
}

// ---- Setup -----------------------------------------------------------------
void Position::set(const std::string& fen, StateInfo* si) {
    std::memset(this, 0, sizeof(Position));
    std::memset(si, 0, sizeof(StateInfo));
    st = si;
    st->epSquare = SQ_NONE;
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
        board[s] = NO_PIECE;

    std::istringstream ss(fen);
    ss >> std::noskipws;
    char token;

    // 1) Piece placement (rank 8 down to rank 1).
    Square sq = SQ_A8;
    while ((ss >> token) && token != ' ') {
        if (std::isdigit(token))
            sq = Square(int(sq) + (token - '0'));
        else if (token == '/')
            sq = Square(int(sq) - 16);
        else {
            Piece pc = char_to_piece(token);
            if (pc != NO_PIECE) {
                put_piece(pc, sq);
                ++sq;
            }
        }
    }

    // 2) Side to move.
    ss >> token;
    sideToMove = (token == 'w') ? WHITE : BLACK;
    ss >> token; // trailing space

    // 3) Castling rights.
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
        castlingRightsMask[s] = 0;
    castlingRightsMask[SQ_E1] = WHITE_CASTLING;
    castlingRightsMask[SQ_A1] = WHITE_OOO;
    castlingRightsMask[SQ_H1] = WHITE_OO;
    castlingRightsMask[SQ_E8] = BLACK_CASTLING;
    castlingRightsMask[SQ_A8] = BLACK_OOO;
    castlingRightsMask[SQ_H8] = BLACK_OO;

    st->castlingRights = 0;
    while ((ss >> token) && token != ' ') {
        switch (token) {
        case 'K': st->castlingRights |= WHITE_OO;  break;
        case 'Q': st->castlingRights |= WHITE_OOO; break;
        case 'k': st->castlingRights |= BLACK_OO;  break;
        case 'q': st->castlingRights |= BLACK_OOO; break;
        default: break; // '-'
        }
    }

    // 4) En passant square.
    char epFile, epRank;
    if ((ss >> epFile) && (epFile != '-')) {
        ss >> epRank;
        st->epSquare = make_square(File(epFile - 'a'), Rank(epRank - '1'));
    }

    // 5) Halfmove and fullmove counters.
    int halfmove = 0, fullmove = 1;
    ss >> std::skipws >> halfmove >> fullmove;
    st->rule50 = halfmove;
    gamePly = std::max(2 * (fullmove - 1), 0) + (sideToMove == BLACK);

    // Derived state.
    st->key = 0;
    st->pawnKey = 0;
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
        if (board[s] != NO_PIECE) {
            st->key ^= Zobrist::psq[board[s]][s];
            if (type_of(board[s]) == PAWN)
                st->pawnKey ^= Zobrist::psq[board[s]][s];
        }
    if (st->epSquare != SQ_NONE)
        st->key ^= Zobrist::enpassant[file_of(st->epSquare)];
    if (sideToMove == BLACK)
        st->key ^= Zobrist::side;
    st->key ^= Zobrist::castling[st->castlingRights];

    st->checkersBB = attackers_to(square<KING>(sideToMove)) & pieces(~sideToMove);
    set_check_info();
}

std::string Position::fen() const {
    std::ostringstream ss;
    for (Rank r = RANK_8; r >= RANK_1; --r) {
        int empties = 0;
        for (File f = FILE_A; f <= FILE_H; ++f) {
            Square s = make_square(f, r);
            if (board[s] == NO_PIECE) { ++empties; continue; }
            if (empties) { ss << empties; empties = 0; }
            ss << PieceChars[board[s]];
        }
        if (empties) ss << empties;
        if (r > RANK_1) ss << '/';
    }
    ss << (sideToMove == WHITE ? " w " : " b ");
    if (st->castlingRights) {
        if (can_castle(WHITE_OO))  ss << 'K';
        if (can_castle(WHITE_OOO)) ss << 'Q';
        if (can_castle(BLACK_OO))  ss << 'k';
        if (can_castle(BLACK_OOO)) ss << 'q';
    } else ss << '-';
    ss << ' ';
    if (st->epSquare == SQ_NONE) ss << '-';
    else ss << char('a' + file_of(st->epSquare)) << char('1' + rank_of(st->epSquare));
    ss << ' ' << st->rule50 << ' ' << (1 + (gamePly - (sideToMove == BLACK)) / 2);
    return ss.str();
}

// ---- Attack queries --------------------------------------------------------
Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    return (pawn_attacks_bb(BLACK, s) & pieces(WHITE, PAWN))
         | (pawn_attacks_bb(WHITE, s) & pieces(BLACK, PAWN))
         | (attacks_bb<KNIGHT>(s, occ) & pieces(KNIGHT))
         | (attacks_bb<KING>(s, occ)   & pieces(KING))
         | (attacks_bb<BISHOP>(s, occ) & pieces(BISHOP, QUEEN))
         | (attacks_bb<ROOK>(s, occ)   & pieces(ROOK, QUEEN));
}

Bitboard Position::slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const {
    Bitboard blockers = 0;
    pinners = 0;

    Bitboard snipers = ((attacks_bb<ROOK>(s, 0)   & pieces(QUEEN, ROOK))
                      | (attacks_bb<BISHOP>(s, 0) & pieces(QUEEN, BISHOP))) & sliders;
    Bitboard occupancy = pieces() ^ snipers;

    while (snipers) {
        Square sniperSq = pop_lsb(snipers);
        Bitboard b = between_bb(s, sniperSq) & occupancy;
        if (b && !more_than_one(b)) {
            blockers |= b;
            if (b & pieces(color_of(piece_on(s))))
                pinners |= square_bb(sniperSq);
        }
    }
    return blockers;
}

void Position::set_check_info() {
    st->blockersForKing[WHITE] =
        slider_blockers(pieces(BLACK), square<KING>(WHITE), st->pinners[BLACK]);
    st->blockersForKing[BLACK] =
        slider_blockers(pieces(WHITE), square<KING>(BLACK), st->pinners[WHITE]);

    Square ksq = square<KING>(~sideToMove); // enemy king: squares that would check it
    st->checkSquares[PAWN]   = pawn_attacks_bb(~sideToMove, ksq);
    st->checkSquares[KNIGHT] = attacks_bb<KNIGHT>(ksq, pieces());
    st->checkSquares[BISHOP] = attacks_bb<BISHOP>(ksq, pieces());
    st->checkSquares[ROOK]   = attacks_bb<ROOK>(ksq, pieces());
    st->checkSquares[QUEEN]  = st->checkSquares[BISHOP] | st->checkSquares[ROOK];
    st->checkSquares[KING]   = 0;
}

// ---- Legality / check ------------------------------------------------------
bool Position::legal(Move m) const {
    Color us = sideToMove;
    Square from = from_sq(m), to = to_sq(m);
    MoveType mt = type_of_move(m);

    if (mt == ENPASSANT) {
        Square ksq = square<KING>(us);
        Square capsq = to - pawn_push(us);
        Bitboard occ = (pieces() ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
        return !(attacks_bb<ROOK>(ksq, occ) & pieces(~us, QUEEN, ROOK))
            && !(attacks_bb<BISHOP>(ksq, occ) & pieces(~us, QUEEN, BISHOP));
    }

    // Castling legality is fully validated in move generation.
    if (mt == CASTLING)
        return true;

    if (type_of(piece_on(from)) == KING)
        return !(attackers_to(to, pieces() ^ square_bb(from)) & pieces(~us));

    // A non-king move is legal unless it exposes the king (pinned and moving
    // off the pin ray).
    return !(blockers_for_king(us) & from) || aligned(from, to, square<KING>(us));
}

bool Position::gives_check(Move m) const {
    Color us = sideToMove;
    Square from = from_sq(m), to = to_sq(m);
    PieceType pt = type_of(piece_on(from));

    // Direct check.
    if (check_squares(pt) & to)
        return true;

    Square ksq = square<KING>(~us);

    // Discovered check: mover was blocking and leaves the king line.
    if ((blockers_for_king(~us) & from) && !aligned(from, to, ksq))
        return true;

    switch (type_of_move(m)) {
    case NORMAL:
        return false;
    case PROMOTION:
        return attacks_bb(promotion_type(m), to, pieces() ^ square_bb(from)) & ksq;
    case ENPASSANT: {
        Square capsq = make_square(file_of(to), rank_of(from));
        Bitboard occ = (pieces() ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
        return (attacks_bb<ROOK>(ksq, occ) & pieces(us, QUEEN, ROOK))
            || (attacks_bb<BISHOP>(ksq, occ) & pieces(us, QUEEN, BISHOP));
    }
    default: { // CASTLING: does the rook land on a checking square?
        Square rookTo = make_square(file_of(to) == FILE_G ? FILE_F : FILE_D, rank_of(from));
        return attacks_bb<ROOK>(rookTo, pieces()) & ksq;
    }
    }
}

// ---- Make / unmake ---------------------------------------------------------
void Position::do_move(Move m, StateInfo& newSt) {
    Color us = sideToMove, them = ~us;
    Square from = from_sq(m), to = to_sq(m);
    MoveType mt = type_of_move(m);
    Piece pc = piece_on(from);
    Piece captured = (mt == ENPASSANT) ? make_piece(them, PAWN) : piece_on(to);

    const bool nn = NNUE::is_active();
    if (nn) NNUE::begin_move();

    newSt.previous       = st;
    newSt.key            = st->key;
    newSt.pawnKey        = st->pawnKey;
    newSt.castlingRights = st->castlingRights;
    newSt.epSquare       = st->epSquare;
    newSt.rule50         = st->rule50;
    newSt.pliesFromNull  = st->pliesFromNull;
    st = &newSt;

    uint64_t key = st->key ^ Zobrist::side;
    uint64_t pawnKey = st->pawnKey;
    ++st->rule50;
    ++st->pliesFromNull;
    ++gamePly;

    if (st->epSquare != SQ_NONE) {
        key ^= Zobrist::enpassant[file_of(st->epSquare)];
        st->epSquare = SQ_NONE;
    }

    if (captured) {
        Square capsq = to;
        if (mt == ENPASSANT)
            capsq = to - pawn_push(us);
        key ^= Zobrist::psq[captured][capsq];
        if (type_of(captured) == PAWN)
            pawnKey ^= Zobrist::psq[captured][capsq];
        if (nn) NNUE::remove_feature(captured, capsq);
        remove_piece(capsq);
        st->rule50 = 0;
    }

    // Update castling rights when a king/rook leaves or a rook is captured.
    if (st->castlingRights && (castlingRightsMask[from] | castlingRightsMask[to])) {
        key ^= Zobrist::castling[st->castlingRights];
        st->castlingRights &= ~(castlingRightsMask[from] | castlingRightsMask[to]);
        key ^= Zobrist::castling[st->castlingRights];
    }

    // Move the piece (king for castling).
    move_piece(from, to);
    key ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    if (nn) {
        NNUE::remove_feature(pc, from);
        if (mt != PROMOTION)              // promotion adds the promoted piece below
            NNUE::add_feature(pc, to);
    }

    if (mt == CASTLING) {
        bool kingside = file_of(to) == FILE_G;
        Rank br = relative_rank(us, RANK_1);
        Square rFrom = make_square(kingside ? FILE_H : FILE_A, br);
        Square rTo   = make_square(kingside ? FILE_F : FILE_D, br);
        move_piece(rFrom, rTo);
        Piece rook = make_piece(us, ROOK);
        key ^= Zobrist::psq[rook][rFrom] ^ Zobrist::psq[rook][rTo];
        if (nn) {
            NNUE::remove_feature(rook, rFrom);
            NNUE::add_feature(rook, rTo);
        }
    }

    if (type_of(pc) == PAWN) {
        st->rule50 = 0;
        pawnKey ^= Zobrist::psq[pc][from];      // pawn left 'from'
        if (mt != PROMOTION)
            pawnKey ^= Zobrist::psq[pc][to];    // and (unless promoting) arrived on 'to'
        if ((int(to) ^ int(from)) == 16) {
            Square ep = to - pawn_push(us);
            if (pawn_attacks_bb(us, ep) & pieces(them, PAWN)) {
                st->epSquare = ep;
                key ^= Zobrist::enpassant[file_of(ep)];
            }
        } else if (mt == PROMOTION) {
            Piece promoted = make_piece(us, promotion_type(m));
            remove_piece(to);
            put_piece(promoted, to);
            key ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promoted][to];
            if (nn) NNUE::add_feature(promoted, to);
        }
    }

    st->capturedPiece = captured;
    st->key = key;
    st->pawnKey = pawnKey;
    sideToMove = them;

    if (nn) NNUE::end_move(*this);

    st->checkersBB = attackers_to(square<KING>(them)) & pieces(us);
    set_check_info();
}

void Position::undo_move(Move m) {
    if (NNUE::is_active()) NNUE::pop();
    sideToMove = ~sideToMove;
    Color us = sideToMove;
    Square from = from_sq(m), to = to_sq(m);
    MoveType mt = type_of_move(m);

    if (mt == PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(us, PAWN), to);
    }

    if (mt == CASTLING) {
        move_piece(to, from); // king back
        bool kingside = file_of(to) == FILE_G;
        Rank br = relative_rank(us, RANK_1);
        Square rFrom = make_square(kingside ? FILE_H : FILE_A, br);
        Square rTo   = make_square(kingside ? FILE_F : FILE_D, br);
        move_piece(rTo, rFrom); // rook back
    } else {
        move_piece(to, from);
        if (st->capturedPiece) {
            Square capsq = to;
            if (mt == ENPASSANT)
                capsq = to - pawn_push(us);
            put_piece(st->capturedPiece, capsq);
        }
    }

    st = st->previous;
    --gamePly;
}

void Position::do_null_move(StateInfo& newSt) {
    newSt = *st;
    newSt.previous = st;
    st = &newSt;

    uint64_t key = st->key ^ Zobrist::side;
    if (st->epSquare != SQ_NONE) {
        key ^= Zobrist::enpassant[file_of(st->epSquare)];
        st->epSquare = SQ_NONE;
    }
    st->key = key;
    ++st->rule50;
    st->pliesFromNull = 0;
    sideToMove = ~sideToMove;

    st->checkersBB = 0; // null move only made when not in check
    set_check_info();

    if (NNUE::is_active()) NNUE::push_null();
}

void Position::undo_null_move() {
    if (NNUE::is_active()) NNUE::pop();
    st = st->previous;
    sideToMove = ~sideToMove;
}

// ---- Draw detection --------------------------------------------------------
bool Position::has_repeated() const {
    const StateInfo* cur = st;
    int end = std::min(cur->rule50, cur->pliesFromNull);
    if (end < 4)
        return false;
    const StateInfo* stp = cur->previous->previous;
    for (int i = 4; i <= end; i += 2) {
        stp = stp->previous->previous;
        if (stp->key == cur->key)
            return true;
    }
    return false;
}

bool Position::is_draw(int ply) const {
    if (st->rule50 > 99)
        return true;
    // Two-fold within the search tree counts as a draw.
    const StateInfo* cur = st;
    int end = std::min(cur->rule50, cur->pliesFromNull);
    if (end < 4)
        return false;
    const StateInfo* stp = cur;
    int cnt = 0;
    for (int i = 4; i <= end; i += 2) {
        stp = stp->previous->previous;
        if (stp->key == cur->key) {
            if (++cnt >= 1)
                return true;
        }
    }
    (void)ply;
    return false;
}
