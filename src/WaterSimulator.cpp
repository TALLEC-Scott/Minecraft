#include "WaterSimulator.h"
#include <miniaudio.h>
#include "tracy_shim.h"
#include "world.h"

static constexpr int HDIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

WaterSimulator::~WaterSimulator() {
    if (flowSound) {
        if (audioLoaded) ma_sound_uninit(flowSound);
        delete flowSound;
    }
    if (ambientFlowSound) {
        if (audioLoaded) ma_sound_uninit(ambientFlowSound);
        delete ambientFlowSound;
    }
}

void WaterSimulator::initAudio(ma_engine* engine) {
    if (!engine) return;
    // CA transition sound (short bursts when blocks change)
    if (!flowSound) flowSound = new ma_sound{};
    ma_sound_init_from_file(engine, "assets/Sounds/liquid/water.wav", MA_SOUND_FLAG_DECODE, nullptr, nullptr, flowSound);
    ma_sound_set_looping(flowSound, MA_FALSE);
    ma_sound_set_volume(flowSound, 0.5f);
    // Ambient loop — plays continuously when player is near flowing water
    if (!ambientFlowSound) ambientFlowSound = new ma_sound{};
    ma_sound_init_from_file(engine, "assets/Sounds/liquid/water.wav", MA_SOUND_FLAG_DECODE, nullptr, nullptr, ambientFlowSound);
    ma_sound_set_looping(ambientFlowSound, MA_TRUE);
    ma_sound_set_volume(ambientFlowSound, 0.0f);
    ma_sound_start(ambientFlowSound);
    audioLoaded = true;
}

void WaterSimulator::updateAmbient(glm::vec3 playerPos) {
    if (!audioLoaded || !ambientFlowSound) return;
    int px = (int)std::floor(playerPos.x);
    int py = (int)std::floor(playerPos.y);
    int pz = (int)std::floor(playerPos.z);
    constexpr int RADIUS = 8;
    constexpr int RADIUS_SQ = RADIUS * RADIUS;
    WorldResolver resolver(world->chunkManager);
    int closestDistSq = RADIUS_SQ + 1;
    for (int dx = -RADIUS; dx <= RADIUS; dx += 2) {
        for (int dz = -RADIUS; dz <= RADIUS; dz += 2) {
            for (int dy = -4; dy <= 4; dy += 2) {
                int distSq = dx * dx + dy * dy + dz * dz;
                if (distSq >= closestDistSq) continue;
                int wy = py + dy;
                if (wy < 0 || wy >= CHUNK_HEIGHT) continue;
                auto loc = resolver.local(px + dx, pz + dz);
                if (!loc.chunk || loc.chunk->getBlockType(loc.lx, wy, loc.lz) != WATER) continue;
                if (waterIsSource(loc.chunk->getWaterLevel(loc.lx, wy, loc.lz))) continue;
                closestDistSq = distSq;
            }
        }
    }
    float targetVol = (closestDistSq < RADIUS_SQ) ? 0.8f * (1.0f - std::sqrt((float)closestDistSq) / RADIUS) : 0.0f;
    // Smooth volume transitions to avoid popping
    float blend = (targetVol > ambientVolume) ? 0.08f : 0.04f; // fade in faster than fade out
    ambientVolume += (targetVol - ambientVolume) * blend;
    if (ambientVolume < 0.005f) ambientVolume = 0.0f;
    ma_sound_set_volume(ambientFlowSound, ambientVolume);
}

void WaterSimulator::activate(int x, int y, int z) {
    nextActive.insert(glm::ivec3(x, y, z));
}

void WaterSimulator::activateNeighbors(int x, int y, int z) {
    for (auto& d : DIRS_6) {
        nextActive.insert(glm::ivec3(x + d[0], y + d[1], z + d[2]));
    }
}

