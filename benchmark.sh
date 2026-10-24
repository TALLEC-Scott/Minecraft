#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
    echo "usage: $0 [label] [--headless] [--seed N] [--bench-frames N] [--greedy 0|1] [--fancy-leaves 0|1]" >&2
    exit 1
}

# Options taking a value accept both "--opt N" and "--opt=N"; they are
# passed through to the binary, which validates them.
LABEL=""
HEADLESS=""
GAME_ARGS=()
MEASURE_FRAMES=600
while [ $# -gt 0 ]; do
    case "$1" in
        --headless) HEADLESS="--headless" ;;
        --seed=* | --bench-frames=* | --greedy=* | --fancy-leaves=*)
            opt="${1%%=*}" val="${1#*=}"
            GAME_ARGS+=("$opt" "$val")
            [ "$opt" = "--bench-frames" ] && MEASURE_FRAMES="$val"
            ;;
        --seed | --bench-frames | --greedy | --fancy-leaves)
            [ $# -ge 2 ] || usage
            GAME_ARGS+=("$1" "$2")
            [ "$1" = "--bench-frames" ] && MEASURE_FRAMES="$2"
            shift
            ;;
        -*) echo "unknown option: $1" >&2; usage ;;
        *) [ -z "$LABEL" ] && LABEL="$1" ;;
    esac
    shift
done
[ -z "$LABEL" ] && LABEL="unlabeled"

RESULTS_DIR="benchmark_history"
mkdir -p "$RESULTS_DIR"

echo "Building..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native" > /dev/null 2>&1
make -C build -j$(nproc) > /dev/null 2>&1
echo "Build done."

MODE="benchmark"
[ -n "$HEADLESS" ] && MODE="headless benchmark"
echo "Running $MODE (600-frame warmup spin + hold until streaming settles, then $MEASURE_FRAMES measured frames)..."
./build/minecraft --benchmark $HEADLESS "${GAME_ARGS[@]}" "$LABEL"

# Display legacy results (backward compat)
echo ""
echo "=== Results: $LABEL ==="
cat benchmark_results.txt

# Display profile breakdown if available
if [ -f profile_results.txt ]; then
    cat profile_results.txt
fi

# Save a timestamped copy with both legacy + profile data
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
OUTFILE="$RESULTS_DIR/${TIMESTAMP}_${LABEL}.txt"
{
    echo "label:   $LABEL"
    echo "commit:  $(git rev-parse --short HEAD) - $(git log -1 --pretty=%s)"
    echo "date:    $(date)"
    echo ""
    cat benchmark_results.txt
    if [ -f profile_results.txt ]; then
        echo ""
        cat profile_results.txt
    fi
} > "$OUTFILE"
echo ""
echo "Saved to $OUTFILE"

# Compare with previous run
PREV=$(ls "$RESULTS_DIR"/*.txt 2>/dev/null | grep -v "$OUTFILE" | tail -1)
if [ -n "$PREV" ]; then
    echo ""
    echo "=== Comparison with previous run ==="
    echo "Previous: $(basename $PREV)"
    prev_cfg=$(grep "^config:" "$PREV" | head -1 | cut -d: -f2- | xargs)
    curr_cfg=$(grep "^config:" benchmark_results.txt | head -1 | cut -d: -f2- | xargs)
    if [ "$prev_cfg" != "$curr_cfg" ]; then
        echo "WARNING: run configs differ — not a like-for-like comparison"
        echo "  previous: ${prev_cfg:-unknown (older run)}"
        echo "  current:  ${curr_cfg:-unknown}"
    fi
    prev_avg=$(grep "^avg:" "$PREV"     | head -1 | awk '{print $2}')
    curr_avg=$(grep "^avg:" benchmark_results.txt | head -1 | awk '{print $2}')
    prev_fps=$(grep "^avg:" "$PREV"     | head -1 | grep -oP '\d+ fps' | grep -oP '\d+')
    curr_fps=$(grep "^avg:" benchmark_results.txt | head -1 | grep -oP '\d+ fps' | grep -oP '\d+')
    echo "avg frame time:  $prev_avg ms  ->  $curr_avg ms"
    echo "avg FPS:         $prev_fps  ->  $curr_fps"
    if [ -n "$prev_fps" ] && [ -n "$curr_fps" ] && [ "$prev_fps" -ne 0 ]; then
        delta=$(echo "scale=1; ($curr_fps - $prev_fps) * 100 / $prev_fps" | bc)
        echo "change:          ${delta}%"
    fi
fi
