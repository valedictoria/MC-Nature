#include "search.h"

#include "position.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "see.h"
#include "uci.h"
#include "syzygy/syzygy.h"
#include "nnue/nnue.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace Search {

std::atomic<bool> stop{false};
TimeManager Time;
int64_t nodes = 0;

// ---- Runtime-tunable search parameters (SPSA) ------------------------------
// X-macro table: NAME, default, min, max. Every default reproduces the shipped
// behaviour exactly, so a build with all defaults keeps the bench signature.
// LmrDivX100 is the LMR log-divisor times 100 (210 -> 2.10).
// Defaults below are the SPSA-tuned values (D-round 2026-07-31); min/max unchanged.
#define TUNE_PARAMS(X) \
    X(RfpMargin,       76,   20,   200) \
    X(RfpMarginImp,    54,   20,   200) \
    X(NullBase,         5,    1,     6) \
    X(NullDepthDiv,     5,    3,    12) \
    X(NullEvalDiv,    162,   80,   400) \
    X(ProbCutMargin,  250,  120,   400) \
    X(ProbCutImp,      67,    0,   140) \
    X(FutBase,        158,   50,   350) \
    X(FutScale,        86,   40,   220) \
    X(HistPruneMul,  4134, 1500,  8000) \
    X(SeeCapMul,       64,   15,   130) \
    X(SeeQuietMul,     60,   15,   130) \
    X(LmrHistDiv,    7141, 3072, 16384) \
    X(LmrDivX100,     216,  160,   280) \
    X(AspDelta,        13,    6,    30) \
    X(HistBonusMul,     5,    2,     9) \
    X(HistBonusMax,  7051, 4000, 14000) \
    X(SingularMul,      2,    1,     5) \
    X(LmpBase,          4,    1,     7) \
    X(LmpImpBase,       4,    1,     9)

namespace Tune {
#define TUNE_DEFINE(name, def, lo, hi) int name = def;
TUNE_PARAMS(TUNE_DEFINE)
#undef TUNE_DEFINE
} // namespace Tune

namespace {

// ---- Per-ply stack ---------------------------------------------------------
struct Stack {
    Move  killers[2];
    Move  currentMove;
    Move  excludedMove;   // move to skip during a singular-extension verification
    Piece movedPiece;
    Value staticEval;
    int   ply;
};

// ---- Search-wide tables ----------------------------------------------------
int  mainHistory[COLOR_NB][SQUARE_NB][SQUARE_NB];
Move counterMoves[PIECE_NB][SQUARE_NB];
// Continuation history: [prevPiece][prevTo][movedPiece][to]. ~4 MB.
int  contHistory[PIECE_NB][SQUARE_NB][PIECE_NB][SQUARE_NB];
// Capture history: [movedPiece][to][capturedType].
int  captureHistory[PIECE_NB][SQUARE_NB][PIECE_TYPE_NB];
// Pawn-structure eval correction history: learns the (search - static) residual
// keyed by the pawn Zobrist key, de-biasing future static evals. [color][index].
constexpr int CorrHistSize        = 16384;   // power of two
constexpr int CorrHistGrain       = 256;     // fixed-point scale of stored entries
constexpr int CorrHistWeightScale = 256;     // running-average denominator
constexpr int CorrHistMax         = 256 * 32; // clamp on stored entry
int  pawnCorrHist[COLOR_NB][CorrHistSize];
int  Reductions[64][64];

// Proven-tablebase win magnitude: above any HCE eval, but below the mate band
// (VALUE_MATE_IN_MAX_PLY) so it never trips mate-distance logic or the
// depth>=6 mate early-exit.
constexpr Value VALUE_TB_WIN = VALUE_MATE_IN_MAX_PLY - MAX_PLY;

Move  rootBestMove = MOVE_NONE;
Value rootBestValue = -VALUE_INFINITE;

Move  pvTable[MAX_PLY + 1][MAX_PLY + 1];
int   pvLen[MAX_PLY + 1];

Limits limits;
Color  rootColor;

// Fill the LMR reduction table from the tunable log-divisor. Recomputed on init
// and whenever LmrDivX100 changes via setoption.
void compute_reductions() {
    double div = Tune::LmrDivX100 / 100.0;
    for (int d = 1; d < 64; ++d)
        for (int m = 1; m < 64; ++m)
            Reductions[d][m] = int(0.5 + std::log(d) * std::log(m) / div);
}

// ---- Helpers ---------------------------------------------------------------
Value score_to_tt(Value v, int ply) {
    return v >= VALUE_MATE_IN_MAX_PLY ? Value(v + ply)
         : v <= VALUE_MATED_IN_MAX_PLY ? Value(v - ply) : v;
}
Value score_from_tt(Value v, int ply) {
    if (v == VALUE_NONE) return VALUE_NONE;
    return v >= VALUE_MATE_IN_MAX_PLY ? Value(v - ply)
         : v <= VALUE_MATED_IN_MAX_PLY ? Value(v + ply) : v;
}

bool has_non_pawn_material(const Position& pos, Color c) {
    return pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP)
         | pos.pieces(c, ROOK)   | pos.pieces(c, QUEEN);
}

