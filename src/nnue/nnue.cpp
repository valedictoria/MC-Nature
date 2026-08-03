#include "nnue.h"

#include "../position.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

// NEON is the AArch64 baseline (the Makefile targets -mcpu=apple-m1), so it's
// always available on the actual deployment target; guarded so a non-ARM
// build still compiles via the scalar fallback below.
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#define CC_HAVE_NEON 1
#endif

// Default net embedded via incbin.s.
extern "C" const unsigned char CC_nnue_net[];
extern "C" const unsigned char CC_nnue_net_end[];

namespace NNUE {

namespace {

// ---- Architecture constants (must match the trained net exactly) ----
constexpr int INPUT       = 768;
constexpr int HIDDEN      = 1024;
constexpr int NUM_BUCKETS = 4;
constexpr int QA          = 255;
constexpr int QB          = 64;
constexpr int QAB         = QA * QB;
constexpr int SCALE       = 400;

// akimbo king-square -> input-bucket map. Only files a-d (0..3) are ever indexed
// (the caller horizontally mirrors kings on files e-h first), so only the
// low-file entries {0,1,2,3} are used.
constexpr int BUCKETS[64] = {
    0, 0, 1, 1, 5, 5, 4, 4,
    2, 2, 2, 2, 6, 6, 6, 6,
    3, 3, 3, 3, 7, 7, 7, 7,
    3, 3, 3, 3, 7, 7, 7, 7,
    3, 3, 3, 3, 7, 7, 7, 7,
    3, 3, 3, 3, 7, 7, 7, 7,
    3, 3, 3, 3, 7, 7, 7, 7,
    3, 3, 3, 3, 7, 7, 7, 7,
};

// akimbo SEE values (index by MC-Nature PieceType) for the material scale.
constexpr int SeeVal[PIECE_TYPE_NB] = { 0, 100, 450, 450, 650, 1250, 0, 0 };

// ---- Loaded network (views into embedded bytes or a heap buffer) ----
using Row = int16_t[HIDDEN];
const Row* featureWeights = nullptr;      // [INPUT * NUM_BUCKETS][HIDDEN]
const int16_t* featureBias = nullptr;     // [HIDDEN]
const Row* outputWeights = nullptr;       // [2][HIDDEN]
int16_t outputBias = 0;
bool loaded = false;

std::vector<unsigned char> fileBuf;       // backing store for an EvalFile override

constexpr size_t NET_BYTES =
    size_t(INPUT * NUM_BUCKETS) * HIDDEN * sizeof(int16_t)  // feature_weights
    + size_t(HIDDEN) * sizeof(int16_t)                      // feature_bias
    + size_t(2) * HIDDEN * sizeof(int16_t)                  // output_weights
    + sizeof(int16_t);                                      // output_bias

void map_net(const unsigned char* base) {
    const int16_t* p = reinterpret_cast<const int16_t*>(base);
    featureWeights = reinterpret_cast<const Row*>(p);
    p += size_t(INPUT * NUM_BUCKETS) * HIDDEN;
    featureBias = p;
    p += HIDDEN;
    outputWeights = reinterpret_cast<const Row*>(p);
    p += size_t(2) * HIDDEN;
    outputBias = *p;
    loaded = true;
}

// SCReLU flatten: sum_i clamp(acc[i],0,QA)^2 * weight[i].
//
// v = clamp(acc[i],0,QA) is in [0,255] so v*v <= 65025 fits exactly in int32;
// weight[i] is int16 (|w| <= 32767) so v*v*weight[i] <= 65025*32767 <
// INT32_MAX, so that product also fits exactly in int32. Both facts are
// architectural (QA and the int16 weight type are fixed constants), not
// dependent on the specific trained weights. That means the NEON path below
// can do the v*v and v*v*weight multiplies as 32-bit vector ops and only
// widen to int64 for the final sum — every intermediate value is exactly
// representable, so this is bit-identical to the scalar reference (integer
// addition is associative/commutative, so reordering the sum across lanes
// changes nothing).
#ifdef CC_HAVE_NEON
int64_t flatten(const int16_t* acc, const int16_t* weight) {
    const int16x8_t zero = vdupq_n_s16(0);
    const int16x8_t qa   = vdupq_n_s16(QA);
    int64x2_t sumLo = vdupq_n_s64(0);
    int64x2_t sumHi = vdupq_n_s64(0);

    for (int i = 0; i < HIDDEN; i += 8) {
        int16x8_t v = vminq_s16(vmaxq_s16(vld1q_s16(&acc[i]), zero), qa);
        int16x8_t w = vld1q_s16(&weight[i]);

        int32x4_t vLo = vmovl_s16(vget_low_s16(v));
        int32x4_t vHi = vmovl_s16(vget_high_s16(v));
        int32x4_t wLo = vmovl_s16(vget_low_s16(w));
        int32x4_t wHi = vmovl_s16(vget_high_s16(w));

        int32x4_t termLo = vmulq_s32(vmulq_s32(vLo, vLo), wLo); // v*v*w, fits int32
        int32x4_t termHi = vmulq_s32(vmulq_s32(vHi, vHi), wHi);

        sumLo = vaddq_s64(sumLo, vpaddlq_s32(termLo)); // widen-and-pairwise-add, exact
        sumHi = vaddq_s64(sumHi, vpaddlq_s32(termHi));
    }

    int64x2_t total = vaddq_s64(sumLo, sumHi);
    return vgetq_lane_s64(total, 0) + vgetq_lane_s64(total, 1);
}
#else
int64_t flatten(const int16_t* acc, const int16_t* weight) {
    int64_t sum = 0;
    for (int i = 0; i < HIDDEN; ++i) {
        int v = acc[i];
        v = v < 0 ? 0 : v > QA ? QA : v;
        sum += int64_t(v * v) * weight[i];
    }
    return sum;
}
#endif

// Row-wise int16 add/sub used by accumulator refresh and incremental updates.
// Bit-identical to a scalar element-wise loop (16-bit add/sub wraps exactly
// the same way in both forms).
inline void add_row(int16_t* dst, const int16_t* src) {
#ifdef CC_HAVE_NEON
    for (int i = 0; i < HIDDEN; i += 8)
        vst1q_s16(&dst[i], vaddq_s16(vld1q_s16(&dst[i]), vld1q_s16(&src[i])));
#else
    for (int i = 0; i < HIDDEN; ++i) dst[i] += src[i];
#endif
}

inline void sub_row(int16_t* dst, const int16_t* src) {
#ifdef CC_HAVE_NEON
    for (int i = 0; i < HIDDEN; i += 8)
        vst1q_s16(&dst[i], vsubq_s16(vld1q_s16(&dst[i]), vld1q_s16(&src[i])));
#else
    for (int i = 0; i < HIDDEN; ++i) dst[i] -= src[i];
#endif
}

int32_t output(const int16_t* boys, const int16_t* opps) {
    int64_t sum = flatten(boys, outputWeights[0]) + flatten(opps, outputWeights[1]);
    return int32_t((sum / QA + outputBias) * SCALE / QAB);
}

// akimbo feature base index for a piece of color `pcColor`, type index `pc`
// (0=pawn..5=king), from perspective SIDE (0=white, 1=black). `ksq` is the
// perspective's own king square (already NOT flipped by the caller).
template<int SIDE>
int base_index(Color pcColor, int pc, Square ksq) {
    int k = ksq;
    if ((k & 7) > 3) k ^= 7;               // horizontal mirror by king file
    if (SIDE == 1) k ^= 56;                // vertical flip for black's bucket
    int bucket = BUCKETS[k];
    int colorOffset = SIDE == 0 ? (pcColor == WHITE ? 0 : 384)
                                : (pcColor == WHITE ? 384 : 0);
    return 768 * bucket + colorOffset + 64 * pc;
}

// Runtime feature-row index for a piece on `sq`, from perspective P (0=white,
// 1=black) whose king is on `ksq`. Reproduces base_index<P>() + the square flip
// exactly, so incremental updates match the from-scratch path bit-for-bit.
inline int feature_row(int P, Piece pc, Square sq, Square ksq) {
    int k = ksq;
    if ((k & 7) > 3) k ^= 7;
    if (P == 1) k ^= 56;
    int bucket = BUCKETS[k];
    Color pcColor = color_of(pc);
    int colorOffset = P == 0 ? (pcColor == WHITE ? 0 : 384)
                             : (pcColor == WHITE ? 384 : 0);
    int base = 768 * bucket + colorOffset + 64 * (type_of(pc) - PAWN);
    int flip = ((ksq & 7) > 3 ? 7 : 0) ^ (P == 1 ? 56 : 0);
    return base + (int(sq) ^ flip);
}

// Refresh-detection key: bucket + mirror state for perspective P's king. Any
// change forces a full rebuild of that perspective (indices shift for all pieces).
inline int king_key(int P, Square ksq) {
    int k = ksq, mir = (ksq & 7) > 3 ? 1 : 0;
    if (mir) k ^= 7;
    if (P == 1) k ^= 56;
    return BUCKETS[k] * 2 + mir;
}

// ---- Incremental accumulator stack ----
struct Accumulator {
    int16_t v[2][HIDDEN]; // [perspective][hidden]
    int     key[2];       // king_key per perspective (for refresh detection)
};

constexpr int STACK_SIZE = 1024; // > any real alphabeta+qsearch nesting
// thread_local: each Lazy-SMP search thread walks its own do_move/undo_move
// sequence over its own Position copy and must not share accumulator state
// with any other thread searching concurrently — sharing this would silently
// corrupt evaluations (wrong scores, no crash) rather than just race visibly.
thread_local Accumulator accStack[STACK_SIZE];
thread_local int  accHead = 0;
thread_local bool accActive = false;

// Recorded piece changes for the move currently being applied.
struct DPiece { Piece pc; Square sq; };
thread_local DPiece dRem[4]; thread_local int nRem = 0;
thread_local DPiece dAdd[4]; thread_local int nAdd = 0;

// Rebuild perspective P of `a` from the full board.
void refresh_perspective(Accumulator& a, int P, const Position& pos, Square ksq) {
    std::memcpy(a.v[P], featureBias, sizeof(a.v[P]));
    for (Color c : { WHITE, BLACK })
        for (PieceType pt = PAWN; pt <= KING; ++pt) {
            Piece pcp = make_piece(c, pt);
            Bitboard bb = pos.pieces(c, pt);
            while (bb) {
                int sq = pop_lsb(bb);
                add_row(a.v[P], featureWeights[feature_row(P, pcp, Square(sq), ksq)]);
            }
        }
    a.key[P] = king_key(P, ksq);
}

void refresh_full(Accumulator& a, const Position& pos) {
    refresh_perspective(a, 0, pos, pos.square<KING>(WHITE));
    refresh_perspective(a, 1, pos, pos.square<KING>(BLACK));
}

} // namespace

bool active() { return loaded; }

void init() {
    size_t sz = size_t(CC_nnue_net_end - CC_nnue_net);
    if (sz >= NET_BYTES)
        map_net(CC_nnue_net);
}

bool load_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return false;
    std::streamsize sz = in.tellg();
    if (sz < std::streamsize(NET_BYTES))
        return false;
    in.seekg(0);
    size_t n = size_t(sz);
    std::vector<unsigned char> buf(n);
    if (!in.read(reinterpret_cast<char*>(buf.data()), sz))
        return false;
    fileBuf = std::move(buf);              // keep the bytes alive
    map_net(fileBuf.data());
    return true;
}

