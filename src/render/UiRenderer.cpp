#include "render/UiRenderer.hpp"

#include "assets/ResourceManager.hpp"
#include "assets/Texture.hpp"
#include "render/RenderPass.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <sstream>
#include <string_view>
#include <vector>

namespace strategy {
namespace {
struct UiVertex {
    glm::vec2 position;
    glm::vec3 color;
    glm::vec2 uv{0.0F};
};

void appendRectangle(std::vector<UiVertex>& output,
                     float left,
                     float top,
                     float right,
                     float bottom,
                     glm::vec3 color,
                     int width,
                     int height) {
    const auto point = [width, height](float x, float y) {
        return glm::vec2{x / static_cast<float>(width) * 2.0F - 1.0F,
                         1.0F - y / static_cast<float>(height) * 2.0F};
    };
    output.insert(output.end(),
                  {{point(left, top), color},
                   {point(left, bottom), color},
                   {point(right, top), color},
                   {point(right, top), color},
                   {point(left, bottom), color},
                   {point(right, bottom), color}});
}

} // namespace

UiRenderer::UiRenderer(ShaderManager& shaders, ResourceManager& resources)
    : shaders_(shaders), resources_(resources),
      iconAtlas_(IconAtlas::load("assets/icons_atlas.json")), font_(shaders) {
    program_ =
        shaders_.loadFiles("ui", "assets/shaders/ui.vert", "assets/shaders/ui.frag");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, color));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, uv));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

UiRenderer::~UiRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

void UiRenderer::rectangle(float left,
                           float top,
                           float right,
                           float bottom,
                           const glm::vec3& color,
                           int width,
                           int height,
                           float opacity) const {
    std::vector<UiVertex> vertices;
    appendRectangle(vertices, left, top, right, bottom, color, width, height);
    RenderPass pass(RenderPassKind::userInterface);
    shaders_.use(program_);
    glUniform1i(shaders_.uniform(program_, "useTexture"), 0);
    glUniform1f(shaders_.uniform(program_, "opacity"), opacity);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(UiVertex)),
                 vertices.data(),
                 GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
}

void UiRenderer::image(std::uint32_t texture,
                       float left, float top, float right, float bottom,
                       float u0, float v0, float u1, float v1,
                       const glm::vec3& tint, int width, int height, float opacity) const {
    const auto point = [width, height](float x, float y) {
        return glm::vec2{x / static_cast<float>(width) * 2.0F - 1.0F,
                         1.0F - y / static_cast<float>(height) * 2.0F};
    };
    const std::vector<UiVertex> vertices{
        {point(left, top), tint, {u0, v0}}, {point(left, bottom), tint, {u0, v1}},
        {point(right, top), tint, {u1, v0}}, {point(right, top), tint, {u1, v0}},
        {point(left, bottom), tint, {u0, v1}}, {point(right, bottom), tint, {u1, v1}}};
    RenderPass pass(RenderPassKind::userInterface);
    shaders_.use(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(shaders_.uniform(program_, "uiTexture"), 0);
    glUniform1i(shaders_.uniform(program_, "useTexture"), 1);
    glUniform1f(shaders_.uniform(program_, "opacity"), opacity);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(UiVertex)),
                 vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glUniform1i(shaders_.uniform(program_, "useTexture"), 0);
}

void UiRenderer::text(const std::string& value,
                      float x,
                      float y,
                      float scale,
                      const glm::vec3& color,
                      int width,
                      int height) const {
    font_.draw(value, x, y, std::max(scale * 8.0F, 13.0F), color, width, height);
}

float UiRenderer::measureText(const std::string& value, float scale) const {
    return font_.measureWidth(value, std::max(scale * 8.0F, 13.0F));
}


} // namespace strategy
