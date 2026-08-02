# MeikeChess

A UCI-compliant C++ chess engine.

## Build

Note, GlobeBot is the name for the product while developing.

Requires only `clang++` (comes with Xcode Command Line Tools) and `make`.

```sh
make            # builds build/globebot
make perft      # builds and runs the perft verification suite
make install    # copies build/globebot to ../lichess-bot/engines/globebot
make clean
``` Then in `lichess-bot/config.yml` set:

```yaml
engine:
  name: globebot
  protocol: uci
  uci_options:
    EvalFile: engines/globebot.nnue   # optional; HCE used if absent
```

## Verify movegen (perft)

```sh
./build/globebot perft
```

Runs the standard six perft positions to depth 5 and reports pass/fail against known node counts.

## UCI usage

```sh
./build/globebot
uci
isready
position startpos moves e2e4 e7e5
go depth 8
```