void update_pv(int ply, Move m) {
    pvTable[ply][0] = m;
    std::memcpy(&pvTable[ply][1], &pvTable[ply + 1][0], pvLen[ply + 1] * sizeof(Move));
    pvLen[ply] = pvLen[ply + 1] + 1;
}

void check_time() {
    if (limits.infinite)
        return;
    if ((nodes & 2047) == 0) {
        if (limits.useTimeManager || limits.movetime) {
            if (Time.elapsed() >= Time.hard_ms())
                stop.store(true, std::memory_order_relaxed);
        }
        if (limits.nodes && nodes >= limits.nodes)
            stop.store(true, std::memory_order_relaxed);
    }
}

// ---- Move ordering ---------------------------------------------------------
int quiet_history(Color us, Stack* ss, Piece movedPiece, Move m); // forward decl

void score_moves(const Position& pos, ExtMove* begin, ExtMove* end, Move ttMove, Stack* ss) {
    Color us = pos.side_to_move();
    for (ExtMove* it = begin; it != end; ++it) {
        Move m = it->move;
        if (m == ttMove) { it->value = 1 << 30; continue; }

        if (pos.capture(m) || type_of_move(m) == PROMOTION) {
            PieceType captured = type_of(pos.piece_on(to_sq(m)));
            PieceType attacker = type_of(pos.moved_piece(m));
            int mvvlva = PieceValue[captured] * 16 - PieceValue[attacker];
            int capHist = captureHistory[pos.moved_piece(m)][to_sq(m)][captured];
            it->value = (see_ge(pos, m, 0) ? (1 << 28) : -(1 << 28)) + mvvlva + capHist;
        } else if (m == ss->killers[0]) {
            it->value = (1 << 27) + 2;
        } else if (m == ss->killers[1]) {
            it->value = (1 << 27) + 1;
        } else if (m == counterMoves[pos.moved_piece(m)][to_sq(m)]) {
            it->value = (1 << 27);
        } else {
            it->value = quiet_history(us, ss, pos.moved_piece(m), m);
        }
    }
}

ExtMove* pick_best(ExtMove* begin, ExtMove* end) {
    ExtMove* best = begin;
    for (ExtMove* it = begin + 1; it != end; ++it)
        if (it->value > best->value)
            best = it;
    std::swap(*begin, *best);
    return begin;
}

void update_history(const Position& pos, Move m, int bonus) {
    Color us = pos.side_to_move();
    int& h = mainHistory[us][from_sq(m)][to_sq(m)];
    bonus = std::clamp(bonus, -8000, 8000);
    h += bonus - h * std::abs(bonus) / 8192;
}

void update_capture_history(const Position& pos, Move m, int bonus) {
    PieceType captured = type_of(pos.piece_on(to_sq(m)));
    int& h = captureHistory[pos.moved_piece(m)][to_sq(m)][captured];
    bonus = std::clamp(bonus, -8000, 8000);
    h += bonus - h * std::abs(bonus) / 8192;
}

// Combined quiet-move history: butterfly + 1-ply and 2-ply continuation.
int quiet_history(Color us, Stack* ss, Piece movedPiece, Move m) {
    int h = mainHistory[us][from_sq(m)][to_sq(m)];
    for (int back : { 1, 2 }) {
        Move pm = (ss - back)->currentMove;
        if (pm != MOVE_NONE && pm != MOVE_NULL)
            h += contHistory[(ss - back)->movedPiece][to_sq(pm)][movedPiece][to_sq(m)];
    }
    return h;
}

