#include "render/FontRenderer.hpp"

#include "render/RenderPass.hpp"

#include <ft2build.h>
#include <glad/glad.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>
namespace strategy {
namespace {
struct Vertex {
    glm::vec2 position, uv;
    glm::vec3 color;
};
std::filesystem::path fontPath() {
    for (const auto& path : {std::filesystem::path{"assets/fonts/Inter-Bold.ttf"},
                             std::filesystem::path{"C:/Windows/Fonts/segoeuib.ttf"},
                             std::filesystem::path{"C:/Windows/Fonts/arialbd.ttf"},
                             std::filesystem::path{"assets/fonts/Inter-Regular.ttf"},
                             std::filesystem::path{"C:/Windows/Fonts/segoeui.ttf"},
                             std::filesystem::path{"C:/Windows/Fonts/arial.ttf"}})
        if (std::filesystem::exists(path))
            return path;
    throw std::runtime_error("No UI font found; add assets/fonts/Inter-Bold.ttf");
}
} // namespace
FontRenderer::FontRenderer(ShaderManager& shaders)
    : shaders_(shaders) {
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    if (FT_Init_FreeType(&library))
        throw std::runtime_error("Could not initialize FreeType");
    const auto path = fontPath();
    if (FT_New_Face(library, path.string().c_str(), 0, &face)) {
        FT_Done_FreeType(library);
        throw std::runtime_error("Could not load UI font: " + path.string());
    }
    FT_Set_Pixel_Sizes(face, 0, 48);
    constexpr int width = 1024, height = 512;
    std::vector<unsigned char> atlas(width * height, 0);
    int penX = 1, penY = 1, rowHeight = 0;
    for (unsigned c = 32; c < 128; ++c) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
            continue;
        FT_GlyphSlot_Embolden(face->glyph);
        const auto& b = face->glyph->bitmap;
        if (penX + int(b.width) + 1 >= width) {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }
        for (unsigned row = 0; row < b.rows; ++row)
            for (unsigned column = 0; column < b.width; ++column)
                atlas[(penY + int(row)) * width + penX + int(column)] =
                    b.buffer[row * b.pitch + column];
        Glyph& g = glyphs_[c];
        g.size = {b.width, b.rows};
        g.bearing = {face->glyph->bitmap_left, face->glyph->bitmap_top};
        g.advance = float(face->glyph->advance.x >> 6);
        g.uvMin = {float(penX) / width, float(penY) / height};
        g.uvMax = {float(penX + int(b.width)) / width, float(penY + int(b.rows)) / height};
        penX += int(b.width) + 1;
        rowHeight = std::max(rowHeight, int(b.rows));
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    program_ = shaders_.loadFiles(
        "font", "assets/shaders/font.vert", "assets/shaders/font.frag");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)sizeof(glm::vec2));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(glm::vec2) * 2));
    glBindVertexArray(0);
}
FontRenderer::~FontRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteTextures(1, &texture_);
}
void FontRenderer::draw(const std::string& text,
                        float x,
                        float top,
                        float pixelHeight,
                        glm::vec3 color,
                        int width,
                        int height) const {
    drawBatch({TextDraw{text, x, top, pixelHeight, color}}, width, height);
}

void FontRenderer::drawBatch(const std::vector<TextDraw>& draws, int width, int height) const {
    std::vector<Vertex> shadows;
    std::vector<Vertex> glyphs;
    const auto append = [&](const TextDraw& draw, glm::vec2 offset, glm::vec3 color,
                            std::vector<Vertex>& output) {
        float x = draw.x;
        const float scale = draw.pixelHeight / 48.0F;
        const float baseline = draw.top + draw.pixelHeight;
        const auto ndc = [width, height, offset](float px, float py) {
            return glm::vec2{(px + offset.x) / width * 2 - 1,
                             1 - (py + offset.y) / height * 2};
        };
        for (unsigned char c : draw.text) {
            if (c >= glyphs_.size())
                continue;
            const Glyph& glyph = glyphs_[c];
            const float left = x + glyph.bearing.x * scale,
                        right = left + glyph.size.x * scale,
                        bottom = baseline - glyph.bearing.y * scale,
                        lower = bottom + glyph.size.y * scale;
            output.insert(output.end(),
                          {{ndc(left, bottom), {glyph.uvMin.x, glyph.uvMin.y}, color},
                           {ndc(left, lower), {glyph.uvMin.x, glyph.uvMax.y}, color},
                           {ndc(right, bottom), {glyph.uvMax.x, glyph.uvMin.y}, color},
                           {ndc(right, bottom), {glyph.uvMax.x, glyph.uvMin.y}, color},
                           {ndc(left, lower), {glyph.uvMin.x, glyph.uvMax.y}, color},
                           {ndc(right, lower), {glyph.uvMax.x, glyph.uvMax.y}, color}});
            x += glyph.advance * scale;
        }
    };
    for (const TextDraw& draw : draws) {
        append(draw, {2, 2}, {0.01F, 0.015F, 0.02F}, shadows);
        append(draw, {0, 0}, draw.color, glyphs);
    }
    shadows.insert(shadows.end(), glyphs.begin(), glyphs.end());
    if (shadows.empty())
        return;

    RenderPass pass(RenderPassKind::userInterface);
    glActiveTexture(GL_TEXTURE0);
    shaders_.use(program_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glUniform1i(shaders_.uniform(program_, "atlas"), 0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 GLsizeiptr(shadows.size() * sizeof(Vertex)),
                 shadows.data(),
                 GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(shadows.size()));
}
} // namespace strategy
