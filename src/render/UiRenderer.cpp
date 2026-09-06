#include "render/UiRenderer.hpp"

#include "render/RenderPass.hpp"
#include "ui/UiDocument.hpp"

#include <algorithm>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <vector>

namespace strategy {
namespace {
struct UiVertex {
    glm::vec2 position;
    glm::vec3 color;
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

UiRenderer::UiRenderer() {
    constexpr const char* vertex = R"(#version 450 core
layout(location=0) in vec2 position;
layout(location=1) in vec3 colorIn; out vec3 color;
void main(){gl_Position=vec4(position,0,1);color=colorIn;})";
    constexpr const char* fragment = R"(#version 450 core
in vec3 color; out vec4 outputColor;
void main(){outputColor=vec4(color,1);})";
    program_ = shaders_.load("ui", vertex, fragment);
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, color));
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
                           int height) const {
    std::vector<UiVertex> vertices;
    appendRectangle(vertices, left, top, right, bottom, color, width, height);
    RenderPass pass(RenderPassKind::userInterface);
    shaders_.use(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(UiVertex)),
                 vertices.data(),
                 GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
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

void UiRenderer::draw(const UiDocument& document, int width, int height) const {
    std::vector<UiVertex> rectangles;
    std::vector<TextDraw> labels;
    for (const UiElement& element : document.elements()) {
        if (element.kind != UiElementKind::label) {
            const glm::vec3 color = element.hovered || element.focused ? element.hoverColor
                                                                       : element.color;
            appendRectangle(rectangles,
                            element.bounds.left,
                            element.bounds.top,
                            element.bounds.right,
                            element.bounds.bottom,
                            color,
                            width,
                            height);
        }
        if (!element.text.empty())
            labels.push_back({element.text,
                              element.bounds.left +
                                  (element.kind == UiElementKind::label ? 0.0F : 12.0F),
                              element.bounds.top +
                                  (element.kind == UiElementKind::label ? 0.0F : 10.0F),
                              std::max(element.textScale * 8.0F, 13.0F),
                              element.textColor});
    }

    RenderPass pass(RenderPassKind::userInterface);
    if (!rectangles.empty()) {
        shaders_.use(program_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(rectangles.size() * sizeof(UiVertex)),
                     rectangles.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(rectangles.size()));
    }
    font_.drawBatch(labels, width, height);
}

} // namespace strategy