Value evaluate(const Position& pos) {
    const int16_t* wPersp; // white-perspective accumulator
    const int16_t* bPersp; // black-perspective accumulator
    int16_t white[HIDDEN], black[HIDDEN];

    if (accActive) {
        const Accumulator& a = accStack[accHead];
        wPersp = a.v[0];
        bPersp = a.v[1];
    } else {
        // Full from-scratch build (used by the `eval` command and as the oracle).
        std::memcpy(white, featureBias, sizeof(white));
        std::memcpy(black, featureBias, sizeof(black));
        Square wksq = pos.square<KING>(WHITE), bksq = pos.square<KING>(BLACK);
        for (Color c : { WHITE, BLACK })
            for (PieceType pt = PAWN; pt <= KING; ++pt) {
                Piece pcp = make_piece(c, pt);
                Bitboard bb = pos.pieces(c, pt);
                while (bb) {
                    int sq = pop_lsb(bb);
                    add_row(white, featureWeights[feature_row(0, pcp, Square(sq), wksq)]);
                    add_row(black, featureWeights[feature_row(1, pcp, Square(sq), bksq)]);
                }
            }
        wPersp = white;
        bPersp = black;
    }

    int32_t eval = pos.side_to_move() == WHITE ? output(wPersp, bPersp)
                                               : output(bPersp, wPersp);

    // akimbo material output scaling.
    int mat = popcount(pos.pieces(KNIGHT)) * SeeVal[KNIGHT]
            + popcount(pos.pieces(BISHOP)) * SeeVal[BISHOP]
            + popcount(pos.pieces(ROOK))   * SeeVal[ROOK]
            + popcount(pos.pieces(QUEEN))  * SeeVal[QUEEN];
    mat = 700 + mat / 32;
    return Value(eval * mat / 1024);
}