void WaterSimulator::tick() {
    if (!forceNextTick) {
        auto now = std::chrono::steady_clock::now();
        if (lastTickTime.time_since_epoch().count() == 0) { lastTickTime = now; return; }
        double elapsed = std::chrono::duration<double>(now - lastTickTime).count();
        if (elapsed < TICK_SECONDS) return;
        lastTickTime = now;
    }
    forceNextTick = false;
    ZoneScopedN("WaterSimulator::tick");
    TracyPlot("water_active", (int64_t)activeBlocks.size());

    activeBlocks.swap(nextActive);
    nextActive.clear();
    // Rule 1/2 each insert up to 5 neighbors into nextActive and every
    // decay adds 6 — reserve once to avoid rehashes mid-tick.
    nextActive.reserve(activeBlocks.size() * 4 + 64);

    WorldResolver resolver(world->chunkManager);
    tickCells.clear();
    tickCells.reserve(activeBlocks.size());

    // Pre-pass: collect each active cell's chunk + local coords + raw byte,
    // and decide whether flowing cells lose support THIS TICK (start-of-tick
    // state, no in-pass cascading). Decaying cells are marked and will skip
    // Rule 1/2 in the main pass — that skip is what prevents the period-2
    // checkerboard along the decay front.
    for (const auto& pos : activeBlocks) {
        int x = pos.x, y = pos.y, z = pos.z;
        if (y < 0 || y >= CHUNK_HEIGHT) continue;
        auto loc = resolver.local(x, z);
        if (!loc.chunk || loc.chunk->getBlockType(loc.lx, y, loc.lz) != WATER) continue;
        uint8_t raw = loc.chunk->getWaterLevel(loc.lx, y, loc.lz);

        WaterTickCell cell{pos, loc.chunk, loc.lx, loc.lz, raw, false};

        if (!waterIsSource(raw) && !waterIsFalling(raw)) {
            uint8_t lvl = waterFlowLevel(raw);
            bool supported = false;
            if (y + 1 < CHUNK_HEIGHT && loc.chunk->getBlockType(loc.lx, y + 1, loc.lz) == WATER) {
                uint8_t araw = loc.chunk->getWaterLevel(loc.lx, y + 1, loc.lz);
                if (waterIsSource(araw) || waterIsFalling(araw)) supported = true;
                else if (waterFlowLevel(araw) < lvl) supported = true;
            }
            if (!supported) {
                for (auto& hd : HDIRS) {
                    auto n = resolver.local(x + hd[0], z + hd[1]);
                    if (!n.chunk || n.chunk->getBlockType(n.lx, y, n.lz) != WATER) continue;
                    uint8_t nraw = n.chunk->getWaterLevel(n.lx, y, n.lz);
                    if (waterIsFalling(nraw)) { supported = true; break; }
                    if (waterFlowLevel(nraw) < lvl) { supported = true; break; }
                }
            }
            cell.willDecay = !supported;
        }

        tickCells.push_back(cell);
    }

    int processed = 0;
    bool anyChanged = false;

    for (const auto& cell : tickCells) {
        if (processed >= MAX_BLOCKS_PER_TICK) {
            nextActive.insert(cell.pos);
            continue;
        }
        if (cell.willDecay) continue;

        int x = cell.pos.x, y = cell.pos.y, z = cell.pos.z;
        Chunk* chunk = cell.chunk;
        int lx = cell.lx, lz = cell.lz;
        uint8_t raw = cell.raw;
        uint8_t level = waterFlowLevel(raw);
        bool falling = waterIsFalling(raw);
        processed++;

        // Rule 1: gravity. The block below is marked with WATER_FALLING_FLAG
        // so it cannot become a source when its chain above breaks.
        if (y > 0) {
            block_type belowType = chunk->getBlockType(lx, y - 1, lz);
            if (belowType == AIR) {
                world->setBlock(x, y - 1, z, WATER, WATER_FALLING_FLAG);
                activate(x, y - 1, z);
                activate(x, y, z);
                anyChanged = true;
                continue;
            }
        }

        // "landed" = below is solid, world floor, or non-falling pool.
        // Air or falling water below means mid-column / over a cliff —
        // horizontal spread must not fan out sideways from there.
        bool landed = false;
        if (y == 0) {
            landed = true;
        } else {
            block_type belowType = chunk->getBlockType(lx, y - 1, lz);
            if (belowType == AIR) {
                landed = false;
            } else if (belowType == WATER) {
                uint8_t belowRaw = chunk->getWaterLevel(lx, y - 1, lz);
                landed = !waterIsFalling(belowRaw);
            } else {
                landed = true;
            }
        }

        // Rule 0: infinite water source. A non-source, non-falling water
        // cell with 2+ horizontal source neighbors becomes a source itself.
        // This is how oceans refill when you break a block. Excluding falling
        // water prevents cascading waterfalls from being upgraded to sources
        // when they land in an existing pool.
        if (!waterIsSource(raw) && !falling && landed) {
            int sourceCount = 0;
            for (auto& hd : HDIRS) {
                auto n = resolver.local(x + hd[0], z + hd[1]);
                if (!n.chunk) continue;
                if (n.chunk->getBlockType(n.lx, y, n.lz) != WATER) continue;
                if (waterIsSource(n.chunk->getWaterLevel(n.lx, y, n.lz))) sourceCount++;
            }
            if (sourceCount >= 2) {
                world->setBlock(x, y, z, WATER, 0);
                activate(x, y, z);
                anyChanged = true;
                continue;
            }
        }

        // Rule 2: horizontal spread. Falling water spreads only when it
        // lands on solid ground (source-like fan-out). When it lands on
        // an existing pool, it dissipates into the pool — no extra water
        // layer above the pool surface.
        bool solidBelow = (y > 0) && chunk->getBlockType(lx, y - 1, lz) != AIR
                                  && chunk->getBlockType(lx, y - 1, lz) != WATER;
        bool canSpreadFalling = falling && solidBelow;
        uint8_t spreadLevel = falling ? 0 : level;
        bool allowSpread = landed && (falling ? canSpreadFalling : true);
        if (spreadLevel < WATER_MAX_FLOW && allowSpread) {
            for (auto& hd : HDIRS) {
                int nx = x + hd[0], nz = z + hd[1];
                auto n = resolver.local(nx, nz);
                if (!n.chunk) continue;
                block_type nbt = n.chunk->getBlockType(n.lx, y, n.lz);
                if (nbt == AIR) {
                    world->setBlock(nx, y, nz, WATER, uint8_t(spreadLevel + 1));
                    activate(nx, y, nz);
                    anyChanged = true;
                } else if (nbt == WATER) {
                    uint8_t nraw = n.chunk->getWaterLevel(n.lx, y, n.lz);
                    if (waterIsSource(nraw) || waterIsFalling(nraw)) continue;
                    if (waterFlowLevel(nraw) > spreadLevel + 1) {
                        world->setBlock(nx, y, nz, WATER, uint8_t(spreadLevel + 1));
                        activate(nx, y, nz);
                        anyChanged = true;
                    }
                }
            }
        }

        // Rule 3 (falling only): flowing decay was decided in the pre-pass.
        if (falling) {
            bool aboveIsWater = y + 1 < CHUNK_HEIGHT &&
                                chunk->getBlockType(lx, y + 1, lz) == WATER;
            if (!aboveIsWater) {
                world->setBlock(x, y, z, AIR, 0);
                activateNeighbors(x, y, z);
                anyChanged = true;
            }
        }
    }

    // Apply pre-pass decays. Each decay adds 6 neighbor inserts to
    // nextActive, so grow the reserve before the loop.
    size_t decayCount = 0;
    for (const auto& cell : tickCells) if (cell.willDecay) decayCount++;
    if (decayCount > 0) {
        nextActive.reserve(nextActive.size() + decayCount * 6);
        for (const auto& cell : tickCells) {
            if (!cell.willDecay) continue;
            world->setBlock(cell.pos.x, cell.pos.y, cell.pos.z, AIR, 0);
            activateNeighbors(cell.pos.x, cell.pos.y, cell.pos.z);
            anyChanged = true;
        }
    }

    if (audioLoaded && flowSound) {
        if (anyChanged && !ma_sound_is_playing(flowSound)) {
            ma_sound_set_looping(flowSound, MA_TRUE);
            ma_sound_seek_to_pcm_frame(flowSound, 0);
            ma_sound_start(flowSound);
        } else if (!anyChanged && activeBlocks.empty() && nextActive.empty()) {
            ma_sound_set_looping(flowSound, MA_FALSE);
        }
    }
}
