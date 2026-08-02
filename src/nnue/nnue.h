#pragma once
// NNUE evaluation — a faithful port of akimbo's bullet-trained network
// (768 -> 1024)x2 -> 1, 4 king-input buckets + horizontal mirroring, SCReLU,
// QA=255 QB=64 SCALE=400. The default net is embedded in the binary (incbin.s);
// the UCI EvalFile option can override it with an external net of identical
// architecture. HCE remains the fallback when no net is loaded.

#include "../types.h"
#include <string>

class Position;

namespace NNUE {

// True once a network is loaded (the embedded default, or an EvalFile override).
bool active();

// Load the embedded default network. Called once at engine init.
void init();

// Override with an external net file of the same architecture. Returns false
// (and leaves the current net untouched) on any size/read failure.
bool load_file(const std::string& path);

// Side-to-move POV evaluation in centipawn-like units.
Value evaluate(const Position& pos);

// ---- Incremental accumulator (maintained across do_move/undo_move) ----
// When active, the feature-transformer accumulators are updated incrementally
// per move instead of rebuilt from scratch every evaluate(), giving a large NPS
// win. Search enables it and seeds the stack via refresh(); perft leaves it off
// (do_move then skips all NNUE work). The eval always matches the from-scratch
// path bit-for-bit, so the bench signature is unchanged.
void set_active(bool on);
bool is_active();
void refresh(const Position& pos);      // seed the stack top from the full board

// Reported by Position::do_move as it mutates the board (only when active).
void begin_move();
void remove_feature(Piece pc, Square sq);
void add_feature(Piece pc, Square sq);
void end_move(const Position& pos);     // finalize the pushed frame

void push_null();                       // null move: duplicate the top frame
void pop();                             // undo any pushed frame (real or null)

} // namespace NNUE
