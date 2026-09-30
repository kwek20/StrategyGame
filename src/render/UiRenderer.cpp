#include "render/UiRenderer.hpp"

#include "assets/ResourceManager.hpp"
#include "assets/Texture.hpp"
#include "render/RenderPass.hpp"
#include "ui/UiDocument.hpp"
#include "ui/UiTheme.hpp"

#include <algorithm>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <sstream>
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
    : shaders_(shaders), resources_(resources), font_(shaders) {
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

void UiRenderer::draw(const UiDocument& document, int width, int height) const {
    struct DeferredTexture {
        std::string key;
        float left, top, right, bottom;
        glm::vec3 tint;
    };
    std::vector<UiVertex> rectangles;
    std::vector<TextDraw> labels;
    std::vector<DeferredTexture> tooltipTextures;
    for (const UiElement& element : document.elements()) {
        if (element.kind == UiElementKind::tooltip) {
            if (element.text.empty()) continue;
            const float fontPixels = std::max(element.textScale * 8.0F, 13.0F);
            const float padding = 12.0F;
            const float boxWidth = std::min(element.bounds.right - element.bounds.left,
                                            static_cast<float>(width) - 16.0F);
            const float available = std::max(1.0F, boxWidth - padding * 2);
            std::vector<std::string> lines;
            std::istringstream paragraphs(element.text);
            std::string paragraph;
            while (std::getline(paragraphs, paragraph)) {
                std::istringstream words(paragraph);
                std::string line, word;
                while (words >> word) {
                    if (!line.empty() && font_.measureWidth(line + " " + word, fontPixels) > available) {
                        lines.push_back(line);
                        line.clear();
                    }
                    while (font_.measureWidth(word, fontPixels) > available && word.size() > 1) {
                        std::size_t end = 1;
                        while (end < word.size() && font_.measureWidth(word.substr(0, end + 1), fontPixels) <= available) ++end;
                        lines.push_back(word.substr(0, end));
                        word.erase(0, end);
                    }
                    if (!line.empty()) line += " ";
                    line += word;
                }
                lines.push_back(line);
            }
            const float lineHeight = fontPixels * 1.4F;
            const float boxHeight = padding * 2 + lineHeight * static_cast<float>(lines.size());
            const float left = std::clamp(element.bounds.left, 8.0F, std::max(8.0F, width - boxWidth - 8));
            const float preferredTop = element.id == "entity.tooltip" ? element.bounds.bottom - boxHeight : element.bounds.top;
            const float top = std::clamp(preferredTop, 8.0F, std::max(8.0F, height - boxHeight - 8));
            if (element.texture.empty()) {
                appendRectangle(rectangles, left - 1, top - 1,
                                left + boxWidth + 1, top + boxHeight + 1,
                                {0.45F, 0.55F, 0.63F}, width, height);
                appendRectangle(rectangles, left, top, left + boxWidth,
                                top + boxHeight, element.color, width, height);
            } else {
                tooltipTextures.push_back(
                    {element.texture, left, top, left + boxWidth, top + boxHeight,
                     {0.88F, 0.90F, 0.92F}});
            }
            for (std::size_t i = 0; i < lines.size(); ++i)
                labels.push_back({lines[i], left + padding, top + padding + lineHeight * static_cast<float>(i),
                                  fontPixels, i == 0 ? glm::vec3{1.0F, 0.88F, 0.52F} : element.textColor});
            continue;
        }
        if (element.kind != UiElementKind::label && element.texture.empty()) {
            const glm::vec3 color = !element.enabled ? UiTheme::disabled
                : (element.pressed ? element.hoverColor * 0.72F
                : (element.hovered || element.focused ? element.hoverColor : element.color));
            const bool framedPanel = element.kind == UiElementKind::panel ||
                                     element.kind == UiElementKind::modalPanel;
            if (framedPanel)
                appendRectangle(rectangles, element.bounds.left, element.bounds.top,
                                element.bounds.right, element.bounds.bottom,
                                {0.18F, 0.34F, 0.42F}, width, height);
            appendRectangle(rectangles,
                            element.bounds.left + (framedPanel ? 2.0F : 0.0F),
                            element.bounds.top + (framedPanel ? 2.0F : 0.0F),
                            element.bounds.right - (framedPanel ? 2.0F : 0.0F),
                            element.bounds.bottom - (framedPanel ? 2.0F : 0.0F),
                            color,
                            width,
                            height);
            if (element.kind == UiElementKind::progressBar) {
                appendRectangle(rectangles, element.bounds.left, element.bounds.top,
                                element.bounds.right, element.bounds.bottom,
                                {0.34F, 0.48F, 0.54F}, width, height);
                appendRectangle(rectangles, element.bounds.left + 2.0F, element.bounds.top + 2.0F,
                                element.bounds.left +
                                    (element.bounds.right - element.bounds.left - 4.0F) * element.progress,
                                element.bounds.bottom - 2.0F, element.hoverColor, width, height);
            }
        }
        if (!element.text.empty()) {
            std::string displayText = element.text;
            const float fontPixels = std::max(element.textScale * 8.0F, 13.0F);
            const float available = element.bounds.right > element.bounds.left
                ? element.bounds.right - element.bounds.left -
                    (element.kind == UiElementKind::label ? 0.0F : 24.0F)
                : 0.0F;
            if (available > 0.0F) {
                const std::size_t maximumCharacters = static_cast<std::size_t>(
                    std::max(1.0F, available / (fontPixels * 0.58F)));
                if (displayText.size() > maximumCharacters)
                    displayText = maximumCharacters > 3
                        ? displayText.substr(0, maximumCharacters - 3) + "..."
                        : displayText.substr(0, maximumCharacters);
            }
            glm::vec3 textColor = element.enabled ? element.textColor : UiTheme::disabledText;
            const float luminance = textColor.r * 0.2126F + textColor.g * 0.7152F +
                                    textColor.b * 0.0722F;
            if (luminance < 0.48F) textColor = UiTheme::text;
            const bool centered = element.kind == UiElementKind::button;
            const float textWidth = centered ? font_.measureWidth(displayText, fontPixels) : 0.0F;
            const float x = centered
                                ? element.bounds.left +
                                      ((element.bounds.right - element.bounds.left) - textWidth) *
                                          0.5F
                                : element.bounds.left +
                                      (element.kind == UiElementKind::label ? 0.0F : 12.0F);
            const float top = centered
                                  ? element.bounds.top +
                                        ((element.bounds.bottom - element.bounds.top) - fontPixels) *
                                            0.5F
                                  : element.bounds.top +
                                        (element.kind == UiElementKind::label ? 0.0F : 10.0F);
            labels.push_back({displayText, x, top, fontPixels, textColor});
        }
    }

    RenderPass pass(RenderPassKind::userInterface);
    for (const UiElement& element : document.elements()) {
        if (element.texture.empty() ||
            (element.kind != UiElementKind::panel &&
             element.kind != UiElementKind::modalPanel)) continue;
        const TextureHandle handle = resources_.requestTexture(element.texture);
        if (resources_.state(handle) == ResourceState::ready) {
            const Texture* texture = resources_.texture(handle);
            image(texture->id(), element.bounds.left, element.bounds.top,
                  element.bounds.right, element.bounds.bottom, 0.0F, 0.0F, 1.0F, 1.0F,
                  element.color, width, height);
        } else {
            rectangle(element.bounds.left, element.bounds.top, element.bounds.right,
                      element.bounds.bottom, {0.025F, 0.04F, 0.06F}, width, height);
        }
    }
    if (!rectangles.empty()) {
        shaders_.use(program_);
        glUniform1i(shaders_.uniform(program_, "useTexture"), 0);
        glUniform1f(shaders_.uniform(program_, "opacity"), 1.0F);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(rectangles.size() * sizeof(UiVertex)),
                     rectangles.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(rectangles.size()));
    }
    for (const UiElement& element : document.elements()) {
        if (element.texture.empty() || element.kind == UiElementKind::label ||
            element.kind == UiElementKind::panel ||
            element.kind == UiElementKind::modalPanel ||
            element.kind == UiElementKind::tooltip) continue;
        const TextureHandle handle = resources_.requestTexture(element.texture);
        const glm::vec3 tint = !element.enabled ? UiTheme::disabled
            : (element.pressed ? element.hoverColor * 0.72F
            : (element.hovered || element.focused ? element.hoverColor : element.color));
        const float verticalOverscan = element.kind == UiElementKind::button ? 3.0F : 0.0F;
        if (resources_.state(handle) == ResourceState::ready) {
            const Texture* texture = resources_.texture(handle);
            image(texture->id(), element.bounds.left, element.bounds.top - verticalOverscan,
                  element.bounds.right, element.bounds.bottom + verticalOverscan,
                  0.0F, 0.0F, 1.0F, 1.0F, tint, width, height);
        } else {
            rectangle(element.bounds.left, element.bounds.top - verticalOverscan,
                      element.bounds.right, element.bounds.bottom + verticalOverscan,
                      element.color, width, height);
        }
        if (element.kind == UiElementKind::progressBar &&
            !element.progressTexture.empty() && element.progress > 0.0F) {
            const TextureHandle fillHandle = resources_.requestTexture(element.progressTexture);
            if (resources_.state(fillHandle) == ResourceState::ready) {
                const Texture* fill = resources_.texture(fillHandle);
                const float right = element.bounds.left +
                    (element.bounds.right - element.bounds.left) * element.progress;
                image(fill->id(), element.bounds.left, element.bounds.top, right,
                      element.bounds.bottom, 0.0F, 0.0F, element.progress, 1.0F,
                      {1.0F, 1.0F, 1.0F}, width, height);
            }
        }
    }
    for (const DeferredTexture& item : tooltipTextures) {
        const TextureHandle handle = resources_.requestTexture(item.key);
        if (resources_.state(handle) == ResourceState::ready) {
            const Texture* texture = resources_.texture(handle);
            image(texture->id(), item.left, item.top, item.right, item.bottom,
                  0.0F, 0.0F, 1.0F, 1.0F, item.tint, width, height);
        } else {
            rectangle(item.left, item.top, item.right, item.bottom,
                      {0.02F, 0.03F, 0.04F}, width, height);
        }
    }
    font_.drawBatch(labels, width, height);
}

} // namespace strategy
