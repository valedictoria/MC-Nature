#include "bench.h"

#include "position.h"
#include "search.h"
#include "timeman.h"
#include "tt.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>

namespace {

// Fixed position set spanning openings, middlegames, tactics and endgames.
// Kept stable so the summed node count is a reproducible signature.
const char* BenchFens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14",
    "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14",
    "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
    "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
    "2r2rk1/1bqnbpp1/1p1ppn1p/pP6/N1P1P3/P2B1N1P/1B2QPP1/R2R2K1 b - - 2 15",
    "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",
    "8/8/1P6/5pr1/8/4R3/7k/2K5 w - - 0 1",
};

} // namespace

void bench_run(int depth) {
    if (depth <= 0)
        depth = 13;

    TT.resize(16); // fixed hash so the node signature is reproducible

    int64_t totalNodes = 0;
    auto t0 = std::chrono::steady_clock::now();

    std::streambuf* coutBuf = std::cout.rdbuf();

    for (const char* fen : BenchFens) {
        Position pos;
        StateInfo st;
        pos.set(fen, &st);

        TT.clear();       // deterministic starting state per position
        Search::clear();

        Limits lim;
        lim.depth = depth;

        std::ostringstream sink;
        std::cout.rdbuf(sink.rdbuf()); // suppress per-iteration info / bestmove
        Search::think(pos, lim);
        std::cout.rdbuf(coutBuf);

        totalNodes += Search::nodes;
        std::printf("%-72s %12lld nodes\n", fen, (long long)Search::nodes);
    }

    auto t1 = std::chrono::steady_clock::now();
    int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    int64_t nps = ms > 0 ? totalNodes * 1000 / ms : 0;

    std::printf("\n===========================\n");
    std::printf("Total time (ms) : %lld\n", (long long)ms);
    std::printf("Nodes searched  : %lld\n", (long long)totalNodes);
    std::printf("Nodes/second    : %lld\n", (long long)nps);
}
