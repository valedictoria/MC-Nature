#include "syzygy.h"

#include "../position.h"
#include "../movegen.h"
#include "tbprobe.h"

namespace Syzygy {

int MaxPieces = 0;

void init(const std::string& path) {
    if (path.empty() || path == "<empty>") {
        tb_free();
        MaxPieces = 0;
        return;
    }
    MaxPieces = tb_init(path.c_str()) ? int(TB_LARGEST) : 0;
}

namespace {

// Pack the current position into Fathom's per-type (both-color) bitboards.
void fill(const Position& pos, uint64_t& white, uint64_t& black, uint64_t& kings,
          uint64_t& queens, uint64_t& rooks, uint64_t& bishops, uint64_t& knights,
          uint64_t& pawns, unsigned& ep) {
    white   = pos.pieces(WHITE);
    black   = pos.pieces(BLACK);
    kings   = pos.pieces(KING);
    queens  = pos.pieces(QUEEN);
    rooks   = pos.pieces(ROOK);
    bishops = pos.pieces(BISHOP);
    knights = pos.pieces(KNIGHT);
    pawns   = pos.pieces(PAWN);
    // Fathom expects 0 for "no ep square"; a1 (0) is never a real ep target.
    ep = pos.ep_square() == SQ_NONE ? 0u : unsigned(pos.ep_square());
}

int wdl_to_sign(unsigned wdl) {
    return wdl == TB_WIN ? 1 : wdl == TB_LOSS ? -1 : 0; // cursed/blessed/draw -> 0
}

} // namespace

bool probe_wdl(const Position& pos, int& wdl) {
    uint64_t white, black, kings, queens, rooks, bishops, knights, pawns;
    unsigned ep;
    fill(pos, white, black, kings, queens, rooks, bishops, knights, pawns, ep);

    unsigned res = tb_probe_wdl(white, black, kings, queens, rooks, bishops, knights,
                                pawns, unsigned(pos.rule50_count()), 0u, ep,
                                pos.side_to_move() == WHITE);
    if (res == TB_RESULT_FAILED)
        return false;
    wdl = wdl_to_sign(TB_GET_WDL(res));
    return true;
}

bool probe_root(const Position& pos, Move& best, int& wdl) {
    uint64_t white, black, kings, queens, rooks, bishops, knights, pawns;
    unsigned ep;
    fill(pos, white, black, kings, queens, rooks, bishops, knights, pawns, ep);

    unsigned res = tb_probe_root(white, black, kings, queens, rooks, bishops, knights,
                                 pawns, unsigned(pos.rule50_count()), 0u, ep,
                                 pos.side_to_move() == WHITE, nullptr);
    if (res == TB_RESULT_FAILED || res == TB_RESULT_CHECKMATE || res == TB_RESULT_STALEMATE)
        return false;

    wdl = wdl_to_sign(TB_GET_WDL(res));

    unsigned from  = TB_GET_FROM(res);
    unsigned to    = TB_GET_TO(res);
    unsigned promo = TB_GET_PROMOTES(res);
    PieceType wantPromo = promo == TB_PROMOTES_QUEEN  ? QUEEN
                        : promo == TB_PROMOTES_ROOK   ? ROOK
                        : promo == TB_PROMOTES_BISHOP ? BISHOP
                        : promo == TB_PROMOTES_KNIGHT ? KNIGHT
                        : NO_PIECE_TYPE;

    // Recover the engine's exact Move (right MoveType for ep/castling/promo) by
    // matching Fathom's from/to/promo against a generated legal move.
    ExtMove buf[MAX_MOVES];
    ExtMove* end = generate_legal(pos, buf);
    for (ExtMove* it = buf; it != end; ++it) {
        Move m = it->move;
        if (unsigned(from_sq(m)) != from || unsigned(to_sq(m)) != to)
            continue;
        if (wantPromo != NO_PIECE_TYPE) {
            if (type_of_move(m) != PROMOTION || promotion_type(m) != wantPromo)
                continue;
        } else if (type_of_move(m) == PROMOTION) {
            continue;
        }
        best = m;
        return true;
    }
    return false;
}

} // namespace Syzygy
