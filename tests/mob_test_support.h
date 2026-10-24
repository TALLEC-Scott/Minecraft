// Shared fixtures for the mob tests: a map-backed MobBlockAccess and a
// goal-less test mob. Everything here is GL-free and world-free — mob
// physics / AI / spawn rules only ever see the world through
// MobBlockAccess + MobContext.
#pragma once

#include <cstdint>
#include <random>
#include <unordered_map>

#include "mob.h"
#include "mob_context.h"

class FakeBlocks : public MobBlockAccess {
  public:
    Biome biome = BIOME_PLAINS;
    int sky = 15;
    int blockLight = 0;
    bool loaded = true;

    void set(int x, int y, int z, block_type t) { cells[key(x, y, z)] = t; }
    // Fill an inclusive box.
    void fill(int x0, int y0, int z0, int x1, int y1, int z1, block_type t) {
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) set(x, y, z, t);
    }

    block_type blockAt(int x, int y, int z) const override {
        if (y < 0 || y >= CHUNK_HEIGHT) return AIR;
        auto it = cells.find(key(x, y, z));
        return it == cells.end() ? AIR : it->second;
    }
    bool isLoaded(int /*x*/, int /*z*/) const override { return loaded; }
    int skyLightAt(int /*x*/, int /*y*/, int /*z*/) const override { return sky; }
    int blockLightAt(int /*x*/, int /*y*/, int /*z*/) const override { return blockLight; }
    Biome biomeAt(int /*x*/, int /*z*/) const override { return biome; }

  private:
    static int64_t key(int x, int y, int z) {
        return (static_cast<int64_t>(x + 4096) << 40) | (static_cast<int64_t>(y + 256) << 20) |
               static_cast<int64_t>(z + 4096);
    }
    std::unordered_map<int64_t, block_type> cells;
};

// Mob with the Pig's registry row but no AI goals, so tests drive it
// deterministically through the steering API.
class TestMob : public Mob {
  public:
    TestMob() : Mob(MobType::Pig) {}
    const MobModel& model() const override {
        static const MobModel m = [] {
            MobModel mm;
            mm.add({"body", -1, {0, 0.5f, 0}, {0, 0, 0}, {0.4f, 0.4f, 0.4f}, {1, 1, 1}});
            return mm;
        }();
        return m;
    }
    MobPose pose(float /*partialTick*/) const override { return {}; }
    int ambientSoundInterval() const override { return 0; }
};

struct MobHarness {
    FakeBlocks blocks;
    std::mt19937 rng{12345u};
    MobContext ctx;
    MobHarness() {
        ctx.blocks = &blocks;
        ctx.rng = &rng;
    }
    // Stone floor whose top face is at y = 0.5 (block row y = 0).
    void floor(int radius = 12) { blocks.fill(-radius, 0, -radius, radius, 0, radius, STONE); }
    void run(Mob& m, int ticks) {
        for (int i = 0; i < ticks; ++i) m.tick(ctx);
    }
};
