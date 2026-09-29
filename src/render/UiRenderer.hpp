#pragma once

#include "render/FontRenderer.hpp"
#include "render/ShaderManager.hpp"

#include <cstdint>
#include <string>

namespace strategy {

class UiDocument;
class ResourceManager;

class UiRenderer final {
  public:
    explicit UiRenderer(ShaderManager& shaders, ResourceManager& resources);
    ~UiRenderer();
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    void draw(const UiDocument& document, int width, int height) const;
    void text(const std::string& value,
              float x,
              float y,
              float scale,
              const glm::vec3& color,
              int width,
              int height) const;
    void rectangle(float left,
                   float top,
                   float right,
                   float bottom,
                   const glm::vec3& color,
                   int width,
                   int height,
                   float opacity = 1.0F) const;
    void image(std::uint32_t texture,
               float left,
               float top,
               float right,
               float bottom,
               float u0,
               float v0,
               float u1,
               float v1,
               const glm::vec3& tint,
               int width,
               int height,
               float opacity = 1.0F) const;

  private:
    ShaderManager& shaders_;
    ResourceManager& resources_;
    ShaderHandle program_;
    std::uint32_t vao_{0}, vbo_{0};
    FontRenderer font_;
};

} // namespace strategy
