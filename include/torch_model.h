#pragma once

#include <glm/glm.hpp>

#include "gl_header.h"

class Shader;

// The torch as Minecraft draws it: a 2×2×10-pixel stick textured from
// torch.png, foot at the model origin, stick along +Y (see
// torch::placedModel for placing one in a cell). The top 2 pixels (the
// flame) are a separate cap drawn full-bright in `tipColor` so it can pulse.
// Torches are not in the chunk mesh, whose half-block vertex grid can't
// hold a stick 2 pixels wide.
class TorchModel {
  public:
    ~TorchModel();

    // Through the chunk shader (placed, dropped and remote-held torches).
    // `packedLight` = sky * 16 + block at the torch. Leaves `entityColor`
    // reset to white.
    void drawWorld(const Shader& chunkShader, const glm::mat4& model, float packedLight, glm::vec3 tipColor);
    // Through the billboard shader (the first-person hand pass). Leaves
    // `tintColor` set to `tipColor`.
    void drawView(const Shader& billboardShader, const glm::mat4& model, glm::vec3 bodyTint, glm::vec3 tipColor);

    void destroy();

  private:
    GLuint worldVao = 0, worldVbo = 0, viewVao = 0, viewVbo = 0, ebo = 0;
    bool glReady = false;

    void init();
};
