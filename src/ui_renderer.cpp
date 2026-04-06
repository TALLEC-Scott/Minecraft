#include "ui_renderer.h"
#include "shader_patch.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb/stb_image.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdint>

// Standard 8x8 bitmap font for ASCII 32-127
// Each character is 8 bytes (rows top to bottom, MSB = leftmost pixel)
// clang-format off
static constexpr uint8_t FONT_DATA[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 33 '!'
    {0x36,0x36,0x14,0x00,0x00,0x00,0x00,0x00}, // 34 '"'
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // 35 '#'
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // 36 '$'
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // 37 '%'
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // 38 '&'
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // 39 '''
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // 40 '('
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // 41 ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 '*'
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // 43 '+'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // 44 ','
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // 45 '-'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // 46 '.'
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // 47 '/'
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 48 '0'
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 49 '1'
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // 50 '2'
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 51 '3'
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // 52 '4'
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 53 '5'
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // 54 '6'
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 55 '7'
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // 56 '8'
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 57 '9'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // 58 ':'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // 59 ';'
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // 60 '<'
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // 61 '='
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 62 '>'
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // 63 '?'
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // 64 '@'
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 65 'A'
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 66 'B'
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 67 'C'
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 68 'D'
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 69 'E'
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 70 'F'
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 71 'G'
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 72 'H'
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 73 'I'
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 74 'J'
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 75 'K'
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 76 'L'
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 77 'M'
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 78 'N'
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 79 'O'
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 80 'P'
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 81 'Q'
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 82 'R'
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 83 'S'
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 84 'T'
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 85 'U'
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 86 'V'
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 87 'W'
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 88 'X'
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 89 'Y'
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 90 'Z'
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // 91 '['
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // 92 '\'
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // 93 ']'
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // 94 '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 95 '_'
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // 96 '`'
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 97 'a'
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 98 'b'
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 99 'c'
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // 100 'd'
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // 101 'e'
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // 102 'f'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 103 'g'
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 104 'h'
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 105 'i'
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // 106 'j'
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 107 'k'
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 108 'l'
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 109 'm'
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 110 'n'
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 111 'o'
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 112 'p'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 113 'q'
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 114 'r'
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 115 's'
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 116 't'
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 117 'u'
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 118 'v'
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 119 'w'
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 120 'x'
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 121 'y'
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 122 'z'
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // 123 '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // 124 '|'
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // 125 '}'
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // 126 '~'
};
// clang-format on

static GLuint compileShader(const char* path, GLenum type) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader: " << path << std::endl;
        return 0;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string src = ss.str();
#ifdef __EMSCRIPTEN__
    src = patchShaderForES(src, type == GL_FRAGMENT_SHADER);
#endif
    const char* srcPtr = src.c_str();

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &srcPtr, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader compile error (" << path << "): " << log << std::endl;
    }
    return shader;
}

void UIRenderer::init() {
    // Compile UI shader
    GLuint vert = compileShader("assets/Shaders/ui_vert.shd", GL_VERTEX_SHADER);
    GLuint frag = compileShader("assets/Shaders/ui_frag.shd", GL_FRAGMENT_SHADER);
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vert);
    glAttachShader(shaderProgram, frag);
    glLinkProgram(shaderProgram);
    glDeleteShader(vert);
    glDeleteShader(frag);

    projLoc = glGetUniformLocation(shaderProgram, "projection");
    texLoc = glGetUniformLocation(shaderProgram, "uiTexture");
    useTexLoc = glGetUniformLocation(shaderProgram, "useTexture");

    // Create font texture from embedded bitmap data
    // 128x64 texture: 16 columns x 8 rows of 8x8 glyphs (covers ASCII 32-127)
    constexpr int TEX_W = 128, TEX_H = 48; // 16 cols x 6 rows
    uint8_t pixels[TEX_W * TEX_H * 4];
    std::memset(pixels, 0, sizeof(pixels));

    for (int ch = 0; ch < 96; ch++) {
        int col = ch % 16;
        int row = ch / 16;
        for (int y = 0; y < 8; y++) {
            uint8_t bits = FONT_DATA[ch][y];
            for (int x = 0; x < 8; x++) {
                bool on = (bits >> x) & 1;
                int px = col * 8 + x;
                int py = (TEX_H - 1) - (row * 8 + y);
                int idx = (py * TEX_W + px) * 4;
                pixels[idx + 0] = 255;
                pixels[idx + 1] = 255;
                pixels[idx + 2] = 255;
                pixels[idx + 3] = on ? 255 : 0;
            }
        }
    }

    glGenTextures(1, &fontTexture);
    glBindTexture(GL_TEXTURE_2D, fontTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_W, TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create VAO/VBO for dynamic quad batching
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // pos(2) + uv(2) + color(4) = 8 floats per vertex
    constexpr int STRIDE = 8 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, STRIDE, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, STRIDE, (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

void UIRenderer::destroy() {
    if (fontTexture) glDeleteTextures(1, &fontTexture);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (shaderProgram) glDeleteProgram(shaderProgram);
}

void UIRenderer::begin(int windowWidth, int windowHeight) {
    // Save GL state
    savedDepthTest = glIsEnabled(GL_DEPTH_TEST);
    savedCullFace = glIsEnabled(GL_CULL_FACE);
    savedBlend = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shaderProgram);
    glm::mat4 proj = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform1i(texLoc, 0);

    vertices.clear();
    currentTexture = 0;
    currentUseTexture = false;
}

void UIRenderer::end() {
    flush();

    // Restore GL state
    if (savedDepthTest)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    if (savedCullFace)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    if (savedBlend)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
}

void UIRenderer::flush() {
    if (vertices.empty()) return;

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    glUniform1i(useTexLoc, currentUseTexture ? 1 : 0);
    if (currentUseTexture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTexture);
    }

    int vertexCount = (int)vertices.size() / 8;
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);

    vertices.clear();
}

