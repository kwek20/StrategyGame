#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
namespace strategy {
class FontRenderer final {
public:
    FontRenderer(); ~FontRenderer();
    FontRenderer(const FontRenderer&)=delete; FontRenderer& operator=(const FontRenderer&)=delete;
    void draw(const std::string& text,float x,float top,float pixelHeight,
              glm::vec3 color,int viewportWidth,int viewportHeight) const;
private:
    struct Glyph {glm::vec2 size{0},bearing{0},uvMin{0},uvMax{0};float advance{0};};
    std::array<Glyph,128> glyphs_{};
    std::uint32_t texture_{0},program_{0},vao_{0},vbo_{0};
};
}
