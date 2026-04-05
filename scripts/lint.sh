#!/bin/bash
# Run clang-format and clang-tidy on the project
set -e
cd "$(dirname "$0")/.."

# Ensure compile_commands.json exists
if [ ! -f build/compile_commands.json ]; then
    echo "Generating compile_commands.json..."
    cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null 2>&1
fi

SRC_FILES=$(find src include -name '*.cpp' -o -name '*.h' | grep -v Libraries)

echo "=== clang-format check ==="
FORMAT_ISSUES=0
for f in $SRC_FILES; do
    if ! clang-format --dry-run --Werror "$f" 2>/dev/null; then
        echo "  Format issue: $f"
        FORMAT_ISSUES=$((FORMAT_ISSUES + 1))
    fi
done
if [ $FORMAT_ISSUES -eq 0 ]; then
    echo "  All files formatted correctly."
else
    echo "  $FORMAT_ISSUES file(s) need formatting. Run: clang-format -i src/*.cpp include/*.h"
fi

echo ""
echo "=== clang-tidy ==="
CPP_FILES=$(find src -name '*.cpp' ! -name 'glad.c' ! -name 'stb_image.cpp')
clang-tidy -p build $CPP_FILES 2>&1 | grep -E "warning:|error:" | head -30
echo "Done."