void update_continuation(Stack* ss, Piece movedPiece, Move m, int bonus) {
    bonus = std::clamp(bonus, -8000, 8000);
    for (int back : { 1, 2 }) {
        Move pm = (ss - back)->currentMove;
        if (pm != MOVE_NONE && pm != MOVE_NULL) {
            int& e = contHistory[(ss - back)->movedPiece][to_sq(pm)][movedPiece][to_sq(m)];
            e += bonus - e * std::abs(bonus) / 8192;
        }
    }
}

// ---- Eval correction history -----------------------------------------------
// Apply the learned pawn-structure residual to a raw static eval.
Value adjust_eval(const Position& pos, Value staticEval) {
    int delta = pawnCorrHist[pos.side_to_move()][pos.pawn_key() & (CorrHistSize - 1)] / CorrHistGrain;
    Value v = staticEval + delta;
    return std::clamp(v, Value(VALUE_MATED_IN_MAX_PLY + 1), Value(VALUE_MATE_IN_MAX_PLY - 1));
}

// Blend the residual (searchScore - correctedStatic) into the running average.
void update_pawn_corr(const Position& pos, int depth, int diff) {
    int& entry = pawnCorrHist[pos.side_to_move()][pos.pawn_key() & (CorrHistSize - 1)];
    int scaledDiff = diff * CorrHistGrain;
    int weight = std::min(depth + 1, 16);
    entry = (entry * (CorrHistWeightScale - weight) + scaledDiff * weight) / CorrHistWeightScale;
    entry = std::clamp(entry, -CorrHistMax, CorrHistMax);
}

