#pragma once

// GPU side of biome colours (biome_tint.h): two world-space textures, one
// texel per block column, holding the blended grass and foliage colour
// multipliers. Addressed toroidally (world x, z modulo SIZE), so chunks
// simply overwrite their 16×16 patch when they load; SIZE covers the
// largest render distance without wrapping onto itself. frag.shd reads them
// with texelFetch on texture units 1 and 2. Columns never written read as
// 128 = multiplier 1 (stock colours).

#include "biome_tint.h"

namespace BiomeTintMap {

// (2 × 32 + 1) chunks at the maximum render distance = 1040 blocks; a
// multiple of CHUNK_SIZE so a chunk's patch never straddles the wrap.
inline constexpr int SIZE = 1056;
static_assert(SIZE % CHUNK_SIZE == 0 && SIZE >= (2 * 32 + 1) * CHUNK_SIZE);
inline constexpr int GRASS_UNIT = 1;
inline constexpr int FOLIAGE_UNIT = 2;

// Creates both textures and binds them to their units (leaves unit 0 active).
void init();
void upload(int chunkX, int chunkZ, const BiomeTints& tints);
void destroy();

} // namespace BiomeTintMap