void UIRenderer::ensureState(bool useTexture, GLuint texture) {
    if (useTexture != currentUseTexture || (useTexture && texture != currentTexture)) {
        flush();
        currentUseTexture = useTexture;
        currentTexture = texture;
    }
}

void UIRenderer::addQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, glm::vec4 color) {
    // Two triangles: 6 vertices
    float verts[] = {
        // Triangle 1
        x,
        y,
        u0,
        v0,
        color.r,
        color.g,
        color.b,
        color.a,
        x + w,
        y,
        u1,
        v0,
        color.r,
        color.g,
        color.b,
        color.a,
        x + w,
        y + h,
        u1,
        v1,
        color.r,
        color.g,
        color.b,
        color.a,
        // Triangle 2
        x,
        y,
        u0,
        v0,
        color.r,
        color.g,
        color.b,
        color.a,
        x + w,
        y + h,
        u1,
        v1,
        color.r,
        color.g,
        color.b,
        color.a,
        x,
        y + h,
        u0,
        v1,
        color.r,
        color.g,
        color.b,
        color.a,
    };
    vertices.insert(vertices.end(), std::begin(verts), std::end(verts));
}

void UIRenderer::drawRect(float x, float y, float w, float h, glm::vec4 color) {
    ensureState(false, 0);
    addQuad(x, y, w, h, 0, 0, 0, 0, color);
}

void UIRenderer::drawTexturedRect(float x, float y, float w, float h, GLuint texture, float u0, float v0, float u1,
                                  float v1, glm::vec4 tint) {
    ensureState(true, texture);
    addQuad(x, y, w, h, u0, v0, u1, v1, tint);
}

void UIRenderer::drawText(const std::string& text, float x, float y, float scale, glm::vec4 color) {
    ensureState(true, fontTexture);
    float cx = x;
    constexpr float INV_W = 1.0f / 128.0f; // texture width
    constexpr float INV_H = 1.0f / 48.0f;  // texture height

    for (char c : text) {
        int idx = c - 32;
        if (idx < 0 || idx >= 96) {
            cx += GLYPH_W * scale;
            continue;
        }
        int col = idx % 16;
        int row = idx / 16;
        float u0 = col * 8.0f * INV_W;
        float u1 = u0 + 8.0f * INV_W;
        // Font texture is flipped (OpenGL bottom-left origin), so V is inverted
        float v1 = 1.0f - row * 8.0f * INV_H;
        float v0 = v1 - 8.0f * INV_H;

        addQuad(cx, y, GLYPH_W * scale, GLYPH_H * scale, u0, v1, u1, v0, color);
        cx += GLYPH_W * scale;
    }
}

void UIRenderer::drawTextShadow(const std::string& text, float x, float y, float scale, glm::vec4 color) {
    float offset = std::max(1.0f, scale);
    drawText(text, x + offset, y + offset, scale, glm::vec4(0.15f, 0.15f, 0.15f, color.a));
    drawText(text, x, y, scale, color);
}

void UIRenderer::drawTextRotated(const std::string& text, float pivotX, float pivotY, float scale, float angleDeg,
                                 glm::vec4 color) {
    ensureState(true, fontTexture);
    float rad = angleDeg * 3.14159f / 180.0f;
    float cosA = std::cos(rad), sinA = std::sin(rad);

    float totalW = (float)text.length() * GLYPH_W * scale;
    float startX = -totalW / 2.0f; // center text on pivot
    float startY = -GLYPH_H * scale / 2.0f;

    constexpr float INV_W = 1.0f / 128.0f;
    constexpr float INV_H = 1.0f / 48.0f;

    for (size_t i = 0; i < text.size(); i++) {
        int idx = text[i] - 32;
        if (idx < 0 || idx >= 96) continue;
        int col = idx % 16;
        int row = idx / 16;
        float u0 = col * 8.0f * INV_W;
        float u1 = u0 + 8.0f * INV_W;
        float v1 = 1.0f - row * 8.0f * INV_H;
        float v0 = v1 - 8.0f * INV_H;

        float lx = startX + i * GLYPH_W * scale;
        float ly = startY;
        float gw = GLYPH_W * scale;
        float gh = GLYPH_H * scale;

        // Rotate 4 corners around pivot
        auto rot = [&](float x, float y) -> std::pair<float, float> {
            return {pivotX + x * cosA - y * sinA, pivotY + x * sinA + y * cosA};
        };
        auto [x0, y0] = rot(lx, ly);
        auto [x1, y1] = rot(lx, ly + gh);
        auto [x2, y2] = rot(lx + gw, ly + gh);
        auto [x3, y3] = rot(lx + gw, ly);

        float verts[] = {
            x0, y0, u0, v1, color.r, color.g, color.b, color.a, x2, y2, u1, v0, color.r, color.g, color.b, color.a,
            x3, y3, u1, v1, color.r, color.g, color.b, color.a, x0, y0, u0, v1, color.r, color.g, color.b, color.a,
            x1, y1, u0, v0, color.r, color.g, color.b, color.a, x2, y2, u1, v0, color.r, color.g, color.b, color.a,
        };
        vertices.insert(vertices.end(), std::begin(verts), std::end(verts));
    }
}

float UIRenderer::textWidth(const std::string& text, float scale) const {
    return (float)text.length() * GLYPH_W * scale;
}

float UIRenderer::textHeight(float scale) const {
    return GLYPH_H * scale;
}

GLuint UIRenderer::loadTexture(const char* path, bool repeat) {
    int w, h, channels;
    unsigned char* data = stbi_load(path, &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return 0;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, repeat ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (repeat) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    stbi_image_free(data);
    return tex;
}
