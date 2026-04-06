# Minecraft Clone in C++/OpenGL

A Minecraft-like voxel terrain renderer built with C++ and OpenGL 3.3 Core Profile.

## Features

- Procedural terrain with multi-octave Perlin noise (for heights and biomes)
- Greedy meshing (optional, behind feature flag due to AO artifacts)
- Biome system (ocean, beach, plains, forest, desert, tundra) 
- Per-vertex ambient occlusion
- Sky light with flood-fill propagation (Minecraft-style shadows)
- Directional sun with day/night cycle
- Frustum culling with front-to-back chunk sorting
- Block breaking with raycast targeting
- First-person arm with punch animation
- Walk mode with gravity, jump, collision (double-tap SPACE to toggle)
- Clouds, sun, moon billboards
- Frame profiler with GPU timer queries

## Requirements

- C++20, CMake 3.25+
- OpenGL 3.3+, GLFW 3.3+, GLM, stb_image (header-only), GLAD

On Debian/Ubuntu:
```bash
sudo apt-get install libglfw3-dev libgl1-mesa-dev
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
make -C build -j$(nproc)
./build/minecraft              # run from project root (asset paths are relative)
```

### Options

```bash
./build/minecraft --seed 42                    # specific world seed
./benchmark.sh "label" --headless --seed=42    # headless benchmark
cmake -B build -DENABLE_CLANG_TIDY=OFF         # disable linter for faster builds
cmake -B build -DBUILD_TESTS=OFF               # skip test compilation
```

## Tests

```bash
make -C build tests
./build/tests
```

## Controls

- **WASD** — move
- **SPACE** — fly up / jump (walk mode)
- **Q** — fly down
- **R / SHIFT** — sprint
- **Double-tap SPACE** — toggle walk/fly mode
- **Left Click** — break block
- **X** — toggle wireframe
- **J** — toggle day/night cycle
- **F12** — toggle fullscreen
- **ESC** — quit

## Project Structure

```
src/          — C++ source files
include/      — headers
assets/       — Shaders/, Textures/
scripts/      — mapgen, check_biomes, lint, build tools
tests/        — GoogleTest unit tests
Libraries/    — third-party (GLM, GLAD, stb)
```

## Benchmarking

```bash
./benchmark.sh "label"                # windowed benchmark
./benchmark.sh "label" --headless     # headless (skip WSL2 swap overhead)
```

Outputs frame time breakdown (update/render/swap), GPU timer, mesh build stats, and geometry counts.

## Potential Performance Optimizations

### Greedy Meshing with Shader-Based Lighting

Currently greedy meshing is disabled (`GREEDY_MESHING 0` in `cube.h`) because merging adjacent faces into larger quads causes AO/shadow interpolation artifacts — dark streaks appear diagonally across merged faces where corner AO values differ.

Minecraft itself does not use greedy meshing for this same reason — per-vertex AO and sky light values interpolate incorrectly across large merged quads. It uses naive culled meshing instead (one quad per visible face).

A potential fix: compute AO and sky light **per-pixel in the fragment shader** instead of per-vertex interpolation. Two approaches:

1. **Fragment-based AO**: use `fract(FragPos)` to determine the pixel's position within the block and snap to the nearest corner's AO value, avoiding cross-block interpolation.

2. **3D light texture**: upload per-block sky light and AO as a 16×128×16 3D texture per chunk. The fragment shader samples it at `FragPos`, giving pixel-perfect lighting with zero interpolation artifacts.

Either approach would allow re-enabling greedy meshing (12x fewer triangles, 0.25ms GPU vs 2.07ms) while maintaining correct lighting. The tradeoff is shader complexity and potential texture memory/bandwidth cost.

## License

[MIT](https://choosealicense.com/licenses/mit/)

## Author

- [Scott TALLEC](https://github.com/TALLEC-Scott)
- [Justin JAECKER](https://github.com/Justinj68)
