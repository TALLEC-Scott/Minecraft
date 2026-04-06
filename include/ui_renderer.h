#pragma once

#include "gl_header.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Shader;

class UIRenderer {
  public:
    void init();
    void destroy();

    // Call at start of UI frame — sets up ortho projection, disables depth, etc.
    void begin(int windowWidth, int windowHeight);

    // Drawing primitives
    void drawRect(float x, float y, float w, float h, glm::vec4 color);
    void drawTexturedRect(float x, float y, float w, float h, GLuint texture, float u0, float v0, float u1, float v1,
                          glm::vec4 tint = glm::vec4(1.0f));
    void drawText(const std::string& text, float x, float y, float scale, glm::vec4 color = glm::vec4(1.0f));
    void drawTextShadow(const std::string& text, float x, float y, float scale, glm::vec4 color = glm::vec4(1.0f));
    void drawTextRotated(const std::string& text, float cx, float cy, float scale, float angleDeg,
                         glm::vec4 color = glm::vec4(1.0f));
    float textWidth(const std::string& text, float scale) const;
    float textHeight(float scale) const;

    // Flush all batched quads
    void end();

    // Load a texture from file (returns GL texture ID)
    static GLuint loadTexture(const char* path, bool repeat = false);

    static constexpr float GLYPH_W = 8.0f;
    static constexpr float GLYPH_H = 8.0f;

  private:
    GLuint shaderProgram = 0;
    GLuint fontTexture = 0;
    GLuint vao = 0, vbo = 0;
    std::vector<float> vertices;

    // Current batch state
    GLuint currentTexture = 0;
    bool currentUseTexture = false;

    // Saved GL state
    GLboolean savedDepthTest = GL_FALSE;
    GLboolean savedCullFace = GL_FALSE;
    GLboolean savedBlend = GL_FALSE;

    void flush();
    void ensureState(bool useTexture, GLuint texture);
    void addQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, glm::vec4 color);

    // Shader uniform locations
    GLint projLoc = -1;
    GLint texLoc = -1;
    GLint useTexLoc = -1;
};
