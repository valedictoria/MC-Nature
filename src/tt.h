#pragma once
// Transposition table: 3-entry clusters, depth+age replacement.

#include "types.h"
#include <vector>

enum Bound { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };

struct TTEntry {
    // key16 stores (true key low bits) XOR (mix of the fields below), not the
    // raw key. Lazy-SMP lockless check: a torn concurrent read/write (one
    // thread's key half combined with another thread's data half) recombines
    // to the wrong value here and self-corrects to a probe miss instead of a
    // corrupted hit. Single-threaded behaviour is unchanged (the XOR cancels
    // out on a clean read), so the bench signature is unaffected.
    uint16_t key16;
    uint16_t move16;
    int16_t  value16;
    int16_t  eval16;
    int8_t   depth8;
    uint8_t  genBound8; // bits 0-1 bound, bits 2-7 generation

    Move  move() const  { return Move(move16); }
    Value value() const { return Value(value16); }
    Value eval() const  { return Value(eval16); }
    int   depth() const { return depth8; }
    Bound bound() const { return Bound(genBound8 & 0x3); }

    void save(uint64_t key, Value v, Bound b, int d, Move m, Value ev, uint8_t generation);

    static uint16_t data_mix(uint16_t move16, int16_t value16, int16_t eval16,
                              int8_t depth8, uint8_t genBound8) {
        return move16 ^ uint16_t(value16) ^ uint16_t(eval16)
             ^ uint16_t(uint16_t(uint8_t(depth8)) << 8) ^ genBound8;
    }
};

constexpr int ClusterSize = 3;
struct TTCluster {
    TTEntry entry[ClusterSize];
    char padding[2]; // pad 30 -> 32 bytes (half a cache line)
};

class TranspositionTable {
public:
    ~TranspositionTable() { free_mem(); }

    void resize(size_t mb);
    void clear();
    void new_search() { generation8 += 4; } // low 2 bits reserved for bound
    uint8_t generation() const { return generation8; }

    // Returns the matching entry (found=true) or the replacement target.
    TTEntry* probe(uint64_t key, bool& found) const;

    // Per-mille of sampled entries written this generation (UCI "hashfull").
    int hashfull() const;

private:
    void free_mem();
    TTCluster* clusterOf(uint64_t key) const {
        // High bits select the cluster (mul-shift index).
        return &table[(__uint128_t(key) * clusterCount) >> 64];
    }

    TTCluster* table = nullptr;
    size_t clusterCount = 0;
    uint8_t generation8 = 0;

    friend struct TTEntry;
};

extern TranspositionTable TT;
