#pragma once

#include <cstdint>

namespace strategy {

enum class RenderPassKind { terrain, world, overlay, userInterface };

// Owns the mutable OpenGL bindings and fixed-function switches for one pass.
// Restoring the previous state makes nested passes (notably text inside UI) safe.
class RenderPass final {
  public:
    explicit RenderPass(RenderPassKind kind);
    ~RenderPass();
    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

  private:
    std::int32_t program_{0};
    std::int32_t vertexArray_{0};
    std::int32_t arrayBuffer_{0};
    std::int32_t activeTexture_{0};
    std::int32_t texture2d_{0};
    bool depthTest_{false};
    bool cullFace_{false};
    bool blend_{false};
};

} // namespace strategy
