#include "see.h"

#include "position.h"
#include "bitboard.h"

bool see_ge(const Position& pos, Move m, int threshold) {
    // Non-normal moves (promotion, en passant, castling) are conservatively kept.
    if (type_of_move(m) != NORMAL)
        return 0 >= threshold;

    Square from = from_sq(m), to = to_sq(m);

    int swap = PieceValue[type_of(pos.piece_on(to))] - threshold;
    if (swap < 0)
        return false;

    swap = PieceValue[type_of(pos.piece_on(from))] - swap;
    if (swap <= 0)
        return true;

    Bitboard occupied = pos.pieces() ^ square_bb(from) ^ square_bb(to);
    Color stm = color_of(pos.piece_on(from));
    Bitboard attackers = pos.attackers_to(to, occupied);
    Bitboard bishops = pos.pieces(BISHOP, QUEEN);
    Bitboard rooks   = pos.pieces(ROOK, QUEEN);
    int res = 1;

    while (true) {
        stm = ~stm;
        attackers &= occupied;

        Bitboard stmAttackers = attackers & pos.pieces(stm);
        if (!stmAttackers)
            break;

        res ^= 1;

        Bitboard bb;
        if ((bb = stmAttackers & pos.pieces(PAWN))) {
            if ((swap = PieceValue[PAWN] - swap) < res) break;
            occupied ^= bb & (~bb + 1);
            attackers |= attacks_bb<BISHOP>(to, occupied) & bishops;
        } else if ((bb = stmAttackers & pos.pieces(KNIGHT))) {
            if ((swap = PieceValue[KNIGHT] - swap) < res) break;
            occupied ^= bb & (~bb + 1);
        } else if ((bb = stmAttackers & pos.pieces(BISHOP))) {
            if ((swap = PieceValue[BISHOP] - swap) < res) break;
            occupied ^= bb & (~bb + 1);
            attackers |= attacks_bb<BISHOP>(to, occupied) & bishops;
        } else if ((bb = stmAttackers & pos.pieces(ROOK))) {
            if ((swap = PieceValue[ROOK] - swap) < res) break;
            occupied ^= bb & (~bb + 1);
            attackers |= attacks_bb<ROOK>(to, occupied) & rooks;
        } else if ((bb = stmAttackers & pos.pieces(QUEEN))) {
            if ((swap = PieceValue[QUEEN] - swap) < res) break;
            occupied ^= bb & (~bb + 1);
            attackers |= (attacks_bb<BISHOP>(to, occupied) & bishops)
                       | (attacks_bb<ROOK>(to, occupied) & rooks);
        } else { // king: legal only if the opponent has no defenders left
            return (attackers & ~pos.pieces(stm)) ? bool(res ^ 1) : bool(res);
        }
    }
    return bool(res);
}
