#include "eval.h"

#include "position.h"
#include "bitboard.h"
#include "nnue/nnue.h"

#include <algorithm>

namespace {

// ---- Tapered material (midgame / endgame) ----------------------------------
// Values and piece-square tables follow the well-known PeSTO set (Rofchade),
// indexed a8..h1 (index 0 = a8). White reads with square^56, Black with square.
constexpr int MgValue[7] = { 0, 82, 337, 365, 477, 1025, 0 };
constexpr int EgValue[7] = { 0, 94, 281, 297, 512,  936, 0 };
constexpr int PhaseInc[7] = { 0, 0, 1, 1, 2, 4, 0 };

constexpr int MgPawn[64] = {
    0,0,0,0,0,0,0,0,
    98,134,61,95,68,126,34,-11,
    -6,7,26,31,65,56,25,-20,
    -14,13,6,21,23,12,17,-23,
    -27,-2,-5,12,17,6,10,-25,
    -26,-4,-4,-10,3,3,33,-12,
    -35,-1,-20,-23,-15,24,38,-22,
    0,0,0,0,0,0,0,0 };
constexpr int EgPawn[64] = {
    0,0,0,0,0,0,0,0,
    178,173,158,134,147,132,165,187,
    94,100,85,67,56,53,82,84,
    32,24,13,5,-2,4,17,17,
    13,9,-3,-7,-7,-8,3,-1,
    4,7,-6,1,0,-5,-1,-8,
    13,8,8,10,13,0,2,-7,
    0,0,0,0,0,0,0,0 };
constexpr int MgKnight[64] = {
    -167,-89,-34,-49,61,-97,-15,-107,
    -73,-41,72,36,23,62,7,-17,
    -47,60,37,65,84,129,73,44,
    -9,17,19,53,37,69,18,22,
    -13,4,16,13,28,19,21,-8,
    -23,-9,12,10,19,17,25,-16,
    -29,-53,-12,-3,-1,18,-14,-19,
    -105,-21,-58,-33,-17,-28,-19,-23 };
constexpr int EgKnight[64] = {
    -58,-38,-13,-28,-31,-27,-63,-99,
    -25,-8,-25,-2,-9,-25,-24,-52,
    -24,-20,10,9,-1,-9,-19,-41,
    -17,3,22,22,22,11,8,-18,
    -18,-6,16,25,16,17,4,-18,
    -23,-3,-1,15,10,-3,-20,-22,
    -42,-20,-10,-5,-2,-20,-23,-44,
    -29,-51,-23,-15,-22,-18,-50,-64 };
constexpr int MgBishop[64] = {
    -29,4,-82,-37,-25,-42,7,-8,
    -26,16,-18,-13,30,59,18,-47,
    -16,37,43,40,35,50,37,-2,
    -4,5,19,50,37,37,7,-2,
    -6,13,13,26,34,12,10,4,
    0,15,15,15,14,27,18,10,
    4,15,16,0,7,21,33,1,
    -33,-3,-14,-21,-13,-12,-39,-21 };
constexpr int EgBishop[64] = {
    -14,-21,-11,-8,-7,-9,-17,-24,
    -8,-4,7,-12,-3,-13,-4,-14,
    2,-8,0,-1,-2,6,0,4,
    -3,9,12,9,14,10,3,2,
    -6,3,13,19,7,10,-3,-9,
    -12,-3,8,10,13,3,-7,-15,
    -14,-18,-7,-1,4,-9,-15,-27,
    -23,-9,-23,-5,-9,-16,-5,-17 };
constexpr int MgRook[64] = {
    32,42,32,51,63,9,31,43,
    27,32,58,62,80,67,26,44,
    -5,19,26,36,17,45,61,16,
    -24,-11,7,26,24,35,-8,-20,
    -36,-26,-12,-1,9,-7,6,-23,
    -45,-25,-16,-17,3,0,-5,-33,
    -44,-16,-20,-9,-1,11,-6,-71,
    -19,-13,1,17,16,7,-37,-26 };
constexpr int EgRook[64] = {
    13,10,18,15,12,12,8,5,
    11,13,13,11,-3,3,8,3,
    7,7,7,5,4,-3,-5,-3,
    4,3,13,1,2,1,-1,2,
    3,5,8,4,-5,-6,-8,-11,
    -4,0,-5,-1,-7,-12,-8,-16,
    -6,-6,0,2,-9,-9,-11,-3,
    -9,2,3,-1,-5,-13,4,-20 };
constexpr int MgQueen[64] = {
    -28,0,29,12,59,44,43,45,
    -24,-39,-5,1,-16,57,28,54,
    -13,-17,7,8,29,56,47,57,
    -27,-27,-16,-16,-1,17,-2,1,
    -9,-26,-9,-10,-2,-4,3,-3,
    -14,2,-11,-2,-5,2,14,5,
    -35,-8,11,2,8,15,-3,1,
    -1,-18,-9,10,-15,-25,-31,-50 };
constexpr int EgQueen[64] = {
    -9,22,22,27,27,19,10,20,
    -17,20,32,41,58,25,30,0,
    -20,6,9,49,47,35,19,9,
    3,22,24,45,57,40,57,36,
    -18,28,19,47,31,34,39,23,
    -16,-27,15,6,9,17,10,5,
    -22,-23,-30,-16,-16,-23,-36,-32,
    -33,-28,-22,-43,-5,-32,-20,-41 };
constexpr int MgKing[64] = {
    -65,23,16,-15,-56,-34,2,13,
    29,-1,-20,-7,-8,-4,-38,-29,
    -9,24,2,-16,-20,6,22,-22,
    -17,-20,-12,-27,-30,-25,-14,-36,
    -49,-1,-27,-39,-46,-44,-33,-51,
    -14,-14,-22,-46,-44,-30,-15,-27,
    1,7,-8,-64,-43,-16,9,8,
    -15,36,12,-54,8,-28,24,14 };
constexpr int EgKing[64] = {
    -74,-35,-18,-18,-11,15,4,-17,
    -12,17,14,17,17,38,23,11,
    10,17,23,15,20,45,44,13,
    -8,22,24,27,26,33,26,3,
    -18,-4,21,24,27,23,9,-11,
    -19,-3,11,21,23,16,7,-9,
    -27,-11,4,13,14,4,-5,-17,
    -53,-34,-21,-11,-28,-14,-24,-43 };

const int* MgPst[7] = { nullptr, MgPawn, MgKnight, MgBishop, MgRook, MgQueen, MgKing };
const int* EgPst[7] = { nullptr, EgPawn, EgKnight, EgBishop, EgRook, EgQueen, EgKing };

constexpr int Tempo = 10;

// ---- Positional term weights ----------------------------------------------
// Mobility bonus per attacked square in the mobility area, {mg, eg} per piece.
constexpr int MobMg[7] = { 0, 0, 4, 3, 2, 1, 0 };
constexpr int MobEg[7] = { 0, 0, 4, 3, 4, 2, 0 };
constexpr int MobCenter[7] = { 0, 0, 4, 6, 7, 13, 0 }; // center of the mobility ramp

// Passed pawn bonus indexed by relative rank (0..7).
constexpr int PassedMg[8] = { 0, 5, 10, 15, 30, 55, 90, 0 };
constexpr int PassedEg[8] = { 0, 8, 15, 30, 55, 90, 140, 0 };

constexpr int IsolatedMg = -12, IsolatedEg = -8;
constexpr int DoubledMg  = -8,  DoubledEg  = -22;
// Connected/phalanx pawn bonus by relative rank (eg = half).
constexpr int ConnectedBonus[8] = { 0, 4, 6, 9, 15, 28, 48, 0 };
constexpr int RookOpenMg = 26,  RookOpenEg = 10;
constexpr int RookSemiMg = 12,  RookSemiEg = 5;

// King-safety: per-attacker weight for pieces bearing on the enemy king ring.
constexpr int KingAttackWeight[7] = { 0, 0, 2, 2, 3, 5, 0 };

// Threats (attacker POV), tapered {mg, eg}.
constexpr int PawnThreatMg  = 45, PawnThreatEg  = 38; // pawn attacks a piece
constexpr int MinorThreatMg = 40, MinorThreatEg = 30; // minor attacks rook/queen
constexpr int RookThreatMg  = 40, RookThreatEg  = 20; // rook attacks queen
constexpr int HangingMg     = 25, HangingEg     = 30; // attacked & undefended piece

// Precomputed pawn-structure masks.
Bitboard PassedMask[COLOR_NB][SQUARE_NB];
Bitboard IsolatedMask[FILE_NB];
Bitboard ForwardFileMask[COLOR_NB][SQUARE_NB];

struct EvalTables {
    EvalTables() {
        for (File f = FILE_A; f <= FILE_H; ++f) {
            Bitboard adj = 0;
            if (f > FILE_A) adj |= file_bb(File(f - 1));
            if (f < FILE_H) adj |= file_bb(File(f + 1));
            IsolatedMask[f] = adj;
        }
        for (Square s = SQ_A1; s <= SQ_H8; ++s) {
            File f = file_of(s);
            Rank r = rank_of(s);
            Bitboard wFront = 0, bFront = 0;
            for (Rank rr = Rank(r + 1); rr <= RANK_8; rr = Rank(rr + 1)) wFront |= rank_bb(rr);
            for (Rank rr = RANK_1; rr < r; rr = Rank(rr + 1))           bFront |= rank_bb(rr);
            ForwardFileMask[WHITE][s] = file_bb(f) & wFront;
            ForwardFileMask[BLACK][s] = file_bb(f) & bFront;
            Bitboard span = file_bb(f) | IsolatedMask[f];
            PassedMask[WHITE][s] = span & wFront;
            PassedMask[BLACK][s] = span & bFront;
        }
    }
};
const EvalTables evalTables;

} // namespace

