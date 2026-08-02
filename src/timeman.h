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
