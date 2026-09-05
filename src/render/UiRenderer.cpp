#include "render/UiRenderer.hpp"

#include "ui/UiDocument.hpp"

#include <algorithm>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <vector>

namespace strategy {
namespace {
std::uint32_t shader(GLenum type, const char* source) {
    const auto result = glCreateShader(type);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    GLint compiled = GL_FALSE;
    glGetShaderiv(result, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glDeleteShader(result);
        throw std::runtime_error("UI shader compilation failed");
    }
    return result;
}
} // namespace

UiRenderer::UiRenderer() {
    constexpr const char* vertex = R"(#version 450 core
layout(location=0) in vec2 position;
void main(){gl_Position=vec4(position,0,1);})";
    constexpr const char* fragment = R"(#version 450 core
uniform vec3 color; out vec4 outputColor;
void main(){outputColor=vec4(color,1);})";
    const auto vertexShader = shader(GL_VERTEX_SHADER, vertex);
    const auto fragmentShader = shader(GL_FRAGMENT_SHADER, fragment);
    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, fragmentShader);
    glLinkProgram(program_);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

UiRenderer::~UiRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(program_);
}

void UiRenderer::rectangle(float left,
                           float top,
                           float right,
                           float bottom,
                           const glm::vec3& color,
                           int width,
                           int height) const {
    const auto point = [width, height](float x, float y) {
        return glm::vec2{x / static_cast<float>(width) * 2.0F - 1.0F,
                         1.0F - y / static_cast<float>(height) * 2.0F};
    };
    const std::vector<glm::vec2> vertices{point(left, top),
                                          point(left, bottom),
                                          point(right, top),
                                          point(right, top),
                                          point(left, bottom),
                                          point(right, bottom)};
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glUniform3fv(glGetUniformLocation(program_, "color"), 1, glm::value_ptr(color));
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
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
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    for (const UiElement& element : document.elements()) {
        if (element.kind != UiElementKind::label) {
            const glm::vec3 color = element.hovered || element.focused ? element.hoverColor
                                                                       : element.color;
            rectangle(element.bounds.left,
                      element.bounds.top,
                      element.bounds.right,
                      element.bounds.bottom,
                      color,
                      width,
                      height);
        }
        if (!element.text.empty())
            text(element.text,
                 element.bounds.left + (element.kind == UiElementKind::label ? 0.0F : 12.0F),
                 element.bounds.top + (element.kind == UiElementKind::label ? 0.0F : 10.0F),
                 element.textScale,
                 element.textColor,
                 width,
                 height);
    }
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace strategy
