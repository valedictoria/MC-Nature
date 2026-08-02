#include "tt.h"

#include <cstdlib>
#include <cstring>

TranspositionTable TT;

void TTEntry::save(uint64_t key, Value v, Bound b, int d, Move m, Value ev, uint8_t generation) {
    uint16_t k16 = uint16_t(key);

    // Preserve an existing move if the new one is empty and same position.
    if (m != MOVE_NONE || k16 != key16)
        move16 = uint16_t(m);

    // Replace unless the incoming entry is shallower for the same position.
    if (b == BOUND_EXACT || k16 != key16 || d + 4 > depth8) {
        key16     = k16;
        value16   = int16_t(v);
        eval16    = int16_t(ev);
        depth8    = int8_t(d);
        genBound8 = uint8_t(generation | b);
    }
}

void TranspositionTable::free_mem() {
    if (table) { std::free(table); table = nullptr; }
}

void TranspositionTable::resize(size_t mb) {
    free_mem();
    clusterCount = (mb * 1024 * 1024) / sizeof(TTCluster);
    if (clusterCount == 0)
        clusterCount = 1;
    table = static_cast<TTCluster*>(std::calloc(clusterCount, sizeof(TTCluster)));
    generation8 = 0;
}

void TranspositionTable::clear() {
    if (table)
        std::memset(table, 0, clusterCount * sizeof(TTCluster));
    generation8 = 0;
}

TTEntry* TranspositionTable::probe(uint64_t key, bool& found) const {
    TTEntry* const first = clusterOf(key)->entry;
    uint16_t k16 = uint16_t(key);

    for (int i = 0; i < ClusterSize; ++i)
        if (first[i].key16 == k16 && first[i].genBound8) {
            found = true;
            return &first[i];
        }

    // Pick the shallowest / oldest entry as the replacement target.
    TTEntry* replace = first;
    for (int i = 1; i < ClusterSize; ++i) {
        int replAge = (generation8 - (replace->genBound8 & 0xFC)) & 0xFF;
        int candAge = (generation8 - (first[i].genBound8 & 0xFC)) & 0xFF;
        if (replace->depth8 - replAge > first[i].depth8 - candAge)
            replace = &first[i];
    }
    found = false;
    return replace;
}
