#pragma once
// Transposition table: 3-entry clusters, depth+age replacement.

#include "types.h"
#include <vector>

enum Bound { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };

struct TTEntry {
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
