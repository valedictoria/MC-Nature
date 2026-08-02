#include "magic.h"

#include <algorithm>

namespace Magic {

MagicEntry BishopMagics[SQUARE_NB];
MagicEntry RookMagics[SQUARE_NB];

namespace {

Bitboard BishopTable[5248];
Bitboard RookTable[102400];

// Simple deterministic xorshift64* PRNG, biased toward sparse magics via AND-folding.
struct PRNG {
    uint64_t s;
    explicit PRNG(uint64_t seed) : s(seed) {}
    uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    uint64_t sparse() { return next() & next() & next(); }
};

Bitboard slider_attack(const int deltas[4], Square sq, Bitboard occupied) {
    Bitboard attack = 0;
    for (int i = 0; i < 4; ++i) {
        Square s = sq;
        int d = deltas[i];
        while (true) {
            Square next = Square(int(s) + d);
            if (!square_ok(next) || distance(s, next) > 2)
                break;
            attack |= square_bb(next);
            if (occupied & next)
                break;
            s = next;
        }
    }
    return attack;
}

void init_magics(PieceType pt, Bitboard table[], MagicEntry magics[]) {
    const int bishopDeltas[4] = { NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST };
    const int rookDeltas[4]   = { NORTH, EAST, SOUTH, WEST };
    const int* deltas = (pt == BISHOP) ? bishopDeltas : rookDeltas;

    Bitboard occupancy[4096], reference[4096];
    Bitboard* attacksBase = table;

    for (Square s = SQ_A1; s <= SQ_H8; ++s) {
        // Relevant occupancy mask: reachable squares minus the board edges
        // that are irrelevant to blocking (they can never block further).
        Bitboard edges = ((BB::Rank1BB | BB::Rank8BB) & ~rank_bb(s))
                       | ((BB::FileABB | BB::FileHBB) & ~file_bb(s));

        MagicEntry& m = magics[s];
        m.mask = slider_attack(deltas, s, 0) & ~edges;
        m.shift = 64 - popcount(m.mask);
        m.attacks = attacksBase;

        // Enumerate all subsets of the mask (Carry-Rippler) and record references.
        Bitboard b = 0;
        int size = 0;
        do {
            occupancy[size] = b;
            reference[size] = slider_attack(deltas, s, b);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);

        // Search for a magic that maps every subset without a harmful collision.
        PRNG rng(0x2456ABDE9137F0C5ULL ^ (uint64_t(s) * 0x9E3779B97F4A7C15ULL));
        std::fill(m.attacks, m.attacks + size, 0);
        int epoch[4096] = {};
        int currentEpoch = 0;

        for (bool done = false; !done; ) {
            do {
                m.magic = rng.sparse();
            } while (popcount((m.mask * m.magic) >> 56) < 6);

            done = true;
            ++currentEpoch;
            for (int i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < currentEpoch) {
                    epoch[idx] = currentEpoch;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i]) {
                    done = false; // constructive collision
                    break;
                }
            }
        }

        attacksBase += size;
    }
}

} // namespace

void init() {
    init_magics(BISHOP, BishopTable, BishopMagics);
    init_magics(ROOK,   RookTable,   RookMagics);
}

} // namespace Magic
