#include "perft.h"

#include "movegen.h"
#include "uci.h"

#include <chrono>
#include <cstdio>

uint64_t perft(Position& pos, int depth) {
    if (depth == 0)
        return 1;

    MoveList moves(pos);

    // Bulk counting at depth 1 is a common (and safe) speedup.
    if (depth == 1)
        return moves.size();

    uint64_t nodes = 0;
    StateInfo st;
    for (const ExtMove& m : moves) {
        pos.do_move(m, st);
        nodes += perft(pos, depth - 1);
        pos.undo_move(m);
    }
    return nodes;
}

void perft_divide(Position& pos, int depth) {
    uint64_t total = 0;
    MoveList moves(pos);
    StateInfo st;
    for (const ExtMove& m : moves) {
        pos.do_move(m, st);
        uint64_t n = depth > 1 ? perft(pos, depth - 1) : 1;
        pos.undo_move(m);
        char buf[8];
        move_to_uci(m, buf);
        std::printf("%s: %llu\n", buf, (unsigned long long)n);
        total += n;
    }
    std::printf("\nNodes: %llu\n", (unsigned long long)total);
}

bool perft_suite() {
    struct Case { const char* fen; int depth; uint64_t expected; };
    static const Case cases[] = {
        { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6, 119060324ULL },
        { "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5, 193690690ULL },
        { "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 7, 178633661ULL },
        { "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6, 706045033ULL },
        { "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5, 89941194ULL },
        { "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 5, 164075551ULL },
    };

    bool allPass = true;
    for (const Case& c : cases) {
        Position pos;
        StateInfo si;
        pos.set(c.fen, &si);
        auto t0 = std::chrono::steady_clock::now();
        uint64_t got = perft(pos, c.depth);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        bool ok = got == c.expected;
        allPass &= ok;
        std::printf("[%s] depth %d  expected %llu  got %llu  (%.0f ms)\n",
                    ok ? "PASS" : "FAIL", c.depth,
                    (unsigned long long)c.expected, (unsigned long long)got, ms);
        if (!ok)
            std::printf("       FEN: %s\n", c.fen);
    }
    std::printf("\n%s\n", allPass ? "All perft positions passed." : "PERFT FAILED.");
    return allPass;
}
