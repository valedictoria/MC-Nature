#pragma once
// Thin C++ glue over the vendored Fathom probing library (tbprobe.c).
// Converts a CarelessChess Position into Fathom's bitboard arguments and maps
// results back into engine types. All probing is a no-op unless a SyzygyPath
// has been loaded (MaxPieces > 0), so default builds are unaffected.

#include "../types.h"
#include <string>

class Position;

namespace Syzygy {

// Largest number of pieces present in the loaded tablebases (== TB_LARGEST);
// 0 when no tablebases are loaded.
extern int MaxPieces;

inline bool active() { return MaxPieces > 0; }

// (Re)load Syzygy tablebases from a path (colon/semicolon-separated dirs, per
// Fathom). An empty path frees any loaded tables.
void init(const std::string& path);

// Internal-node WDL probe. Returns true on a hit and sets `wdl` to the
// side-to-move result: +1 win, 0 draw (incl. cursed/blessed under the 50-move
// rule), -1 loss. Caller must gate on rule50 == 0 and no castling rights.
bool probe_wdl(const Position& pos, int& wdl);

// Root DTZ probe. Returns true on a hit, sets `best` to a DTZ-optimal legal
// move (in the engine's exact Move encoding) and `wdl` to +1/0/-1 from the
// side-to-move POV. 50-move aware, so it needs no rule50 gate.
bool probe_root(const Position& pos, Move& best, int& wdl);

} // namespace Syzygy
