#!/bin/bash
# Manual mutation testing for BlockFlag system
set -e
cd "$(dirname "$0")/.."

CUBE_H="include/cube.h"
BACKUP="/tmp/cube_h_backup.h"
cp "$CUBE_H" "$BACKUP"

KILLED=0
SURVIVED=0
TOTAL=0

apply_mutation() {
    # Use python for reliable find/replace
    python3 -c "
import sys
with open('$CUBE_H', 'r') as f:
    content = f.read()
content = content.replace(sys.argv[1], sys.argv[2], 1)
with open('$CUBE_H', 'w') as f:
    f.write(content)
" "$1" "$2"
}

run_mutant() {
    local desc="$1"
    local search="$2"
    local replace="$3"
    TOTAL=$((TOTAL + 1))

    apply_mutation "$search" "$replace"

    if ! make -C build -j$(nproc) tests 2>/dev/null 1>/dev/null; then
        echo "  [$TOTAL] KILLED (build fail): $desc"
        KILLED=$((KILLED + 1))
        cp "$BACKUP" "$CUBE_H"
        return
    fi

    if ./build/tests --gtest_filter='BlockFlag*' 2>/dev/null 1>/dev/null; then
        echo "  [$TOTAL] SURVIVED: $desc"
        SURVIVED=$((SURVIVED + 1))
    else
        echo "  [$TOTAL] KILLED: $desc"
        KILLED=$((KILLED + 1))
    fi

    cp "$BACKUP" "$CUBE_H"
}

echo "=== Mutation Testing: BlockFlag ==="
echo ""

run_mutant "AIR: BF_TRANSPARENT -> BF_SOLID" \
    "/* AIR       */ BF_TRANSPARENT," \
    "/* AIR       */ BF_SOLID,"

run_mutant "WATER: add BF_SOLID" \
    "/* WATER     */ BF_TRANSPARENT | BF_LIQUID," \
    "/* WATER     */ BF_TRANSPARENT | BF_LIQUID | BF_SOLID,"

run_mutant "LEAVES: remove BF_TRANSLUCENT" \
    "/* LEAVES    */ BF_SOLID | BF_TRANSLUCENT," \
    "/* LEAVES    */ BF_SOLID,"

run_mutant "LEAVES: add BF_OPAQUE" \
    "/* LEAVES    */ BF_SOLID | BF_TRANSLUCENT," \
    "/* LEAVES    */ BF_SOLID | BF_TRANSLUCENT | BF_OPAQUE,"

run_mutant "DIRT: remove BF_SOLID" \
    "/* DIRT      */ BF_SOLID | BF_OPAQUE," \
    "/* DIRT      */ BF_OPAQUE,"

run_mutant "WATER: add BF_OPAQUE" \
    "/* WATER     */ BF_TRANSPARENT | BF_LIQUID," \
    "/* WATER     */ BF_TRANSPARENT | BF_LIQUID | BF_OPAQUE,"

run_mutant "Out-of-range: BF_NONE -> BF_SOLID" \
    "return BF_NONE;" \
    "return BF_SOLID;"

run_mutant "hasFlag: invert logic" \
    "!= 0;" \
    "== 0;"

run_mutant "STONE: solid+opaque -> transparent" \
    "/* STONE     */ BF_SOLID | BF_OPAQUE," \
    "/* STONE     */ BF_TRANSPARENT,"

run_mutant "WATER: remove BF_LIQUID" \
    "/* WATER     */ BF_TRANSPARENT | BF_LIQUID," \
    "/* WATER     */ BF_TRANSPARENT,"

echo ""
echo "=== Results ==="
echo "Total:    $TOTAL"
echo "Killed:   $KILLED"
echo "Survived: $SURVIVED"
echo "Score:    $KILLED/$TOTAL ($(( KILLED * 100 / TOTAL ))%)"

cp "$BACKUP" "$CUBE_H"
rm "$BACKUP"