Value evaluate(const Position& pos) {
    if (NNUE::active())
        return NNUE::evaluate(pos);

    int mg[COLOR_NB] = { 0, 0 };
    int eg[COLOR_NB] = { 0, 0 };
    int phase = 0;

    const Bitboard occ = pos.pieces();
    const Bitboard pawns[COLOR_NB] = { pos.pieces(WHITE, PAWN), pos.pieces(BLACK, PAWN) };
    const Bitboard pawnAtt[COLOR_NB] = {
        pawn_attacks_bb<WHITE>(pawns[WHITE]),
        pawn_attacks_bb<BLACK>(pawns[BLACK])
    };

    // King safety: 3x3 ring around each king, and per-king accumulators for
    // enemy pieces bearing on it. Indexed by the *defending* (king-owning) color.
    const Square kingSq[COLOR_NB] = { pos.square<KING>(WHITE), pos.square<KING>(BLACK) };
    const Bitboard kingRing[COLOR_NB] = {
        BB::PseudoAttacks[KING][kingSq[WHITE]] | square_bb(kingSq[WHITE]),
        BB::PseudoAttacks[KING][kingSq[BLACK]] | square_bb(kingSq[BLACK])
    };
    int kingAttackers[COLOR_NB] = { 0, 0 };
    int kingAttackUnits[COLOR_NB] = { 0, 0 };

    // Per-color attack maps for threat detection (filled during the piece loop).
    Bitboard atkMinor[COLOR_NB] = { 0, 0 };
    Bitboard atkRook[COLOR_NB]  = { 0, 0 };
    Bitboard atkAll[COLOR_NB] = {
        pawnAtt[WHITE] | BB::PseudoAttacks[KING][kingSq[WHITE]],
        pawnAtt[BLACK] | BB::PseudoAttacks[KING][kingSq[BLACK]]
    };

    for (Color c : { WHITE, BLACK }) {
        Color them = ~c;
        // Mobility area: exclude own pawns/king and squares attacked by enemy pawns.
        Bitboard mobArea = ~(pos.pieces(c, PAWN) | pos.pieces(c, KING) | pawnAtt[them]);

        for (PieceType pt = PAWN; pt <= KING; ++pt) {
            Bitboard b = pos.pieces(c, pt);
            phase += PhaseInc[pt] * popcount(b);
            while (b) {
                Square s = pop_lsb(b);
                int idx = (c == WHITE) ? (s ^ 56) : s;
                mg[c] += MgValue[pt] + MgPst[pt][idx];
                eg[c] += EgValue[pt] + EgPst[pt][idx];

                if (pt >= KNIGHT && pt <= QUEEN) {
                    Bitboard att = attacks_bb(pt, s, occ);
                    int m = popcount(att & mobArea) - MobCenter[pt];
                    mg[c] += m * MobMg[pt];
                    eg[c] += m * MobEg[pt];

                    atkAll[c] |= att;
                    if (pt == KNIGHT || pt == BISHOP) atkMinor[c] |= att;
                    else if (pt == ROOK)              atkRook[c]  |= att;

                    // Pressure on the enemy king ring (them = the king owner).
                    Bitboard onRing = att & kingRing[them];
                    if (onRing) {
                        kingAttackers[them]++;
                        kingAttackUnits[them] += KingAttackWeight[pt] * popcount(onRing);
                    }
                }

                if (pt == PAWN) {
                    Rank rr = relative_rank(c, s);
                    if (!(pawns[them] & PassedMask[c][s])) {
                        mg[c] += PassedMg[rr];
                        // Blocked passer (enemy piece on the stop square) is worth
                        // less, especially in the endgame where it wants to run.
                        Square front = s + pawn_push(c);
                        int eb = PassedEg[rr];
                        if (square_ok(front) && (pos.pieces(them) & square_bb(front)))
                            eb = eb * 2 / 3;
                        eg[c] += eb;
                    }
                    if (!(pawns[c] & IsolatedMask[file_of(s)])) {
                        mg[c] += IsolatedMg;
                        eg[c] += IsolatedEg;
                    }
                    if (pawns[c] & ForwardFileMask[c][s]) { // another own pawn ahead = doubled
                        mg[c] += DoubledMg;
                        eg[c] += DoubledEg;
                    }
                    // Connected: defended by an own pawn, or a phalanx neighbor.
                    bool supported = pawnAtt[c] & square_bb(s);
                    bool phalanx   = pawns[c] & IsolatedMask[file_of(s)] & rank_bb(s);
                    if (supported || phalanx) {
                        mg[c] += ConnectedBonus[rr];
                        eg[c] += ConnectedBonus[rr] / 2;
                    }
                }

                if (pt == ROOK) {
                    Bitboard file = file_bb(s);
                    if (!(pawns[c] & file)) {
                        if (!(pawns[them] & file)) { mg[c] += RookOpenMg; eg[c] += RookOpenEg; }
                        else                       { mg[c] += RookSemiMg; eg[c] += RookSemiEg; }
                    }
                }
            }
        }
    }

    // Threats: reward attacking enemy pieces with cheaper attackers or leaving
    // them hanging (attacked and undefended). Attacker-POV bonuses.
    for (Color c : { WHITE, BLACK }) {
        Color them = ~c;
        Bitboard nonPawnEnemies = pos.pieces(them) & ~pawns[them] & ~pos.pieces(them, KING);

        Bitboard t = pawnAtt[c] & nonPawnEnemies;                 // pawn hits a piece
        int n = popcount(t);
        mg[c] += PawnThreatMg * n;  eg[c] += PawnThreatEg * n;

        t = atkMinor[c] & pos.pieces(them, ROOK, QUEEN);          // minor hits rook/queen
        n = popcount(t);
        mg[c] += MinorThreatMg * n; eg[c] += MinorThreatEg * n;

        t = atkRook[c] & pos.pieces(them, QUEEN);                 // rook hits queen
        n = popcount(t);
        mg[c] += RookThreatMg * n;  eg[c] += RookThreatEg * n;

        Bitboard weak = nonPawnEnemies & atkAll[c] & ~atkAll[them]; // hanging
        n = popcount(weak);
        mg[c] += HangingMg * n;     eg[c] += HangingEg * n;
    }

    // King safety: pawn-shelter penalty for open/half-open files near the king,
    // plus a quadratic king-danger term from the accumulated attacker pressure.
    for (Color kc : { WHITE, BLACK }) {
        int kf = file_of(kingSq[kc]);
        int shelter = 0;
        for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); ++f) {
            Bitboard fb = file_bb(File(f));
            if (!(pawns[kc] & fb))
                shelter += (pawns[~kc] & fb) ? 12 : 20; // half-open : fully open
        }

        int units = kingAttackers[kc] >= 2 ? kingAttackUnits[kc] : 0;
        if (!pos.count(~kc, QUEEN))
            units /= 2; // no enemy queen → attacks are far less potent
        int danger = std::min(units * units / 2, 600);

        mg[kc] -= shelter + danger;
        eg[kc] -= shelter / 2;
    }

    // Bishop pair.
    if (pos.count(WHITE, BISHOP) >= 2) { mg[WHITE] += 25; eg[WHITE] += 40; }
    if (pos.count(BLACK, BISHOP) >= 2) { mg[BLACK] += 25; eg[BLACK] += 40; }

    int mgScore = mg[WHITE] - mg[BLACK];
    int egScore = eg[WHITE] - eg[BLACK];

    int mgPhase = phase > 24 ? 24 : phase;
    int egPhase = 24 - mgPhase;
    int score = (mgScore * mgPhase + egScore * egPhase) / 24; // White POV

    Value v = pos.side_to_move() == WHITE ? score : -score;
    return v + Tempo;
}