// ---- Quiescence search -----------------------------------------------------
Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {
    ++nodes;
    check_time();

    int ply = ss->ply;
    pvLen[ply] = 0;

    if (pos.is_draw(ply))
        return VALUE_DRAW;
    if (ply >= MAX_PLY)
        return pos.checkers() ? VALUE_DRAW : evaluate(pos);

    bool inCheck = pos.checkers();

    bool ttHit = false;
    TTEntry* tte = TT.probe(pos.key(), ttHit);
    Value ttValue = ttHit ? score_from_tt(tte->value(), ply) : VALUE_NONE;
    Move  ttMove  = ttHit ? tte->move() : MOVE_NONE;
    if (ttHit && tte->depth() >= 0 && ttValue != VALUE_NONE) {
        Bound b = tte->bound();
        if (b == BOUND_EXACT
            || (b == BOUND_LOWER && ttValue >= beta)
            || (b == BOUND_UPPER && ttValue <= alpha))
            return ttValue;
    }

    Value bestValue, rawEval;
    if (inCheck) {
        bestValue = -VALUE_INFINITE;
        rawEval   = VALUE_NONE;
    } else {
        rawEval   = evaluate(pos);
        bestValue = adjust_eval(pos, rawEval); // correction-history-adjusted stand-pat
        if (bestValue >= beta)
            return bestValue;
        if (bestValue > alpha)
            alpha = bestValue;
    }

    ExtMove moveBuf[MAX_MOVES];
    ExtMove* end = generate_tactical(pos, moveBuf);
    score_moves(pos, moveBuf, end, ttMove, ss);

    Move bestMove = MOVE_NONE;
    StateInfo st;
    for (ExtMove* it = moveBuf; it != end; ++it) {
        ExtMove* cur = pick_best(it, end);
        Move m = cur->move;

        // Delta / SEE pruning when not in check.
        if (!inCheck) {
            if (!see_ge(pos, m, -50))
                continue;
        }

        (ss)->currentMove = m;
        (ss)->movedPiece = pos.moved_piece(m);
        pos.do_move(m, st);
        Value v = -qsearch(pos, ss + 1, -beta, -alpha);
        pos.undo_move(m);

        if (stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (v > bestValue) {
            bestValue = v;
            bestMove = m;
            if (v > alpha) {
                alpha = v;
                update_pv(ply, m);
                if (v >= beta)
                    break;
            }
        }
    }

    if (inCheck && bestValue == -VALUE_INFINITE)
        return mated_in(ply); // checkmate

    Bound b = bestValue >= beta ? BOUND_LOWER : BOUND_UPPER;
    tte->save(pos.key(), score_to_tt(bestValue, ply), b, 0, bestMove,
              rawEval, TT.generation());
    return bestValue;
}

// ---- Main alpha-beta -------------------------------------------------------
Value alphabeta(Position& pos, Stack* ss, Value alpha, Value beta, int depth, bool cutNode) {
    const bool pvNode = beta - alpha > 1;
    const bool rootNode = ss->ply == 0;
    const int  ply = ss->ply;

    if (depth <= 0)
        return qsearch(pos, ss, alpha, beta);

    ++nodes;
    check_time();
    pvLen[ply] = 0;

    if (stop.load(std::memory_order_relaxed))
        return VALUE_ZERO;

    if (!rootNode) {
        if (pos.is_draw(ply))
            return VALUE_DRAW;
        if (ply >= MAX_PLY)
            return pos.checkers() ? VALUE_DRAW : evaluate(pos);
        // Mate-distance pruning.
        alpha = std::max(alpha, mated_in(ply));
        beta  = std::min(beta, mate_in(ply + 1));
        if (alpha >= beta)
            return alpha;
    }

    const bool inCheck = pos.checkers();
    const Color us = pos.side_to_move();
    const Move  excludedMove = ss->excludedMove;

    // TT probe.
    bool ttHit = false;
    TTEntry* tte = TT.probe(pos.key(), ttHit);
    Value ttValue = ttHit ? score_from_tt(tte->value(), ply) : VALUE_NONE;
    Move  ttMove  = rootNode ? rootBestMove : (ttHit ? tte->move() : MOVE_NONE);

    // During a singular verification search we must not take the TT cutoff for
    // this key (it would just return ttValue and defeat the probe).
    if (!excludedMove && !pvNode && ttHit && tte->depth() >= depth && ttValue != VALUE_NONE) {
        Bound b = tte->bound();
        if (b == BOUND_EXACT
            || (b == BOUND_LOWER && ttValue >= beta)
            || (b == BOUND_UPPER && ttValue <= alpha))
            return ttValue;
    }

    // ---- Syzygy tablebase probe (WDL) ----
    // A hit is a proven result: return it as a cutoff. Fathom requires an empty
    // 50-move counter and no castling rights, so gate on those.
    if (!rootNode && !excludedMove && Syzygy::active()
        && pos.rule50_count() == 0
        && !pos.can_castle(ANY_CASTLING)
        && popcount(pos.pieces()) <= Syzygy::MaxPieces) {
        int wdl;
        if (Syzygy::probe_wdl(pos, wdl)) {
            Value tbValue = wdl > 0 ? Value(VALUE_TB_WIN - ply)
                          : wdl < 0 ? Value(-VALUE_TB_WIN + ply)
                                    : VALUE_DRAW;
            Bound tbBound = wdl > 0 ? BOUND_LOWER : wdl < 0 ? BOUND_UPPER : BOUND_EXACT;
            if (tbBound == BOUND_EXACT
                || (tbBound == BOUND_LOWER && tbValue >= beta)
                || (tbBound == BOUND_UPPER && tbValue <= alpha)) {
                tte->save(pos.key(), score_to_tt(tbValue, ply), tbBound,
                          std::min(depth + 6, MAX_PLY - 1), MOVE_NONE,
                          VALUE_NONE, TT.generation());
                return tbValue;
            }
        }
    }

    // Static eval. `unadjustedEval` is the raw HCE value (cached in TT);
    // `staticEval` is the correction-history-adjusted value used for pruning.
    Value unadjustedEval = inCheck ? VALUE_NONE
                     : (ttHit && tte->eval() != VALUE_NONE) ? tte->eval()
                     : evaluate(pos);
    Value staticEval = inCheck ? VALUE_NONE : adjust_eval(pos, unadjustedEval);
    ss->staticEval = staticEval;

    bool improving = !inCheck && ply >= 2 && (ss - 2)->staticEval != VALUE_NONE
                   && staticEval > (ss - 2)->staticEval;

    // ---- Whole-node pruning (non-PV, not in check) ----
    if (!pvNode && !inCheck) {
        // Reverse futility pruning.
        if (depth <= 8 && staticEval < VALUE_MATE_IN_MAX_PLY) {
            int margin = (improving ? Tune::RfpMarginImp : Tune::RfpMargin) * depth;
            if (staticEval - margin >= beta)
                return staticEval;
        }

        // Null-move pruning.
        if (depth >= 3 && staticEval >= beta
            && (ss - 1)->currentMove != MOVE_NULL
            && has_non_pawn_material(pos, pos.side_to_move())) {
            int R = Tune::NullBase + depth / Tune::NullDepthDiv
                  + std::min(3, (staticEval - beta) / Tune::NullEvalDiv);
            ss->currentMove = MOVE_NULL;
            ss->movedPiece = NO_PIECE;
            StateInfo st;
            pos.do_null_move(st);
            Value v = -alphabeta(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
            pos.undo_null_move();
            if (v >= beta)
                return v < VALUE_MATE_IN_MAX_PLY ? v : beta;
        }

        // ProbCut: if a good capture survives a reduced verification search above a
        // raised beta, the node almost certainly fails high — prune it.
        Value probCutBeta = beta + Tune::ProbCutMargin - Tune::ProbCutImp * improving;
        if (depth >= 5
            && std::abs(beta) < VALUE_MATE_IN_MAX_PLY
            && !(ttHit && tte->depth() >= depth - 3
                 && ttValue != VALUE_NONE && ttValue < probCutBeta)) {
            ExtMove pcBuf[MAX_MOVES];
            ExtMove* pcEnd = generate_tactical(pos, pcBuf);
            score_moves(pos, pcBuf, pcEnd, ttMove, ss);
            StateInfo pcSt;
            for (ExtMove* it = pcBuf; it != pcEnd; ++it) {
                pick_best(it, pcEnd);
                Move m = it->move;
                if (m == excludedMove)
                    continue;
                // Only captures whose static exchange already reaches the raised beta.
                if (!see_ge(pos, m, probCutBeta - staticEval))
                    continue;

                ss->currentMove = m;
                ss->movedPiece = pos.moved_piece(m);
                pos.do_move(m, pcSt);
                // Cheap qsearch screen, then a depth-4 reduced confirmation search.
                Value v = -qsearch(pos, ss + 1, -probCutBeta, -probCutBeta + 1);
                if (v >= probCutBeta)
                    v = -alphabeta(pos, ss + 1, -probCutBeta, -probCutBeta + 1, depth - 4, !cutNode);
                pos.undo_move(m);

                if (v >= probCutBeta) {
                    tte->save(pos.key(), score_to_tt(v, ply), BOUND_LOWER, depth - 3, m,
                              unadjustedEval, TT.generation());
                    return v;
                }
            }
        }
    }

    // Internal iterative reduction.
    if (depth >= 4 && ttMove == MOVE_NONE)
        --depth;

    // ---- Move loop ----
    ExtMove moveBuf[MAX_MOVES];
    ExtMove* end = generate_legal(pos, moveBuf);
    score_moves(pos, moveBuf, end, ttMove, ss);

    Value bestValue = -VALUE_INFINITE;
    Move  bestMove = MOVE_NONE;
    int   moveCount = 0;
    Move  quietsSearched[64];
    int   quietCount = 0;
    Move  capturesSearched[32];
    int   captureCount = 0;
    StateInfo st;

    for (ExtMove* it = moveBuf; it != end; ++it) {
        pick_best(it, end);
        Move m = it->move;
        if (m == excludedMove)
            continue;
        ++moveCount;

        bool capture = pos.capture(m) || type_of_move(m) == PROMOTION;
        bool givesCheck = pos.gives_check(m);
        bool quiet = !capture;

        Piece movedPiece = pos.moved_piece(m);
        int histScore = quiet ? quiet_history(us, ss, movedPiece, m) : 0;

        // Late move / SEE / history pruning at shallow depth (non-PV).
        if (!rootNode && !pvNode && !inCheck && bestValue > VALUE_MATED_IN_MAX_PLY) {
            if (quiet && !givesCheck) {
                int lmpLimit = improving ? (Tune::LmpImpBase + 2 * depth * depth)
                                         : (Tune::LmpBase + depth * depth);
                if (moveCount >= lmpLimit)
                    continue;
                if (depth <= 6 && staticEval + Tune::FutBase + Tune::FutScale * depth <= alpha)
                    continue;
                if (depth <= 4 && moveCount > 1 && histScore < -Tune::HistPruneMul * depth)
                    continue;
            }
            if (depth <= 8 && !see_ge(pos, m, capture ? -Tune::SeeCapMul * depth
                                                       : -Tune::SeeQuietMul * depth * depth))
                continue;
        }

        // ---- Extensions ----
        int extension = 0;

        // Singular extension: if the TT move is much better than all alternatives
        // (a reduced-depth search excluding it fails below a margin under ttValue),
        // extend it. If that same search beats beta, the node multi-cuts.
        if (!rootNode
            && depth >= 8
            && m == ttMove
            && !excludedMove
            && ttHit
            && tte->depth() >= depth - 3
            && (tte->bound() & BOUND_LOWER)
            && std::abs(ttValue) < VALUE_MATE_IN_MAX_PLY) {
            Value singularBeta  = ttValue - Tune::SingularMul * depth;
            int   singularDepth = (depth - 1) / 2;
            ss->excludedMove = m;
            Value v = alphabeta(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
            ss->excludedMove = MOVE_NONE;

            if (v < singularBeta) {
                extension = 1;
                if (!pvNode && v < singularBeta - 24) // clearly singular → double-extend
                    extension = 2;
            } else if (singularBeta >= beta) {
                return singularBeta;                  // multi-cut
            } else if (ttValue >= beta) {
                extension = -1;                       // negative extension
            }
        }

        // Check extension (only when not already extended for singularity).
        if (!extension && givesCheck && ply + depth < MAX_PLY - 1)
            extension = 1;

        ss->currentMove = m;
        ss->movedPiece = movedPiece;
        pos.do_move(m, st);

        int newDepth = depth - 1 + extension;
        Value v;

        // Late move reductions.
        bool doFullSearch;
        if (depth >= 3 && moveCount >= 2 && quiet && !givesCheck) {
            int r = Reductions[std::min(63, depth)][std::min(63, moveCount)];
            if (pvNode) --r;
            if (cutNode) ++r;
            if (!improving) ++r;
            r -= std::clamp(histScore / Tune::LmrHistDiv, -2, 2); // strong history reduces less
            int d = std::clamp(newDepth - r, 1, newDepth);
            v = -alphabeta(pos, ss + 1, -alpha - 1, -alpha, d, true);
            doFullSearch = v > alpha && d < newDepth;
        } else {
            doFullSearch = !pvNode || moveCount > 1;
        }

        if (doFullSearch)
            v = -alphabeta(pos, ss + 1, -alpha - 1, -alpha, newDepth, !cutNode);

        if (pvNode && (moveCount == 1 || (v > alpha && (rootNode || v < beta))))
            v = -alphabeta(pos, ss + 1, -beta, -alpha, newDepth, false);

        pos.undo_move(m);

        if (stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (v > bestValue) {
            bestValue = v;
            if (v > alpha) {
                bestMove = m;
                if (rootNode) {
                    rootBestMove = m;
                    rootBestValue = v;
                }
                if (pvNode)
                    update_pv(ply, m);
                if (v >= beta)
                    break;
                alpha = v;
            }
        }

        if (quiet && quietCount < 64)
            quietsSearched[quietCount++] = m;
        else if (!quiet && captureCount < 32)
            capturesSearched[captureCount++] = m;
    }

    // Checkmate / stalemate. In a singular search moveCount==0 means the tt move
    // was the only move: report a fail-low so the caller extends it.
    if (moveCount == 0)
        return excludedMove ? alpha : (inCheck ? mated_in(ply) : VALUE_DRAW);

    // History updates on a beta cutoff. Quiet best moves feed the quiet
    // histories/killers/counter; capture best moves feed capture history.
    // Either way, searched captures that weren't best get a malus.
    if (bestMove != MOVE_NONE) {
        int bonus = std::min(depth * depth * Tune::HistBonusMul, Tune::HistBonusMax);
        bool bestIsCapture = pos.capture(bestMove) || type_of_move(bestMove) == PROMOTION;

        if (bestIsCapture) {
            update_capture_history(pos, bestMove, bonus);
        } else {
            Piece bestPiece = pos.moved_piece(bestMove);
            update_history(pos, bestMove, bonus);
            update_continuation(ss, bestPiece, bestMove, bonus);
            for (int i = 0; i < quietCount; ++i)
                if (quietsSearched[i] != bestMove) {
                    update_history(pos, quietsSearched[i], -bonus);
                    update_continuation(ss, pos.moved_piece(quietsSearched[i]), quietsSearched[i], -bonus);
                }

            if (ss->killers[0] != bestMove) {
                ss->killers[1] = ss->killers[0];
                ss->killers[0] = bestMove;
            }
            if ((ss - 1)->currentMove != MOVE_NONE && (ss - 1)->currentMove != MOVE_NULL)
                counterMoves[(ss - 1)->movedPiece][to_sq((ss - 1)->currentMove)] = bestMove;
        }

        for (int i = 0; i < captureCount; ++i)
            if (capturesSearched[i] != bestMove)
                update_capture_history(pos, capturesSearched[i], -bonus);
    }

    // A singular verification search is a partial search of this node (one move
    // excluded); its result must not update correction history or the TT.
    if (excludedMove)
        return bestValue;

    Bound b = bestValue >= beta ? BOUND_LOWER
            : (pvNode && bestMove != MOVE_NONE) ? BOUND_EXACT : BOUND_UPPER;

    // Eval correction history: learn the (search - static) residual for this pawn
    // structure, unless the result is tactical (best move is a capture/promo), a
    // mate score, or a bound in the direction that carries no eval information.
    if (!inCheck
        && (bestMove == MOVE_NONE
            || !(pos.capture(bestMove) || type_of_move(bestMove) == PROMOTION))
        && std::abs(bestValue) < VALUE_MATE_IN_MAX_PLY
        && !(b == BOUND_LOWER && bestValue <= staticEval)
        && !(b == BOUND_UPPER && bestValue >= staticEval))
        update_pawn_corr(pos, depth, bestValue - staticEval);

    tte->save(pos.key(), score_to_tt(bestValue, ply), b, depth, bestMove,
              inCheck ? VALUE_NONE : unadjustedEval, TT.generation());

    return bestValue;
}

// ---- Info printing ---------------------------------------------------------
void print_info(int depth, Value score, int64_t elapsed) {
    std::cout << "info depth " << depth << " score ";
    if (is_mate_score(score)) {
        int mate = score > 0 ? (VALUE_MATE - score + 1) / 2 : -(VALUE_MATE + score) / 2;
        std::cout << "mate " << mate;
    } else {
        std::cout << "cp " << int(score);
    }
    int64_t nps = elapsed > 0 ? nodes * 1000 / elapsed : 0;
    std::cout << " nodes " << nodes << " nps " << nps << " time " << elapsed << " pv";
    for (int i = 0; i < pvLen[0]; ++i) {
        char buf[6];
        move_to_uci(pvTable[0][i], buf);
        std::cout << ' ' << buf;
    }
    std::cout << std::endl;
}

} // namespace

// ---- Public interface ------------------------------------------------------
void init() {
    compute_reductions();
}

bool set_tune(const std::string& name, int value) {
    bool ok = false;
#define TUNE_SET(pname, def, lo, hi) if (name == #pname) { Tune::pname = value; ok = true; }
    TUNE_PARAMS(TUNE_SET)
#undef TUNE_SET
    if (ok)
        compute_reductions(); // cheap; only LmrDivX100 actually feeds the table
    return ok;
}

void print_tune_options(std::ostream& os) {
#define TUNE_OPT(pname, def, lo, hi) \
    os << "option name " #pname " type spin default " << (def) \
       << " min " << (lo) << " max " << (hi) << "\n";
    TUNE_PARAMS(TUNE_OPT)
#undef TUNE_OPT
}

void clear() {
    std::memset(mainHistory, 0, sizeof(mainHistory));
    std::memset(counterMoves, 0, sizeof(counterMoves));
    std::memset(contHistory, 0, sizeof(contHistory));
    std::memset(captureHistory, 0, sizeof(captureHistory));
    std::memset(pawnCorrHist, 0, sizeof(pawnCorrHist));
    TT.new_search();
}

void think(Position& pos, const Limits& lim) {
    limits = lim;
    nodes = 0;
    stop.store(false, std::memory_order_relaxed);
    rootColor = pos.side_to_move();
    rootBestMove = MOVE_NONE;
    rootBestValue = -VALUE_INFINITE;

    Time.init(limits, rootColor);
    Time.start();
    TT.new_search();

    // History is intentionally kept across moves within a game (reset only on
    // ucinewgame via clear()), which is worth a little strength.

    // Syzygy root probe: in a tablebase position, play the DTZ-optimal move
    // directly (50-move aware, so no rule50 gate). Off unless SyzygyPath loaded.
    if (Syzygy::active()
        && !pos.can_castle(ANY_CASTLING)
        && popcount(pos.pieces()) <= Syzygy::MaxPieces) {
        Move tbMove; int wdl;
        if (Syzygy::probe_root(pos, tbMove, wdl)) {
            rootBestMove = tbMove;
            Value score = wdl > 0 ? VALUE_TB_WIN : wdl < 0 ? -VALUE_TB_WIN : VALUE_DRAW;
            pvTable[0][0] = tbMove;
            pvLen[0] = 1;
            print_info(1, score, Time.elapsed());
            char buf[6];
            move_to_uci(tbMove, buf);
            std::cout << "bestmove " << buf << std::endl;
            return;
        }
    }

    // Enable incremental NNUE and seed the accumulator stack from the root
    // position. Every do_move/undo_move below keeps it in sync.
    NNUE::set_active(true);
    NNUE::refresh(pos);

    Stack stackMem[MAX_PLY + 4];
    std::memset(stackMem, 0, sizeof(stackMem));
    Stack* ss = stackMem + 2; // room for (ss-2)
    for (int i = -2; i <= MAX_PLY; ++i)
        (ss + i)->ply = i;
    for (int i = 0; i < 2; ++i) {
        (ss - 1 - i)->staticEval = VALUE_NONE;
        (ss - 1 - i)->currentMove = MOVE_NONE;
    }

    Value alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
    Value prevScore = -VALUE_INFINITE;
    int maxDepth = limits.depth ? limits.depth : MAX_PLY - 1;

    // Best-move stability for time management: how many consecutive iterations the
    // root best move has held. Higher stability -> stop sooner; a fresh flip -> search longer.
    Move previousBest = MOVE_NONE;
    int  bestStability = 0;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        // Aspiration windows.
        int delta = Tune::AspDelta;
        if (depth >= 5) {
            alpha = std::max<int>(prevScore - delta, -VALUE_INFINITE);
            beta  = std::min<int>(prevScore + delta,  VALUE_INFINITE);
        }

        Value score;
        while (true) {
            score = alphabeta(pos, ss, alpha, beta, depth, false);
            if (stop.load(std::memory_order_relaxed))
                break;
            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max<int>(score - delta, -VALUE_INFINITE);
            } else if (score >= beta) {
                beta = std::min<int>(score + delta, VALUE_INFINITE);
            } else {
                break;
            }
            delta += delta / 3;
        }

        if (stop.load(std::memory_order_relaxed) && depth > 1)
            break;

        prevScore = score;
        print_info(depth, score, Time.elapsed());

        // Track root-best-move stability across iterations.
        if (rootBestMove == previousBest)
            bestStability = std::min(bestStability + 1, 8);
        else
            bestStability = 0;
        previousBest = rootBestMove;

        // Soft-time stop between iterations. Fixed movetime uses its full
        // budget; a game clock stops at half the per-move target (the next
        // iteration typically costs about as much as everything so far),
        // scaled by best-move stability: a flip-flopping PV searches longer,
        // a settled PV bails sooner.
        if ((limits.useTimeManager || limits.movetime) && !limits.infinite) {
            int64_t softLimit;
            if (limits.movetime) {
                softLimit = Time.soft_ms();
            } else {
                static const double stabilityScale[9] =
                    {1.55, 1.35, 1.20, 1.10, 1.00, 0.92, 0.86, 0.82, 0.80};
                softLimit = int64_t(Time.soft_ms() * stabilityScale[bestStability] / 2.0);
                softLimit = std::min(softLimit, Time.hard_ms());
            }
            if (Time.elapsed() >= softLimit)
                break;
        }
        if (is_mate_score(score) && depth >= 6)
            break;
    }

    NNUE::set_active(false); // leave incremental mode; non-search do_move stays cheap

    char buf[6];
    move_to_uci(rootBestMove != MOVE_NONE ? rootBestMove : MOVE_NONE, buf);
    std::cout << "bestmove " << buf << std::endl;
}

} // namespace Search
