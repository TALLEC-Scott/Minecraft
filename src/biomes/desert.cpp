// Desert: sand dunes and cacti.

#include "biome_defs.h"
#include "tree_shapes.h"

namespace {

// Cactus: 12% of cells, 2..4 tall, on sand above the water line.
void decorate(const biomegen::FeatureSite& f) {
    if ((f.bits >> 20) % 100 >= 12) return;
    if (f.surface <= f.seaLevel || f.ground != SAND) return;
    const int h = 2 + static_cast<int>((f.bits >> 8) % 3);
    if (f.surface + h >= CHUNK_HEIGHT) return;
    for (int y = f.surface + 1; y <= f.surface + h; y++) f.set(y, CACTUS);
    f.claim();
}

} // namespace

const BiomeDef DESERT_DEF = {
    "Desert",
    SAND, SAND, SAND,
    0.0f, nullptr,
    &decorate,
    nullptr,
    {1.32f, 0.97f, 0.95f}, // grass tint
    {1.46f, 0.96f, 0.89f}, // foliage tint
};
