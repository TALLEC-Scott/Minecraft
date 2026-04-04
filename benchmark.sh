#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

LABEL="${1:-unlabeled}"
RESULTS_DIR="benchmark_history"
mkdir -p "$RESULTS_DIR"

echo "Building..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native" > /dev/null 2>&1
make -C build -j$(nproc) > /dev/null 2>&1
echo "Build done."

echo "Running benchmark (300 warmup + 300 measured frames)..."
./build/minecraft --benchmark

# Display results
echo ""
echo "=== Results: $LABEL ==="
cat benchmark_results.txt

# Save a timestamped copy
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
OUTFILE="$RESULTS_DIR/${TIMESTAMP}_${LABEL}.txt"
echo "label:   $LABEL" > "$OUTFILE"
echo "commit:  $(git rev-parse --short HEAD) - $(git log -1 --pretty=%s)" >> "$OUTFILE"
echo "date:    $(date)" >> "$OUTFILE"
echo "" >> "$OUTFILE"
cat benchmark_results.txt >> "$OUTFILE"
echo ""
echo "Saved to $OUTFILE"

# If there's a previous result, show a comparison
PREV=$(ls "$RESULTS_DIR"/*.txt 2>/dev/null | grep -v "$OUTFILE" | tail -1)
if [ -n "$PREV" ]; then
    echo ""
    echo "=== Comparison with previous run ==="
    echo "Previous: $(basename $PREV)"
    prev_avg=$(grep "^avg:" "$PREV"     | awk '{print $2}')
    curr_avg=$(grep "^avg:" benchmark_results.txt | awk '{print $2}')
    prev_fps=$(grep "^avg:" "$PREV"     | grep -oP '\d+ fps' | grep -oP '\d+')
    curr_fps=$(grep "^avg:" benchmark_results.txt | grep -oP '\d+ fps' | grep -oP '\d+')
    echo "avg frame time:  $prev_avg ms  ->  $curr_avg ms"
    echo "avg FPS:         $prev_fps  ->  $curr_fps"
    if [ -n "$prev_fps" ] && [ -n "$curr_fps" ] && [ "$prev_fps" -ne 0 ]; then
        delta=$(echo "scale=1; ($curr_fps - $prev_fps) * 100 / $prev_fps" | bc)
        echo "change:          ${delta}%"
    fi
fi
