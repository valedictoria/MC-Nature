#pragma once
// UCI protocol handling and move formatting.

#include "types.h"

#include <string>

class Position;

// Writes the UCI string for m into buf (needs 6 bytes: e.g. "e7e8q\0").
void move_to_uci(Move m, char* buf);

// Parses a UCI move string in the context of pos (resolves ep/castling/promo).
// Returns MOVE_NONE if no legal move matches.
Move uci_to_move(const Position& pos, const std::string& str);

void uci_loop();
