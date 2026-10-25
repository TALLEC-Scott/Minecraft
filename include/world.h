#pragma once

#include <glm/glm.hpp>
#include "chunk.h"
#include "shader.h"
#include "TerrainGenerator.h"
#include "ChunkManager.h"
#include "WaterSimulator.h"
#include "light_propagation.h"
#include "world_resolver.h"

using WorldResolver = WorldResolverT<ChunkManager, Chunk>;

class EntityManager;
class BlockEffects;
class MobManager;
class ParticleSystem;

class World {
  public:
    World(unsigned int seed = 0);
    unsigned int getSeed() const { return seed; }

    // void render(Shader& shaderProgram);
    // Torches the removed block was holding pop off too; their positions
    // are appended to `popped` (the local player drops them as items).
    void destroyBlock(glm::vec3 position, std::vector<glm::ivec3>* popped = nullptr) const;
    // `state` is the cell's state byte (torch.h: standing / which wall);
    // a torch left at INVALID (the network path) picks one from its
    // surroundings, and isn't placed if nothing can hold it.
    void placeBlock(glm::ivec3 position, block_type type, uint8_t state = 0xFF) const;
    uint8_t torchStateAt(glm::ivec3 p) const;
    void setBlock(int x, int y, int z, block_type type, uint8_t waterLevel = 0) const;
    // Between these, setBlock defers its light updates and the end call
    // relights them all in one pass (WaterSimulator wraps each tick).
    void beginLightBatch() const { lightBatching = true; }
    void endLightBatch() const;
    Chunk* getChunk(int x, int y);
    Cube* getBlock(int x, int y, int z) const;

    // Replace a placed TNT block with a primed TNT entity. No-op if the
    // target isn't TNT (e.g. it was already ignited a frame ago).
    void igniteTnt(glm::ivec3 pos, double now) const;

    // Destroys a sphere of blocks, chain-primes TNT in radius with short
    // random fuses, damages + knocks back mobs and the player (via
    // MobManager::applyExplosion), spawns a smoke plume, and raises the
    // camera-shake magnitude based on distance to the player.
    void explode(glm::vec3 center, float power, double now) const;

    TerrainGenerator* terrainGenerator;
    ChunkManager* chunkManager;
    WaterSimulator* waterSimulator;
    EntityManager* entityManager;
    MobManager* mobManager; // living mobs + projectiles, 20 TPS (see mob_manager.h)
    ParticleSystem* particles;
    BlockEffects* blockEffects; // ambient block sounds + particles (lava, torches)

    // Camera-shake magnitude set by explode(); main.cpp decays it per-frame
    // and offsets the view. Mutable because the World pointer is shared via
    // `const World*` throughout the render path.
    mutable float cameraShake = 0.0f;
    // Last camera position observed by update() — used by explode() to
    // compute player knockback / shake distance without the caller having
    // to pass it again.
    mutable glm::vec3 lastCameraPos{0.0f};
    // Set by render(): every loaded chunk's visibleSections mask is valid.
    mutable bool caveCullValid = false;
    mutable bool lightBatching = false;
    mutable std::vector<BlockChange> pendingLight;

    ~World();

    unsigned int seed = 0;

    int render(const Shader& shaderProgram, glm::mat4 viewProjection, glm::vec3 cameraPos) const;
    // Did last render()'s cave-culling BFS reach the section holding this
    // block? Always true when cave culling didn't run that frame (disabled,
    // or the camera chunk wasn't loaded). Used to skip mobs in sealed caves.
    bool sectionVisible(glm::ivec3 block) const;
    // `dt` drives the 20-TPS entity tick accumulator; `now` is used for the
    // visual-flash phase on primed TNT.
    void update(glm::vec3 cameraPosition, float dt, double now) const;
    // Raycast: returns true if a block was hit, sets hitPos to the block and prevPos to the air block before it
    bool raycast(glm::vec3 origin, glm::vec3 direction, float maxDist, glm::ivec3& hitPos, glm::ivec3& prevPos) const;
};