#include "mob_spawn.h"

#include <algorithm>
#include <cmath>

#include "collision.h"

namespace mobspawn {

int skyDarkenFromSunHeight(float sunHeight) {
    // Full daylight once the sun is a little above the horizon, full night
    // once it's a little below; linear ramp through dusk/dawn.
    float day = std::clamp(sunHeight * 4.0f + 0.5f, 0.0f, 1.0f);
    return static_cast<int>(std::lround((1.0f - day) * 11.0f));
}

bool biomeAllows(const MobInfo& info, Biome biome) {
    return (info.spawnBiomes & biomeBit(biome)) != 0;
}

bool passiveLightOk(int sky, int block) {
    return std::max(sky, block) >= PASSIVE_MIN_LIGHT;
}

bool hostileLightOk(int sky, int block, int skyDarken) {
    return std::max(block, sky - skyDarken) <= HOSTILE_MAX_LIGHT;
}

bool hasRoom(const MobBlockAccess& blocks, const MobInfo& info, int x, int y, int z) {
    if (y < 1 || y >= CHUNK_HEIGHT - 1) return false;
    auto solid = [&blocks](int bx, int by, int bz) {
        if (by < 0) return true;
        if (by >= CHUNK_HEIGHT) return false;
        if (!blocks.isLoaded(bx, bz)) return true;
        return hasFlag(blocks.blockAt(bx, by, bz), BF_SOLID);
    };
    glm::vec3 feet(static_cast<float>(x), static_cast<float>(y) - 0.5f, static_cast<float>(z));
    if (collidesAABB(feet, info.halfWidth, info.height, solid)) return false;
    int headY = static_cast<int>(std::floor(feet.y + info.height + 0.5f - 0.001f));
    for (int by = y; by <= headY; ++by)
        if (blocks.blockAt(x, by, z) == WATER) return false;
    return true;
}

bool canSpawnAt(const MobBlockAccess& blocks, const MobInfo& info, int x, int y, int z, int skyDarken) {
    if (info.spawnBiomes == 0) return false;
    if (!blocks.isLoaded(x, z)) return false;
    if (!biomeAllows(info, blocks.biomeAt(x, z))) return false;

    block_type ground = blocks.blockAt(x, y - 1, z);
    int sky = blocks.skyLightAt(x, y, z);
    int blk = blocks.blockLightAt(x, y, z);
    if (info.category == MobCategory::Passive) {
        if (ground != GRASS) return false;
        if (!passiveLightOk(sky, blk)) return false;
    } else {
        // Any full opaque block except bedrock (Minecraft rule); no leaves.
        if (!hasFlag(ground, BF_SOLID) || !hasFlag(ground, BF_OPAQUE) || ground == BEDROCK) return false;
        if (!hostileLightOk(sky, blk, skyDarken)) return false;
    }
    return hasRoom(blocks, info, x, y, z);
}

bool inSpawnRing(glm::vec3 player, glm::vec3 pos) {
    float d = glm::distance(player, pos);
    return d >= SPAWN_MIN_DIST && d <= SPAWN_MAX_DIST;
}

bool shouldDespawnHostile(float distToPlayer, float roll) {
    if (distToPlayer > DESPAWN_INSTANT_DIST) return true;
    return distToPlayer > DESPAWN_RANDOM_DIST && roll < DESPAWN_RANDOM_CHANCE;
}

std::vector<MobType> eligibleTypes(MobCategory category, Biome biome) {
    std::vector<MobType> out;
    const MobInfo* table = mobRegistry();
    for (int i = 0; i < MOB_TYPE_COUNT; ++i) {
        const MobInfo& info = table[i];
        if (info.category == category && info.spawnWeight > 0 && biomeAllows(info, biome)) out.push_back(info.type);
    }
    return out;
}

MobType pickWeighted(const std::vector<MobType>& candidates, float roll) {
    int total = 0;
    for (MobType t : candidates) total += mobInfo(t).spawnWeight;
    if (total <= 0) return MobType::Count;
    float target = roll * static_cast<float>(total);
    float acc = 0.0f;
    for (MobType t : candidates) {
        acc += static_cast<float>(mobInfo(t).spawnWeight);
        if (target < acc) return t;
    }
    return candidates.back();
}

} // namespace mobspawn
