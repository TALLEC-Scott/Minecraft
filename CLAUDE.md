# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
mkdir build && cd build
cmake ..
make
./Minecraft
```

Requires: OpenGL, GLFW 3.3+, GLM, stb_image (header-only), GLAD. C++23, CMake 3.25+.

The Visual Studio solution (`POGL.sln`) is also present for Windows development.

## Architecture

This is a C++ OpenGL Minecraft-like terrain renderer. The world is divided into chunks, generated procedurally using Perlin noise.

**Object hierarchy:**

```
World
└── ChunkManager           — loads/unloads chunks based on camera position
    ├── Chunk[]             — 5×5×5 block grids
    │   └── Cube[]          — individual blocks with mesh data
    └── TerrainGenerator    — wraps perlin_noise to produce height maps
Camera                     — player movement, gravity, FPS controls
Shader / Texture           — OpenGL program and texture management
```

**Key constants** (`cube.h`):
- `CHUNK_SIZE = 5` — blocks per chunk edge
- `RENDER_DISTANCE = 3` — chunk radius loaded around the player
- Block types: `AIR, GRASS, DIRT, STONE, COAL_ORE, BEDROCK, WATER, SAND, GLOWSTONE`

**Block generation logic** (`chunk.cpp`): bottom layer → BEDROCK, lower third → STONE (coal ore variation), middle → DIRT, top surface → GRASS, water-adjacent → SAND, above terrain → AIR or WATER.

**Rendering pipeline** (`Shaders/vert.shd`, `Shaders/frag.shd`):
- Vertex shader: MVP transform, passes normals/UVs to fragment stage
- Fragment shader: Phong lighting (ambient 0.1, diffuse 1.5, specular 0.2 / shininess 32)
- Rotating light source simulates a day/night cycle; sky color interpolates between noon blue `(0.2, 0.6, 1.0)` and dusk `(0.15, 0.15, 0.25)`

**Input** (`main.cpp`): WASD move, SPACE up, Q down, R/Shift sprint, G toggle gravity, J toggle day/night, X wireframe, F12 fullscreen, Left Click break block.
