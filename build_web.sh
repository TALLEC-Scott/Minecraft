#!/bin/bash
set -e

# Source Emscripten SDK
EMSDK_DIR="${EMSDK:-$HOME/emsdk}"
if [ -f "$EMSDK_DIR/emsdk_env.sh" ]; then
    source "$EMSDK_DIR/emsdk_env.sh"
else
    echo "Error: emsdk not found at $EMSDK_DIR"
    echo "Install: git clone https://github.com/emscripten-core/emsdk.git ~/emsdk && cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest"
    exit 1
fi

echo "=== Building for WebAssembly ==="
emcmake cmake -B build_web -DCMAKE_BUILD_TYPE=Release
emmake make -C build_web -j$(nproc)

echo ""
echo "=== Build complete ==="
echo "Output files:"
ls -lh build_web/minecraft.{html,js,wasm,data} 2>/dev/null
echo ""
echo "To run: python3 serve_web.py 8080   # sends the COOP/COEP headers pthreads need"
echo "Then open: http://localhost:8080/minecraft.html"
