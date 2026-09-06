#pragma once

#include "render/FontRenderer.hpp"
#include "render/ShaderManager.hpp"

#include <cstdint>
#include <string>

namespace strategy {

class UiDocument;

class UiRenderer final {
  public:
    UiRenderer();
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
                   int height) const;

  private:
    ShaderManager shaders_;
    ShaderHandle program_;
    std::uint32_t vao_{0}, vbo_{0};
    FontRenderer font_;
};

} // namespace strategy
