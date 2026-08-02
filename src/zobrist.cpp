#include "zobrist.h"

namespace Zobrist {

uint64_t psq[PIECE_NB][SQUARE_NB];
uint64_t enpassant[FILE_NB];
uint64_t castling[CASTLING_RIGHT_NB];
uint64_t side;

namespace {
struct PRNG {
    uint64_t s;
    explicit PRNG(uint64_t seed) : s(seed) {}
    uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
};
} // namespace

void init() {
    PRNG rng(1070372ULL);

    for (int pc = 0; pc < PIECE_NB; ++pc)
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
            psq[pc][s] = rng.next();

    for (int f = 0; f < FILE_NB; ++f)
        enpassant[f] = rng.next();

    // Build castling keys for every rights combination by XOR-composing the
    // four single-right keys, so incremental XOR of a subset stays consistent.
    uint64_t singles[4] = { rng.next(), rng.next(), rng.next(), rng.next() };
    for (int cr = 0; cr < CASTLING_RIGHT_NB; ++cr) {
        castling[cr] = 0;
        for (int i = 0; i < 4; ++i)
            if (cr & (1 << i))
                castling[cr] ^= singles[i];
    }

    side = rng.next();
}

} // namespace Zobrist
