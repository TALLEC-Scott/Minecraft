#include "item_mesh.h"

namespace {

// One quad as two triangles; corners counter-clockwise seen from `normal`.
void pushQuad(std::vector<ItemMeshVertex>& out, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 normal,
              glm::vec3 color) {
    for (glm::vec3 p : {a, b, c, c, d, a}) out.push_back({p, normal, color});
}

} // namespace

std::vector<ItemMeshVertex> extrudeSprite(const uint8_t* rgba, int width, int height) {
    std::vector<ItemMeshVertex> out;
    auto opaque = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height && rgba[(y * width + x) * 4 + 3] >= 128;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!opaque(x, y)) continue;
            const uint8_t* p = rgba + (y * width + x) * 4;
            glm::vec3 color(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f);
            // Cell spans [x0,x1] × [y0,y1] × [-0.5,0.5]; image rows grow downward.
            float x0 = static_cast<float>(x), x1 = x0 + 1.0f;
            float y1 = static_cast<float>(height - y), y0 = y1 - 1.0f;
            const float z0 = -0.5f, z1 = 0.5f;
            pushQuad(out, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1}, color);
            pushQuad(out, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {0, 0, -1}, color);
            if (!opaque(x - 1, y))
                pushQuad(out, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {-1, 0, 0}, color);
            if (!opaque(x + 1, y))
                pushQuad(out, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {1, 0, 0}, color);
            if (!opaque(x, y - 1)) // pixel above in the image = +Y
                pushQuad(out, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, {0, 1, 0}, color);
            if (!opaque(x, y + 1))
                pushQuad(out, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0}, color);
        }
    }
    return out;
}
