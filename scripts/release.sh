#!/bin/bash
# Full release flow. Called directly with a version arg or via
# `./dev.sh release X.Y.Z` / `./dev.sh bump patch`.
set -e

VERSION="$1"
if [ -z "$VERSION" ]; then
    echo "Usage: ./scripts/release.sh <version>  (e.g. 0.7.3)"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$SCRIPT_DIR"

echo "=== Releasing v${VERSION} ==="
# Expects version strings in src/menu.cpp + web/shell.html to already be at
# VERSION. `./dev.sh release <ver>` and `./dev.sh bump <level>` call
# bump_version_strings before delegating here.

echo "Building desktop (release, clang-tidy off)..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_CLANG_TIDY=OFF > /dev/null 2>&1
make -C build -j"$(nproc)" > /dev/null 2>&1

echo "Running tests via dev.sh..."
./dev.sh test

echo "Building web via dev.sh..."
./dev.sh build-web > /dev/null 2>&1
cp web/favicon.png build_web/

echo "Committing v${VERSION}..."
git add -A
git commit -m "Release v${VERSION}"
git tag "v${VERSION}"
git push origin main --tags

echo "Deploying to server..."
rsync -az build_web/minecraft.{html,js,wasm,data} build_web/favicon.png tallec-net:~/minecraft-web/

echo ""
echo "=== v${VERSION} released ==="
echo "Desktop: ./build/minecraft"
echo "Web: tallec.freeboxos.fr:7862/minecraft.html"