// ---- Incremental accumulator interface ----
void set_active(bool on) { accActive = on; }
bool is_active() { return accActive; }

void refresh(const Position& pos) {
    accHead = 0;
    refresh_full(accStack[0], pos);
}

void begin_move() { nRem = nAdd = 0; }
void remove_feature(Piece pc, Square sq) { dRem[nRem++] = { pc, sq }; }
void add_feature(Piece pc, Square sq) { dAdd[nAdd++] = { pc, sq }; }

void end_move(const Position& pos) {
    const Accumulator& parent = accStack[accHead];
    ++accHead;
    Accumulator& cur = accStack[accHead];
    Square ksq[2] = { pos.square<KING>(WHITE), pos.square<KING>(BLACK) };

    for (int P = 0; P < 2; ++P) {
        int nk = king_key(P, ksq[P]);
        if (nk != parent.key[P]) {
            refresh_perspective(cur, P, pos, ksq[P]); // king changed bucket → rebuild
            continue;
        }
        std::memcpy(cur.v[P], parent.v[P], sizeof(cur.v[P]));
        for (int j = 0; j < nRem; ++j)
            sub_row(cur.v[P], featureWeights[feature_row(P, dRem[j].pc, dRem[j].sq, ksq[P])]);
        for (int j = 0; j < nAdd; ++j)
            add_row(cur.v[P], featureWeights[feature_row(P, dAdd[j].pc, dAdd[j].sq, ksq[P])]);
        cur.key[P] = nk;
    }
    nRem = nAdd = 0;
}

void push_null() { accStack[accHead + 1] = accStack[accHead]; ++accHead; }
void pop() { --accHead; }

} // namespace NNUE
