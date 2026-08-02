#include "bitboard.h"
#include "magic.h"
#include "zobrist.h"
#include "search.h"
#include "perft.h"
#include "bench.h"
#include "uci.h"
#include "nnue/nnue.h"

#include <cstdlib>
#include <cstring>

static void engine_init() {
    BB::init();       // geometry + non-slider attacks (fills SquareDistance)
    Magic::init();    // slider magics (needs SquareDistance)
    Zobrist::init();
    Search::init();
    NNUE::init();     // load the embedded default net (HCE fallback if it fails)
}

int main(int argc, char* argv[]) {
    engine_init();

    if (argc > 1 && std::strcmp(argv[1], "perft") == 0)
        return perft_suite() ? 0 : 1;

    if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
        bench_run(argc > 2 ? std::atoi(argv[2]) : 13);
        return 0;
    }

    uci_loop();
    return 0;
}
