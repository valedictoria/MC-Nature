#include "tt.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

TranspositionTable TT;

void TTEntry::save(uint64_t key, Value v, Bound b, int d, Move m, Value ev, uint8_t generation) {
    uint16_t k16 = uint16_t(key);
    uint16_t existingKey16 = key16 ^ data_mix(move16, value16, eval16, depth8, genBound8);
    bool samePos = (k16 == existingKey16);

    // Preserve an existing move if the new one is empty and same position.
    if (m != MOVE_NONE || !samePos)
        move16 = uint16_t(m);

    // Replace unless the incoming entry is shallower for the same position.
    if (b == BOUND_EXACT || !samePos || d + 4 > depth8) {
        value16   = int16_t(v);
        eval16    = int16_t(ev);
        depth8    = int8_t(d);
        genBound8 = uint8_t(generation | b);
    }

    // key16 always re-derived from the final field values (move16 may have
    // changed above even when the rest of the entry didn't) so the XOR
    // relationship stays consistent for the next probe/save.
    key16 = k16 ^ data_mix(move16, value16, eval16, depth8, genBound8);
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

int TranspositionTable::hashfull() const {
    if (!table || clusterCount == 0)
        return 0;
    size_t sampleClusters = std::min<size_t>(1000, clusterCount);
    int cnt = 0;
    for (size_t i = 0; i < sampleClusters; ++i)
        for (int j = 0; j < ClusterSize; ++j)
            // "Full" = written under the current search generation (matches the
            // low bound bits' generation tag, ignoring the bound bits themselves).
            if ((table[i].entry[j].genBound8 & 0xFC) == (generation8 & 0xFC)
                && table[i].entry[j].genBound8 != 0)
                ++cnt;
    return int(size_t(cnt) * 1000 / (sampleClusters * ClusterSize));
}

TTEntry* TranspositionTable::probe(uint64_t key, bool& found) const {
    TTEntry* const first = clusterOf(key)->entry;
    uint16_t k16 = uint16_t(key);

    for (int i = 0; i < ClusterSize; ++i) {
        TTEntry& e = first[i];
        uint16_t entryKey16 = e.key16 ^ TTEntry::data_mix(e.move16, e.value16, e.eval16, e.depth8, e.genBound8);
        if (entryKey16 == k16 && e.genBound8) {
            found = true;
            return &e;
        }
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
