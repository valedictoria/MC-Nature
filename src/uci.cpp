#include "uci.h"

#include "position.h"
#include "movegen.h"
#include "perft.h"
#include "search.h"
#include "bench.h"
#include "tt.h"
#include "eval.h"
#include "syzygy/syzygy.h"
#include "nnue/nnue.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>

static const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

namespace {
int64_t g_moveOverhead = 100;
size_t  g_hashMb = 64;
std::thread g_searchThread;

void join_search() {
    if (g_searchThread.joinable())
        g_searchThread.join();
}
} // namespace

void move_to_uci(Move m, char* buf) {
    if (m == MOVE_NONE) { buf[0]='0'; buf[1]='0'; buf[2]='0'; buf[3]='0'; buf[4]=0; return; }
    Square from = from_sq(m), to = to_sq(m);
    buf[0] = char('a' + file_of(from));
    buf[1] = char('1' + rank_of(from));
    buf[2] = char('a' + file_of(to));
    buf[3] = char('1' + rank_of(to));
    if (type_of_move(m) == PROMOTION) {
        buf[4] = " pnbrqk"[promotion_type(m)];
        buf[5] = 0;
    } else {
        buf[4] = 0;
    }
}

Move uci_to_move(const Position& pos, const std::string& str) {
    for (const ExtMove& m : MoveList(pos)) {
        char buf[6];
        move_to_uci(m, buf);
        if (str == buf)
            return m;
    }
    return MOVE_NONE;
}

namespace {

void set_position(Position& pos, StateInfo states[], int& stateCount, std::istringstream& is) {
    std::string token, fen;
    is >> token;
    if (token == "startpos") {
        fen = StartFEN;
        is >> token; // consume "moves"
    } else if (token == "fen") {
        while (is >> token && token != "moves")
            fen += token + " ";
    } else {
        return;
    }

    stateCount = 0;
    pos.set(fen, &states[stateCount++]);

    while (is >> token) {
        Move m = uci_to_move(pos, token);
        if (m == MOVE_NONE)
            break;
        pos.do_move(m, states[stateCount++]);
    }
}

void go(Position& pos, std::istringstream& is) {
    std::string token;
    Limits limits;
    limits.moveOverhead = g_moveOverhead;
    int perftDepth = 0;

    while (is >> token) {
        if      (token == "wtime")     is >> limits.time[WHITE];
        else if (token == "btime")     is >> limits.time[BLACK];
        else if (token == "winc")      is >> limits.inc[WHITE];
        else if (token == "binc")      is >> limits.inc[BLACK];
        else if (token == "movestogo") is >> limits.movestogo;
        else if (token == "movetime")  is >> limits.movetime;
        else if (token == "depth")     is >> limits.depth;
        else if (token == "nodes")     is >> limits.nodes;
        else if (token == "infinite")  limits.infinite = true;
        else if (token == "perft")     is >> perftDepth;
    }

    if (perftDepth > 0) {
        perft_divide(pos, perftDepth);
        return;
    }

    limits.useTimeManager = (limits.time[WHITE] || limits.time[BLACK]);

    join_search();
    g_searchThread = std::thread([&pos, limits]() { Search::think(pos, limits); });
}

} // namespace

void uci_loop() {
    Position pos;
    static StateInfo states[1024];
    int stateCount = 0;
    pos.set(StartFEN, &states[stateCount++]);
    TT.resize(g_hashMb);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string token;
        is >> token;

        if (token == "uci") {
            std::cout << "id name MC-Nature\n"
                      << "id author valedictoria\n"
                      << "option name Hash type spin default 64 min 1 max 65536\n"
                      << "option name Threads type spin default 1 min 1 max 1\n"
                      << "option name Move Overhead type spin default 100 min 0 max 5000\n"
                      << "option name Ponder type check default false\n"
                      << "option name SyzygyPath type string default <empty>\n"
                      << "option name EvalFile type string default <internal>\n";
            Search::print_tune_options(std::cout);
            std::cout << "uciok" << std::endl;
        } else if (token == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (token == "setoption") {
            std::string name, value;
            is >> token; // "name"
            is >> name;
            while (is >> token && token != "value")
                name += " " + token;
            if (is >> value) {
                while (is >> token) value += " " + token;
            }
            if (name == "Hash") { g_hashMb = std::max(1, std::stoi(value)); TT.resize(g_hashMb); }
            else if (name == "Move Overhead") g_moveOverhead = std::stoi(value);
            else if (name == "SyzygyPath") Syzygy::init(value);
            else if (name == "EvalFile") {
                // Override the embedded net; keep the current one on failure.
                if (!value.empty() && value != "<internal>" && value != "<empty>")
                    NNUE::load_file(value);
            }
            // Otherwise try a tunable search parameter (SPSA). Unknown names —
            // including Threads / Ponder — are accepted and ignored.
            else Search::set_tune(name, std::atoi(value.c_str()));
        } else if (token == "ucinewgame") {
            join_search();
            TT.clear();
            Search::clear();
        } else if (token == "position") {
            join_search();
            set_position(pos, states, stateCount, is);
        } else if (token == "go") {
            go(pos, is);
        } else if (token == "perft") {
            int d; is >> d; perft_divide(pos, d);
        } else if (token == "divide") {
            int d; is >> d; perft_divide(pos, d);
        } else if (token == "bench") {
            join_search();
            int d = 0; is >> d; bench_run(d > 0 ? d : 13);
        } else if (token == "eval") {
            std::cout << "eval " << evaluate(pos)
                      << (NNUE::active() ? " (nnue)" : " (hce)") << std::endl;
        } else if (token == "d" || token == "print") {
            std::cout << pos.fen() << std::endl;
        } else if (token == "stop") {
            Search::stop.store(true, std::memory_order_relaxed);
            join_search();
        } else if (token == "quit") {
            Search::stop.store(true, std::memory_order_relaxed);
            join_search();
            break;
        }
    }
    join_search();
}
