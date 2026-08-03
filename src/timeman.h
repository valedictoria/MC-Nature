#pragma once
// Time allocation with a soft/hard split.

#include "types.h"
#include <chrono>
#include <cstdint>

struct Limits {
    int64_t time[COLOR_NB] = { 0, 0 }; // wtime, btime (ms)
    int64_t inc[COLOR_NB]  = { 0, 0 };
    int64_t movetime = 0;
    int     movestogo = 0;
    int     depth = 0;
    int64_t nodes = 0;
    int64_t moveOverhead = 100;
    bool    infinite = false;
    bool    useTimeManager = false; // true iff a game clock was given

    // `go searchmoves <m1> <m2> ...`: restrict the root to these moves only
    // (empty = no restriction). Populated by the UCI layer, which has the
    // Position needed to parse move strings.
    Move searchMoves[MAX_MOVES];
    int  searchMovesCount = 0;

    // True for `go ponder`. Handled as a plain infinite search (run until an
    // external `stop`, or a fresh `position`+`go`) — this engine doesn't
    // implement true ponderhit continuation, so this is the minimally
    // correct behavior: it stops the engine from treating the opponent's
    // clock as its own move-decision budget and returning an immediate
    // bestmove mid-ponder, without pretending to do more than that.
    bool ponder = false;
};

class TimeManager {
public:
    void init(const Limits& limits, Color us);
    void start() { startTime = now(); }

    int64_t elapsed() const { return now() - startTime; }
    int64_t soft_ms() const { return optimumMs; }
    int64_t hard_ms() const { return maximumMs; }

    void extend_soft(double factor) {
        optimumMs = int64_t(optimumMs * factor);
        if (optimumMs > maximumMs * 4 / 5)
            optimumMs = maximumMs * 4 / 5;
    }

    static int64_t now() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

private:
    int64_t startTime = 0;
    int64_t optimumMs = 0;
    int64_t maximumMs = 0;
};
