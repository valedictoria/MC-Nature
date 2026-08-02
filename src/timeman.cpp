#include "timeman.h"

#include <algorithm>

void TimeManager::init(const Limits& limits, Color us) {
    constexpr int64_t Inf = 1LL << 60;

    if (limits.movetime > 0) {
        optimumMs = maximumMs = std::max<int64_t>(1, limits.movetime - limits.moveOverhead);
        return;
    }

    if (!limits.useTimeManager) { // depth / nodes / infinite
        optimumMs = maximumMs = Inf;
        return;
    }

    int64_t time = limits.time[us];
    int64_t inc  = limits.inc[us];
    int64_t remaining = std::max<int64_t>(1, time - limits.moveOverhead);

    // Bullet-aware divisor: shorter clocks spend a smaller fraction per move.
    int64_t divisor = time > 20000 ? 22 : time > 5000 ? 26 : 30;

    optimumMs = remaining / divisor + inc * 3 / 4;
    maximumMs = std::min(remaining * 4 / 5, optimumMs * 5);

    // If movestogo is given, don't hoard beyond a fair share.
    if (limits.movestogo > 0) {
        int64_t fair = remaining / std::max(1, limits.movestogo) + inc * 3 / 4;
        optimumMs = std::min(optimumMs, fair);
        maximumMs = std::min(maximumMs, remaining * 4 / 5);
    }

    optimumMs = std::max<int64_t>(1, optimumMs);
    maximumMs = std::max(optimumMs, maximumMs);
}
