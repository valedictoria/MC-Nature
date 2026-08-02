#!/usr/bin/env bash
# SPRT wrapper around fastchess for CarelessChess dev vs baseline.
#
# Usage:
#   tools/sprt.sh snapshot [tag]     save current build/carelesschess as baseline (tools/baselines/cc-<tag>)
#   tools/sprt.sh list               list saved baselines
#   tools/sprt.sh [options]          run SPRT: build/carelesschess (dev) vs baseline (base)
#
# Options (env vars override defaults):
#   TC=8+0.08        time control (campaign convention)
#   ELO0=0 ELO1=10   SPRT bounds (campaign convention)
#   ALPHA=0.05 BETA=0.05
#   ROUNDS=2000      max rounds (each round = 2 games w/ swapped colors)
#   CONCURRENCY=4    parallel games (M-series: leave 2-4 cores free for OS)
#   HASH=64          per-engine hash MB (small: forces search quality over TT reuse)
#   THREADS=1        per-engine threads (SPRT convention: 1 to maximize concurrency)
#   BASE=<tag>       baseline to compare against (default: latest)
#   BOOK=<path>      opening book (.pgn or .epd)
#
# Exit: 0 = H1 accepted (patch is better), 1 = H0 accepted (patch not better),
#       2 = inconclusive after ROUNDS.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FASTCHESS="$SCRIPT_DIR/fastchess/fastchess"
BASELINES_DIR="$SCRIPT_DIR/baselines"
# Baseline files are named "<PREFIX>-<tag>" (e.g. cc-capthist-step6).
PREFIX="${PREFIX:-cc}"
DEV_BIN="${DEV_BIN:-$ENGINE_DIR/build/carelesschess}"
DEFAULT_BOOK="$SCRIPT_DIR/books/8moves_v3.pgn"

mkdir -p "$BASELINES_DIR"

die() { echo "error: $*" >&2; exit 1; }

cmd_snapshot() {
    [[ -x "$DEV_BIN" ]] || die "no dev binary at $DEV_BIN — run 'make' first"
    local tag="${1:-$(date +%Y%m%d-%H%M%S)}"
    local dest="$BASELINES_DIR/$PREFIX-$tag"
    cp "$DEV_BIN" "$dest"
    echo "saved baseline: $dest"
    ln -sfn "$PREFIX-$tag" "$BASELINES_DIR/latest"
    echo "'latest' now points to $tag"
}

cmd_list() {
    ls -lh "$BASELINES_DIR" 2>/dev/null || echo "(no baselines)"
}

cmd_run() {
    [[ -x "$FASTCHESS" ]] || die "fastchess not built at $FASTCHESS"
    [[ -x "$DEV_BIN" ]] || die "no dev binary — run 'make' in engine-src/"

    local base_tag="${BASE:-latest}"
    local base_bin="$BASELINES_DIR/$PREFIX-$base_tag"
    if [[ "$base_tag" == "latest" ]]; then
        base_bin="$(readlink "$BASELINES_DIR/latest" 2>/dev/null || true)"
        [[ -n "$base_bin" ]] || die "no baseline saved yet — run: tools/sprt.sh snapshot"
        base_bin="$BASELINES_DIR/$base_bin"
    fi
    [[ -x "$base_bin" ]] || die "baseline not found: $base_bin"

    local tc="${TC:-8+0.08}"
    local elo0="${ELO0:-0}" elo1="${ELO1:-10}"
    local alpha="${ALPHA:-0.05}" beta="${BETA:-0.05}"
    local rounds="${ROUNDS:-2000}"
    local concurrency="${CONCURRENCY:-4}"
    local hash="${HASH:-64}"
    local threads="${THREADS:-1}"
    local book="${BOOK:-$DEFAULT_BOOK}"

    local book_args=()
    if [[ -f "$book" ]]; then
        local fmt="pgn"; [[ "$book" == *.epd ]] && fmt="epd"
        book_args=(-openings "file=$book" "format=$fmt" "order=random" "plies=16")
    else
        echo "warning: no opening book at $book — using startpos (bad for SPRT variance)"
    fi

    echo "SPRT: dev=$DEV_BIN"
    echo "      base=$base_bin"
    echo "      tc=$tc  bounds=[$elo0, $elo1]  alpha=$alpha  beta=$beta"
    echo "      rounds<=$rounds  concurrency=$concurrency  hash=${hash}MB  threads=$threads"
    echo

    exec "$FASTCHESS" \
        -engine "cmd=$DEV_BIN"  "name=dev" \
        -engine "cmd=$base_bin" "name=base" \
        -each "tc=$tc" "proto=uci" "option.Hash=$hash" "option.Threads=$threads" \
        -rounds "$rounds" -games 2 -repeat -concurrency "$concurrency" \
        -sprt "elo0=$elo0" "elo1=$elo1" "alpha=$alpha" "beta=$beta" "model=normalized" \
        -resign movecount=4 score=700 twosided=true \
        -draw movenumber=40 movecount=10 score=8 \
        -ratinginterval 10 \
        "${book_args[@]}"
}

case "${1:-run}" in
    snapshot) shift; cmd_snapshot "$@" ;;
    list)     cmd_list ;;
    run|"")   cmd_run ;;
    -h|--help)
        sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
        ;;
    *)
        die "unknown command: $1 (try snapshot|list|run)"
        ;;
esac
