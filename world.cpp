#include "world.h"
#include <array>

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

World::World() {
    this->terrainGenerator = new TerrainGenerator(0, 0.1, 0, CHUNK_SIZE + 1);
    this->chunkManager = new ChunkManager(RENDER_DISTANCE, CHUNK_SIZE, *terrainGenerator);
}

void World::destroyBlock(glm::vec3 position) const {
    int chunkX = (int)position.x / CHUNK_SIZE;
    int chunkZ = (int)position.z / CHUNK_SIZE;

    int x_chunk = (int)position.x % CHUNK_SIZE;
    int y_chunk = (int)position.y % CHUNK_SIZE;
    int z_chunk = (int)position.z % CHUNK_SIZE;

    auto chunk = this->chunkManager->getChunk(chunkX, chunkZ);
    if (chunk) chunk->destroyBlock(x_chunk, y_chunk, z_chunk);
}

World::~World() {
    delete this->terrainGenerator;
    delete this->chunkManager;
}

Chunk* World::getChunk(int x, int y) {
    return this->chunkManager->getChunk(x, y);
}

Cube* World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_SIZE)
        return nullptr;
    if (x < 0 || x >= CHUNK_SIZE * RENDER_DISTANCE || z < 0 || z >= CHUNK_SIZE * RENDER_DISTANCE)
        return nullptr;

    int chunkX = x / CHUNK_SIZE;
    int chunkZ = z / CHUNK_SIZE;

    if (chunkX < 0 || chunkX >= RENDER_DISTANCE || chunkZ < 0 || chunkZ >= RENDER_DISTANCE)
        return nullptr;

    auto res = this->chunkManager->getChunk(chunkX, chunkZ);
    if (res == nullptr)
        return nullptr;

    return res->getBlock(x % CHUNK_SIZE, y % CHUNK_SIZE, z % CHUNK_SIZE);
}

int World::render(Shader shaderProgram, glm::mat4 viewProjection) const {
    auto planes = extractFrustumPlanes(viewProjection);
    int rendered = 0;

    // Pass 1: opaque geometry
    for (auto& [pos, chunk] : this->chunkManager->chunks) {
        glm::vec3 minP(pos.x * CHUNK_SIZE, 0, pos.y * CHUNK_SIZE);
        glm::vec3 maxP(minP.x + CHUNK_SIZE, CHUNK_SIZE, minP.z + CHUNK_SIZE);
        if (!aabbInFrustum(planes, minP, maxP)) continue;
        chunk.render(shaderProgram);
        rendered++;
    }

    // Pass 2: water (after all opaque to preserve transparency)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (auto& [pos, chunk] : this->chunkManager->chunks) {
        glm::vec3 minP(pos.x * CHUNK_SIZE, 0, pos.y * CHUNK_SIZE);
        glm::vec3 maxP(minP.x + CHUNK_SIZE, CHUNK_SIZE, minP.z + CHUNK_SIZE);
        if (!aabbInFrustum(planes, minP, maxP)) continue;
        chunk.renderWater(shaderProgram);
    }
    glDisable(GL_BLEND);

    return rendered;
}

void World::update(glm::vec3 cameraPosition) const {
    this->chunkManager->update(cameraPosition);
}
