#pragma once
#include "render/ShaderManager.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>
namespace strategy {
struct TextDraw {
    std::string text;
    float x{0}, top{0}, pixelHeight{13};
    glm::vec3 color{1};
};
class FontRenderer final {
  public:
    FontRenderer();
    ~FontRenderer();
    FontRenderer(const FontRenderer&) = delete;
    FontRenderer& operator=(const FontRenderer&) = delete;
    void draw(const std::string& text,
              float x,
              float top,
              float pixelHeight,
              glm::vec3 color,
              int viewportWidth,
              int viewportHeight) const;
    void drawBatch(const std::vector<TextDraw>& draws, int viewportWidth, int viewportHeight) const;

  private:
    struct Glyph {
        glm::vec2 size{0}, bearing{0}, uvMin{0}, uvMax{0};
        float advance{0};
    };
    std::array<Glyph, 128> glyphs_{};
    ShaderManager shaders_;
    ShaderHandle program_;
    std::uint32_t texture_{0}, vao_{0}, vbo_{0};
};
} // namespace strategy
