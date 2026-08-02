#pragma once
// Legal move generation.

#include "position.h"

struct ExtMove {
    Move move;
    int  value;
    operator Move() const { return move; }
    void operator=(Move m) { move = m; }
};

// Generate all legal moves.
ExtMove* generate_legal(const Position& pos, ExtMove* list);

// Generate legal captures + promotions (for quiescence search).
ExtMove* generate_tactical(const Position& pos, ExtMove* list);

// Convenience wrapper for iteration.
template<ExtMove* (*Gen)(const Position&, ExtMove*)>
struct MoveListT {
    ExtMove list[MAX_MOVES];
    ExtMove* last;
    explicit MoveListT(const Position& pos) : last(Gen(pos, list)) {}
    const ExtMove* begin() const { return list; }
    const ExtMove* end() const { return last; }
    size_t size() const { return size_t(last - list); }
    bool contains(Move m) const {
        for (const ExtMove* it = list; it != last; ++it)
            if (it->move == m) return true;
        return false;
    }
};

using MoveList        = MoveListT<generate_legal>;
using TacticalList    = MoveListT<generate_tactical>;
