#include "movegen.h"

#include <initializer_list>

namespace {

inline ExtMove* add(const Position& pos, ExtMove* list, Move m) {
    if (pos.legal(m))
        (*list++).move = m;
    return list;
}

template<Color Us>
ExtMove* generate_king(const Position& pos, ExtMove* list, Bitboard captureMask) {
    Square ksq = pos.square<KING>(Us);
    Bitboard att = attacks_bb<KING>(ksq, pos.pieces()) & ~pos.pieces(Us) & captureMask;
    while (att)
        list = add(pos, list, make_move(ksq, pop_lsb(att)));
    return list;
}

template<Color Us, bool CapturesOnly>
ExtMove* generate_all(const Position& pos, ExtMove* list) {
    constexpr Color Them = ~Us;
    constexpr Direction Up   = Us == WHITE ? NORTH : SOUTH;
    constexpr Direction Cap1 = Us == WHITE ? NORTH_EAST : SOUTH_EAST;
    constexpr Direction Cap2 = Us == WHITE ? NORTH_WEST : SOUTH_WEST;

    const Square   ksq      = pos.square<KING>(Us);
    const Bitboard occ      = pos.pieces();
    const Bitboard checkers = pos.checkers();
    const Bitboard ownPieces = pos.pieces(Us);
    const Bitboard theirPieces = pos.pieces(Them);

    // King target: captures-only restricts to enemy squares.
    Bitboard kingMask = CapturesOnly ? theirPieces : ~Bitboard(0);

    if (more_than_one(checkers))               // double check: only the king may move
        return generate_king<Us>(pos, list, kingMask);

    Bitboard target = checkers ? (between_bb(ksq, lsb(checkers)) | checkers) : ~Bitboard(0);
    target &= ~ownPieces;

    // ---- Pawns ----
    const Bitboard pawns   = pos.pieces(Us, PAWN);
    const Bitboard rank7   = Us == WHITE ? BB::Rank7BB : BB::Rank2BB;
    const Bitboard rank3   = Us == WHITE ? BB::Rank3BB : BB::Rank6BB;
    const Bitboard promo   = pawns & rank7;
    const Bitboard noPromo = pawns & ~rank7;
    const Bitboard empty   = ~occ;

    if (!CapturesOnly) {
        Bitboard b1 = shift<Up>(noPromo) & empty;
        Bitboard b2 = shift<Up>(b1 & rank3) & empty;
        b1 &= target;
        b2 &= target;
        while (b1) { Square to = pop_lsb(b1); list = add(pos, list, make_move(to - Up, to)); }
        while (b2) { Square to = pop_lsb(b2); list = add(pos, list, make_move(to - Up - Up, to)); }
    }

    // Non-promoting captures.
    {
        Bitboard c1 = shift<Cap1>(noPromo) & theirPieces & target;
        Bitboard c2 = shift<Cap2>(noPromo) & theirPieces & target;
        while (c1) { Square to = pop_lsb(c1); list = add(pos, list, make_move(to - Cap1, to)); }
        while (c2) { Square to = pop_lsb(c2); list = add(pos, list, make_move(to - Cap2, to)); }
    }

    // Promotions (push + capture); always considered tactical.
    if (promo) {
        Bitboard pp = shift<Up>(promo)   & empty       & target;
        Bitboard p1 = shift<Cap1>(promo) & theirPieces & target;
        Bitboard p2 = shift<Cap2>(promo) & theirPieces & target;
        auto emit = [&](Bitboard b, Direction d) {
            while (b) {
                Square to = pop_lsb(b);
                Square from = to - d;
                for (PieceType pt : { QUEEN, ROOK, BISHOP, KNIGHT })
                    list = add(pos, list, make_move<PROMOTION>(from, to, pt));
            }
        };
        emit(pp, Up);
        emit(p1, Cap1);
        emit(p2, Cap2);
    }

    // En passant.
    if (pos.ep_square() != SQ_NONE) {
        Square ep = pos.ep_square();
        Square capsq = ep - Up;
        if (!checkers || (checkers & square_bb(capsq))) {
            Bitboard attackers = noPromo & pawn_attacks_bb(Them, ep);
            while (attackers)
                list = add(pos, list, make_move<ENPASSANT>(pop_lsb(attackers), ep));
        }
    }

    // ---- Knights / bishops / rooks / queens ----
    for (PieceType pt : { KNIGHT, BISHOP, ROOK, QUEEN }) {
        Bitboard b = pos.pieces(Us, pt);
        while (b) {
            Square from = pop_lsb(b);
            Bitboard att = attacks_bb(pt, from, occ) & target;
            if (CapturesOnly)
                att &= theirPieces;
            while (att)
                list = add(pos, list, make_move(from, pop_lsb(att)));
        }
    }

    // ---- King ----
    list = generate_king<Us>(pos, list, kingMask);

    // ---- Castling ----
    if (!CapturesOnly && !checkers) {
        constexpr Rank R = Us == WHITE ? RANK_1 : RANK_8;
        const Square e = make_square(FILE_E, R);
        auto path_clear = [&](std::initializer_list<File> files) {
            for (File f : files)
                if (!pos.empty(make_square(f, R))) return false;
            return true;
        };
        auto path_safe = [&](std::initializer_list<File> files) {
            for (File f : files)
                if (pos.attackers_to(make_square(f, R)) & theirPieces) return false;
            return true;
        };
        constexpr CastlingRights OO  = Us == WHITE ? WHITE_OO  : BLACK_OO;
        constexpr CastlingRights OOO = Us == WHITE ? WHITE_OOO : BLACK_OOO;

        if (pos.can_castle(OO) && path_clear({ FILE_F, FILE_G }) && path_safe({ FILE_E, FILE_F, FILE_G }))
            list = add(pos, list, make_move<CASTLING>(e, make_square(FILE_G, R)));
        if (pos.can_castle(OOO) && path_clear({ FILE_B, FILE_C, FILE_D }) && path_safe({ FILE_E, FILE_D, FILE_C }))
            list = add(pos, list, make_move<CASTLING>(e, make_square(FILE_C, R)));
    }

    return list;
}

} // namespace

ExtMove* generate_legal(const Position& pos, ExtMove* list) {
    return pos.side_to_move() == WHITE ? generate_all<WHITE, false>(pos, list)
                                       : generate_all<BLACK, false>(pos, list);
}

ExtMove* generate_tactical(const Position& pos, ExtMove* list) {
    // In check, all evasions are relevant; otherwise captures + promotions only.
    if (pos.checkers())
        return generate_legal(pos, list);
    return pos.side_to_move() == WHITE ? generate_all<WHITE, true>(pos, list)
                                       : generate_all<BLACK, true>(pos, list);
}
