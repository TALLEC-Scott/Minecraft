#!/bin/bash
set -e

VERSION="$1"
if [ -z "$VERSION" ]; then
    echo "Usage: ./release.sh <version>  (e.g. 0.3.1)"
    exit 1
fi

echo "=== Releasing v${VERSION} ==="

# Update version strings
sed -i "s/\"v[0-9]*\.[0-9]*\.[0-9]*\"/\"v${VERSION}\"/" src/menu.cpp
sed -i "s/APP_VERSION = \"[0-9]*\.[0-9]*\.[0-9]*\"/APP_VERSION = \"${VERSION}\"/" web/shell.html
sed -i "s/favicon.png?v=[0-9]*\.[0-9]*\.[0-9]*/favicon.png?v=${VERSION}/" web/shell.html

# Build desktop
echo "Building desktop..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_CLANG_TIDY=OFF > /dev/null 2>&1
make -C build -j$(nproc) > /dev/null 2>&1

# Run tests
echo "Running tests..."
./build/tests --gtest_brief=1

# Build web
echo "Building web..."
source "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" 2>/dev/null
rm -rf build_web
emcmake cmake -B build_web -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
emmake make -C build_web -j$(nproc) > /dev/null 2>&1
cp web/favicon.png build_web/

# Git commit and tag
echo "Committing v${VERSION}..."
git add -A
git commit -m "Release v${VERSION}"
git tag "v${VERSION}"
git push origin main --tags

# Deploy to server
echo "Deploying to server..."
rsync -az build_web/minecraft.{html,js,wasm,data} build_web/favicon.png tallec-net:~/minecraft-web/

echo ""
echo "=== v${VERSION} released ==="
echo "Desktop: ./build/minecraft"
echo "Web: tallec.freeboxos.fr:7862/minecraft.html"
