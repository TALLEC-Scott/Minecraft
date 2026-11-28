#include "biome_tint_map.h"

#include <vector>

#include "gl_header.h"

namespace BiomeTintMap {

namespace {

GLuint grassTex = 0, foliageTex = 0;

GLuint makeTexture(int unit) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, t);
    // Neutral (128 = ×1) until a chunk writes its patch.
    std::vector<uint8_t> neutral(static_cast<size_t>(SIZE) * SIZE * 3, 128);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, SIZE, SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, neutral.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

int wrap(int v) {
    return ((v % SIZE) + SIZE) % SIZE;
}

void put(GLuint tex, int unit, int x0, int z0, const uint8_t (&src)[CHUNK_SIZE][CHUNK_SIZE][3]) {
    // Texture rows are z, columns x; the chunk arrays are [x][z].
    uint8_t texels[CHUNK_SIZE][CHUNK_SIZE][3];
    for (int x = 0; x < CHUNK_SIZE; x++)
        for (int z = 0; z < CHUNK_SIZE; z++)
            for (int c = 0; c < 3; c++) texels[z][x][c] = src[x][z][c];
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x0, z0, CHUNK_SIZE, CHUNK_SIZE, GL_RGB, GL_UNSIGNED_BYTE, texels);
}

} // namespace

void init() {
    if (grassTex) return;
    grassTex = makeTexture(GRASS_UNIT);
    foliageTex = makeTexture(FOLIAGE_UNIT);
    glActiveTexture(GL_TEXTURE0);
}

void upload(int chunkX, int chunkZ, const BiomeTints& tints) {
    if (!grassTex) return;
    const int x0 = wrap(chunkX * CHUNK_SIZE), z0 = wrap(chunkZ * CHUNK_SIZE);
    put(grassTex, GRASS_UNIT, x0, z0, tints.grass);
    put(foliageTex, FOLIAGE_UNIT, x0, z0, tints.foliage);
    glActiveTexture(GL_TEXTURE0);
}

void destroy() {
    glDeleteTextures(1, &grassTex);
    glDeleteTextures(1, &foliageTex);
    grassTex = foliageTex = 0;
}

} // namespace BiomeTintMap
