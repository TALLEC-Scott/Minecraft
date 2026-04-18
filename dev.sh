#!/bin/bash
# dev.sh — one-stop dispatcher for common build / run / test / release
# tasks. Run from the project root.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

WIN_PS='powershell.exe -Command'
WIN_RUN='Push-Location '\''\\wsl.localhost\Ubuntu\home\scott\projects\Minecraft'\''; .\build_win\minecraft.exe'

usage() {
    cat <<EOF
Usage: ./dev.sh <command> [args]

Build:
  build              Desktop release build (cmake + make)
  build-debug        Desktop debug build (no optimizations, clang-tidy off)
  build-win          Windows cross-compile via MinGW (→ build_win/minecraft.exe)
  build-web          WebAssembly build via Emscripten (→ build_web/minecraft.html)
  build-all          All three targets

Run:
  run [args...]      ./build/minecraft
  run-win [args...]  .\build_win\minecraft.exe via powershell
  seed N             Desktop run with --seed N

Test:
  test [filter]      Run tests; optional --gtest_filter pattern
  test-all           Build + run every test

Benchmark (always uses Windows build — WSL2 GPU swap is abnormally slow):
  bench [label]      Windows headless benchmark with the given label

Web:
  serve              python3 -m http.server on build_web/
  dev-web            build-web + serve in one go

Release:
  release X.Y.Z           Release an explicit version
  release <major|minor|patch>  Auto-bump from the latest tag and release
  bump <major|minor|patch>     Alias for `release <level>`

Misc:
  clean              Remove build/, build_win/, build_web/
  clean-saves        Remove saves/ (keeps level.dat template)
  lint               Run scripts/lint.sh
EOF
}

bump_version_strings() {
    local v="$1"
    sed -i "s/\"v[0-9]*\.[0-9]*\.[0-9]*\"/\"v${v}\"/" src/menu.cpp
    sed -i "s/APP_VERSION = \"[0-9]*\.[0-9]*\.[0-9]*\"/APP_VERSION = \"${v}\"/" web/shell.html
    sed -i "s/favicon.png?v=[0-9]*\.[0-9]*\.[0-9]*/favicon.png?v=${v}/" web/shell.html
}

cmd="${1:-}"
shift || true

case "$cmd" in
    build)
        cmake -B build -DCMAKE_BUILD_TYPE=Release
        make -C build -j"$(nproc)"
        ;;
    build-debug)
        cmake -B build_debug -DCMAKE_BUILD_TYPE=Debug -DENABLE_CLANG_TIDY=OFF
        make -C build_debug -j"$(nproc)"
        ;;
    build-win)
        cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
        make -C build_win -j"$(nproc)"
        ;;
    build-web)
        ./build_web.sh
        ;;
    build-all)
        "$0" build
        "$0" build-win
        "$0" build-web
        ;;
    run)
        ./build/minecraft "$@"
        ;;
    run-win)
        # Pass-through extra args by interpolating into the powershell cmd.
        local_args=""
        for a in "$@"; do local_args="$local_args $a"; done
        $WIN_PS "$WIN_RUN$local_args"
        ;;
    seed)
        [ -z "$1" ] && { echo "Usage: ./dev.sh seed N"; exit 1; }
        ./build/minecraft --seed "$1"
        ;;
    test)
        make -C build tests -j"$(nproc)"
        if [ -n "$1" ]; then
            ./build/tests --gtest_filter="$1"
        else
            ./build/tests
        fi
        ;;
    test-all)
        "$0" build
        ./build/tests
        ;;
    bench)
        make -C build_win -j"$(nproc)"
        label="${1:-unlabeled}"
        $WIN_PS "$WIN_RUN --benchmark '$label' --headless 2>&1" | tail -40
        ;;
    serve)
        python3 -m http.server -d build_web 8080
        ;;
    dev-web)
        ./build_web.sh
        python3 -m http.server -d build_web 8080
        ;;
    release|bump)
        arg="${1:-}"
        if [ -z "$arg" ]; then
            echo "Usage: ./dev.sh $cmd <major|minor|patch|X.Y.Z>"
            exit 1
        fi
        case "$arg" in
            major|minor|patch)
                latest=$(git tag --sort=-v:refname | head -1 | sed 's/^v//')
                IFS='.' read -r maj min pat <<< "$latest"
                case "$arg" in
                    major) new="$((maj + 1)).0.0" ;;
                    minor) new="$maj.$((min + 1)).0" ;;
                    patch) new="$maj.$min.$((pat + 1))" ;;
                esac
                echo "Latest: v$latest -> new: v$new ($arg bump)"
                ;;
            *)
                new="$arg"
                ;;
        esac
        bump_version_strings "$new"
        ./scripts/release.sh "$new"
        ;;
    clean)
        rm -rf build build_debug build_win build_web
        ;;
    clean-saves)
        rm -rf saves
        ;;
    lint)
        ./scripts/lint.sh
        ;;
    ""|help|-h|--help)
        usage
        ;;
    *)
        echo "Unknown command: $cmd"
        usage
        exit 1
        ;;
esac
