#include "world.h"
#include "profiler.h"
#include <array>
#include <algorithm>
#include <cmath>

// Extract 6 frustum planes from a combined projection*view matrix (Gribb-Hartmann).
// Each plane is vec4(normal.xyz, distance). A point p is inside if dot(plane.xyz, p) + plane.w >= 0.
static std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& m) {
    return {{
        // Left, Right, Bottom, Top, Near, Far
        glm::vec4(m[0][3]+m[0][0], m[1][3]+m[1][0], m[2][3]+m[2][0], m[3][3]+m[3][0]),
        glm::vec4(m[0][3]-m[0][0], m[1][3]-m[1][0], m[2][3]-m[2][0], m[3][3]-m[3][0]),
        glm::vec4(m[0][3]+m[0][1], m[1][3]+m[1][1], m[2][3]+m[2][1], m[3][3]+m[3][1]),
        glm::vec4(m[0][3]-m[0][1], m[1][3]-m[1][1], m[2][3]-m[2][1], m[3][3]-m[3][1]),
        glm::vec4(m[0][3]+m[0][2], m[1][3]+m[1][2], m[2][3]+m[2][2], m[3][3]+m[3][2]),
        glm::vec4(m[0][3]-m[0][2], m[1][3]-m[1][2], m[2][3]-m[2][2], m[3][3]-m[3][2]),
    }};
}

// Test an AABB against the frustum. Returns false if the box is completely outside any plane.
static bool aabbInFrustum(const std::array<glm::vec4, 6>& planes, glm::vec3 minP, glm::vec3 maxP) {
    for (auto& p : planes) {
        // Pick the positive vertex (corner most in the direction of the plane normal)
        glm::vec3 pv(p.x >= 0 ? maxP.x : minP.x,
                     p.y >= 0 ? maxP.y : minP.y,
                     p.z >= 0 ? maxP.z : minP.z);
        if (p.x * pv.x + p.y * pv.y + p.z * pv.z + p.w < 0)
            return false;
    }
    return true;
}

World::World(unsigned int seed) {
    this->terrainGenerator = new TerrainGenerator(seed, 0.1, 0, CHUNK_HEIGHT);
    this->chunkManager = new ChunkManager(RENDER_DISTANCE, CHUNK_SIZE, *terrainGenerator);
}

void World::destroyBlock(glm::vec3 position) const {
    int bx = (int)std::floor(position.x);
    int by = (int)std::floor(position.y);
    int bz = (int)std::floor(position.z);
    if (by < 0 || by >= CHUNK_HEIGHT) return;

    int chunkX = (bx >= 0) ? bx / CHUNK_SIZE : (bx - CHUNK_SIZE + 1) / CHUNK_SIZE;
    int chunkZ = (bz >= 0) ? bz / CHUNK_SIZE : (bz - CHUNK_SIZE + 1) / CHUNK_SIZE;
    int lx = bx - chunkX * CHUNK_SIZE;
    int lz = bz - chunkZ * CHUNK_SIZE;

    auto chunk = this->chunkManager->getChunk(chunkX, chunkZ);
    if (chunk) chunk->destroyBlock(lx, by, lz);
}

World::~World() {
    delete this->terrainGenerator;
    delete this->chunkManager;
}

Chunk* World::getChunk(int x, int y) {
    return this->chunkManager->getChunk(x, y);
}

Cube* World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT)
        return nullptr;

    int chunkX = (x >= 0) ? x / CHUNK_SIZE : (x - CHUNK_SIZE + 1) / CHUNK_SIZE;
    int chunkZ = (z >= 0) ? z / CHUNK_SIZE : (z - CHUNK_SIZE + 1) / CHUNK_SIZE;

    auto res = this->chunkManager->getChunk(chunkX, chunkZ);
    if (res == nullptr)
        return nullptr;

    return res->getBlock(x - chunkX * CHUNK_SIZE, y, z - chunkZ * CHUNK_SIZE);
}

