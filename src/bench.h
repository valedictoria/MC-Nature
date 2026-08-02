#pragma once
// Reproducible fixed-depth search over a fixed position set. Prints a total
// node-count "signature" used to catch accidental behavior changes in refactors.

void bench_run(int depth = 13);