int World::render(Shader shaderProgram, glm::mat4 viewProjection, glm::vec3 cameraPos) const {
    g_frame.meshBuildBudget = 8; // reset per frame
    auto planes = extractFrustumPlanes(viewProjection);

    // Collect visible chunks and sort front-to-back for early-Z rejection
    struct VisibleChunk {
        glm::ivec2 pos;
        Chunk* chunk;
        float distSq;
    };
    std::vector<VisibleChunk> visible;
    visible.reserve(chunkManager->chunks.size());

    for (auto& [pos, chunk] : chunkManager->chunks) {
        glm::vec3 minP(pos.x * CHUNK_SIZE, 0, pos.y * CHUNK_SIZE);
        glm::vec3 maxP(minP.x + CHUNK_SIZE, CHUNK_HEIGHT, minP.z + CHUNK_SIZE);
        if (!aabbInFrustum(planes, minP, maxP)) continue;
        // Distance from camera to chunk center (squared, skip sqrt)
        glm::vec3 center(minP.x + CHUNK_SIZE * 0.5f, CHUNK_HEIGHT * 0.5f, minP.z + CHUNK_SIZE * 0.5f);
        glm::vec3 d = center - cameraPos;
        visible.push_back({pos, const_cast<Chunk*>(&chunk), d.x*d.x + d.y*d.y + d.z*d.z});
    }

    std::sort(visible.begin(), visible.end(),
              [](const VisibleChunk& a, const VisibleChunk& b) { return a.distSq < b.distSq; });

    // Pass 1: opaque geometry (front-to-back)
    int rendered = 0;
    for (auto& vc : visible) {
        Chunk* nx_neg = chunkManager->getChunk(vc.pos.x - 1, vc.pos.y);
        Chunk* nx_pos = chunkManager->getChunk(vc.pos.x + 1, vc.pos.y);
        Chunk* nz_neg = chunkManager->getChunk(vc.pos.x, vc.pos.y - 1);
        Chunk* nz_pos = chunkManager->getChunk(vc.pos.x, vc.pos.y + 1);
        vc.chunk->render(shaderProgram, nx_neg, nx_pos, nz_neg, nz_pos);
        rendered++;
        g_frame.chunksRendered++;
    }

    // Pass 2: water back-to-front for correct transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = (int)visible.size() - 1; i >= 0; i--) {
        auto& vc = visible[i];
        Chunk* nx_neg = chunkManager->getChunk(vc.pos.x - 1, vc.pos.y);
        Chunk* nx_pos = chunkManager->getChunk(vc.pos.x + 1, vc.pos.y);
        Chunk* nz_neg = chunkManager->getChunk(vc.pos.x, vc.pos.y - 1);
        Chunk* nz_pos = chunkManager->getChunk(vc.pos.x, vc.pos.y + 1);
        vc.chunk->renderWater(shaderProgram, nx_neg, nx_pos, nz_neg, nz_pos);
    }
    glDisable(GL_BLEND);

    return rendered;
}

void World::update(glm::vec3 cameraPosition) const {
    this->chunkManager->update(cameraPosition);
}

bool World::raycast(glm::vec3 origin, glm::vec3 direction, float maxDist, glm::ivec3& hitPos) const {
    // DDA voxel traversal
    direction = glm::normalize(direction);
    glm::ivec3 pos = glm::ivec3(std::floor(origin.x), std::floor(origin.y), std::floor(origin.z));
    glm::ivec3 step;
    glm::vec3 tMax, tDelta;

    for (int i = 0; i < 3; i++) {
        if (direction[i] > 0) {
            step[i] = 1;
            tMax[i] = ((pos[i] + 1) - origin[i]) / direction[i];
        } else if (direction[i] < 0) {
            step[i] = -1;
            tMax[i] = (pos[i] - origin[i]) / direction[i];
        } else {
            step[i] = 0;
            tMax[i] = 1e30f;
        }
        tDelta[i] = (direction[i] != 0) ? std::abs(1.0f / direction[i]) : 1e30f;
    }

    float dist = 0;
    while (dist < maxDist) {
        // Check current block
        if (pos.y >= 0 && pos.y < CHUNK_HEIGHT) {
            int chunkX = (pos.x >= 0) ? pos.x / CHUNK_SIZE : (pos.x - CHUNK_SIZE + 1) / CHUNK_SIZE;
            int chunkZ = (pos.z >= 0) ? pos.z / CHUNK_SIZE : (pos.z - CHUNK_SIZE + 1) / CHUNK_SIZE;
            Chunk* chunk = chunkManager->getChunk(chunkX, chunkZ);
            if (chunk) {
                int lx = pos.x - chunkX * CHUNK_SIZE;
                int lz = pos.z - chunkZ * CHUNK_SIZE;
                Cube* block = chunk->getBlock(lx, pos.y, lz);
                if (block && block->getType() != AIR && block->getType() != WATER) {
                    hitPos = pos;
                    return true;
                }
            }
        }

        // Step to next voxel boundary
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            dist = tMax.x;
            tMax.x += tDelta.x;
            pos.x += step.x;
        } else if (tMax.y < tMax.z) {
            dist = tMax.y;
            tMax.y += tDelta.y;
            pos.y += step.y;
        } else {
            dist = tMax.z;
            tMax.z += tDelta.z;
            pos.z += step.z;
        }
    }
    return false;
}
