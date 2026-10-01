#include "render/UiRenderer.hpp"

#include "assets/ResourceManager.hpp"
#include "assets/Texture.hpp"
#include "render/RenderPass.hpp"
#include "ui/UiDocument.hpp"
#include "ui/UiTheme.hpp"

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

void UiRenderer::draw(const UiDocument& document, int width, int height) const {
    struct DeferredTexture {
        std::string key;
        float left, top, right, bottom;
        glm::vec3 tint;
    };
    struct DeferredIcon {
        std::string key;
        float left, top, right, bottom;
    };
    struct TooltipLine {
        std::string text;
        std::string style;
    };
    std::vector<UiVertex> rectangles;
    std::vector<TextDraw> labels;
    std::vector<DeferredTexture> tooltipTextures;
    std::vector<DeferredIcon> tooltipIcons;
    for (const UiElement& element : document.elements()) {
        if (element.kind == UiElementKind::tooltip) {
            if (element.text.empty()) continue;
            const float fontPixels = std::max(element.textScale * 8.0F, 13.0F);
            const float padding = element.id == "entity.tooltip" ? 28.0F : 12.0F;
            const float boxWidth = std::min(element.bounds.right - element.bounds.left,
                                            static_cast<float>(width) - 16.0F);
            const float available = std::max(1.0F, boxWidth - padding * 2);
            std::vector<TooltipLine> lines;
            std::istringstream paragraphs(element.text);
            std::string paragraph;
            while (std::getline(paragraphs, paragraph)) {
                std::string style = "body";
                if (paragraph.starts_with("[[")) {
                    const std::size_t markerEnd = paragraph.find("]]", 2);
                    if (markerEnd != std::string::npos) {
                        style = paragraph.substr(2, markerEnd - 2);
                        paragraph.erase(0, markerEnd + 2);
                    }
                }
                std::istringstream words(paragraph);
                std::string line, word;
                while (words >> word) {
                    if (!line.empty() && font_.measureWidth(line + " " + word, fontPixels) > available) {
                        lines.push_back({line, style});
                        line.clear();
                    }
                    while (font_.measureWidth(word, fontPixels) > available && word.size() > 1) {
                        std::size_t end = 1;
                        while (end < word.size() && font_.measureWidth(word.substr(0, end + 1), fontPixels) <= available) ++end;
                        lines.push_back({word.substr(0, end), style});
                        word.erase(0, end);
                    }
                    if (!line.empty()) line += " ";
                    line += word;
                }
                lines.push_back({line, style});
            }
            const float lineHeight = fontPixels * 1.5F;
            const float sectionGap = 5.0F;
            std::vector<float> lineOffsets(lines.size(), 0.0F);
            float contentHeight = 0.0F;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (i > 0 && lines[i].style != lines[i - 1].style)
                    contentHeight += sectionGap;
                lineOffsets[i] = contentHeight;
                contentHeight += lineHeight;
            }
            const float boxHeight = padding * 2 + contentHeight;
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
            {
                const auto lineColor = [&]() {
                    if (i == 0 || lines[i].style == "title") return glm::vec3{1.0F, 0.88F, 0.52F};
                    if (lines[i].style == "cost") return glm::vec3{1.0F, 0.72F, 0.36F};
                    if (lines[i].style == "power") return glm::vec3{0.46F, 0.90F, 1.0F};
                    if (lines[i].style == "requirements") return glm::vec3{0.58F, 0.90F, 0.66F};
                    if (lines[i].style == "missing") return glm::vec3{1.0F, 0.42F, 0.36F};
                    return element.textColor;
                }();
                const bool costLine = lines[i].style == "cost" && !element.tooltipIcons.empty();
                const float iconSize = std::min(22.0F, lineHeight - 2.0F);
                const float iconSpan = costLine
                    ? static_cast<float>(element.tooltipIcons.size()) * (iconSize + 3.0F) + 5.0F
                    : 0.0F;
                labels.push_back({lines[i].text, left + padding + iconSpan,
                                  top + padding + lineOffsets[i],
                                  fontPixels, lineColor});
                if (costLine) {
                    for (std::size_t iconIndex = 0; iconIndex < element.tooltipIcons.size(); ++iconIndex) {
                        const float iconLeft = left + padding +
                            static_cast<float>(iconIndex) * (iconSize + 3.0F);
                        const float iconTop = top + padding + lineOffsets[i];
                        tooltipIcons.push_back({element.tooltipIcons[iconIndex], iconLeft, iconTop,
                                                iconLeft + iconSize, iconTop + iconSize});
                    }
                }
            }
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
            const float iconSize = element.centerIconWithText && !element.icon.empty()
                ? std::min(34.0F, element.bounds.bottom - element.bounds.top - 14.0F) : 0.0F;
            const float iconGap = iconSize > 0.0F ? 9.0F : 0.0F;
            const bool compactSelectorArrow = element.id.ends_with(".left") ||
                                              element.id.ends_with(".right");
            const float horizontalPadding = element.kind == UiElementKind::label ? 0.0F
                : ((element.texture == "ui/action_slot_transparent" || compactSelectorArrow)
                    ? 6.0F : 24.0F);
            float available = element.bounds.right > element.bounds.left
                ? element.bounds.right - element.bounds.left - horizontalPadding
                : 0.0F;
            if (element.centerIconWithText) available -= iconSize + iconGap;
            if (available > 0.0F && font_.measureWidth(displayText, fontPixels) > available) {
                constexpr std::string_view ellipsis{"..."};
                const float ellipsisWidth = font_.measureWidth(std::string{ellipsis}, fontPixels);
                if (ellipsisWidth > available) {
                    displayText.clear();
                } else {
                    while (!displayText.empty() &&
                           font_.measureWidth(displayText, fontPixels) + ellipsisWidth > available)
                        displayText.pop_back();
                    displayText += ellipsis;
                }
            } else if (element.bounds.right > element.bounds.left && available <= 0.0F) {
                displayText.clear();
            }
            glm::vec3 textColor = element.enabled ? element.textColor : UiTheme::disabledText;
            const float luminance = textColor.r * 0.2126F + textColor.g * 0.7152F +
                                    textColor.b * 0.0722F;
            if (luminance < 0.48F) textColor = UiTheme::text;
            const bool centered = element.kind == UiElementKind::button ||
                                  element.id == "loading.percentage";
            const float textWidth = centered ? font_.measureWidth(displayText, fontPixels) : 0.0F;
            const float x = centered
                                ? (element.centerIconWithText
                                      ? element.bounds.left +
                                            ((element.bounds.right - element.bounds.left) -
                                             (iconSize + iconGap + textWidth)) * 0.5F +
                                            iconSize + iconGap
                                      : element.bounds.left +
                                            ((element.bounds.right - element.bounds.left) - textWidth) *
                                                0.5F)
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
        if (element.id == "loading.progress" && element.kind == UiElementKind::progressBar) {
            const float barWidth = element.bounds.right - element.bounds.left;
            const float barHeight = element.bounds.bottom - element.bounds.top;
            const float innerLeft = element.bounds.left + barWidth * 0.043F;
            const float innerRight = element.bounds.right - barWidth * 0.043F;
            const float innerTop = element.bounds.top + barHeight * 0.285F;
            const float innerBottom = element.bounds.bottom - barHeight * 0.285F;
            const float clampedProgress = std::clamp(element.progress, 0.0F, 1.0F);
            const float fillRight = innerLeft + (innerRight - innerLeft) * clampedProgress;
            const float seconds = std::chrono::duration<float>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

            rectangle(innerLeft, innerTop, innerRight, innerBottom,
                      {0.005F, 0.025F, 0.040F}, width, height);
            if (clampedProgress > 0.0F) {
                rectangle(innerLeft, innerTop, fillRight, innerBottom,
                          {0.025F, 0.30F, 0.42F}, width, height, 0.80F);
                const float coreTop = innerTop + (innerBottom - innerTop) * 0.43F;
                const float coreBottom = innerBottom - (innerBottom - innerTop) * 0.43F;
                rectangle(innerLeft, coreTop, fillRight, coreBottom,
                          {0.18F, 0.90F, 1.0F}, width, height, 0.90F);

                const float fillWidth = fillRight - innerLeft;
                if (fillWidth > 8.0F) {
                    const float shimmerTravel = std::max(1.0F, fillWidth + 30.0F);
                    const float shimmerCenter = innerLeft - 15.0F +
                        std::fmod(seconds * 85.0F, shimmerTravel);
                    const float shimmerLeft = std::max(innerLeft, shimmerCenter - 12.0F);
                    const float shimmerRight = std::min(fillRight, shimmerCenter + 12.0F);
                    if (shimmerRight > shimmerLeft)
                        rectangle(shimmerLeft, innerTop + 1.0F, shimmerRight, innerBottom - 1.0F,
                                  {0.30F, 0.92F, 1.0F}, width, height, 0.18F);
                }
            }

            for (int tick = 1; tick < 10; ++tick) {
                const float tickX = innerLeft + (innerRight - innerLeft) *
                    (static_cast<float>(tick) / 10.0F);
                const bool completed = tickX <= fillRight + 0.5F;
                const float tickTop = innerTop + (innerBottom - innerTop) * 0.25F;
                const float tickBottom = innerBottom - (innerBottom - innerTop) * 0.25F;
                rectangle(tickX - 0.75F, tickTop, tickX + 0.75F, tickBottom,
                          completed ? glm::vec3{0.34F, 0.92F, 1.0F}
                                    : glm::vec3{0.08F, 0.30F, 0.40F},
                          width, height, completed ? 0.90F : 0.62F);
            }

            const float pulseX = clampedProgress <= 0.0F ? innerLeft + 1.5F : fillRight;
            const float pulse = 0.78F + std::sin(seconds * 5.0F) * 0.22F;
            const TextureHandle leadHandle = resources_.requestTexture("ui/loading_progress_lead");
            const float desiredLeadLeft = pulseX - 24.0F;
            const float desiredLeadRight = pulseX + 8.0F;
            const float leadLeft = std::max(innerLeft, desiredLeadLeft);
            const float leadRight = std::min(innerRight, desiredLeadRight);
            if (resources_.state(leadHandle) == ResourceState::ready && leadRight > leadLeft) {
                const Texture* lead = resources_.texture(leadHandle);
                const float u0 = (leadLeft - desiredLeadLeft) /
                    (desiredLeadRight - desiredLeadLeft);
                const float u1 = (leadRight - desiredLeadLeft) /
                    (desiredLeadRight - desiredLeadLeft);
                image(lead->id(), leadLeft, innerTop, leadRight, innerBottom,
                      u0, 0.0F, u1, 1.0F, {pulse, pulse, pulse}, width, height);
            } else {
                rectangle(std::max(innerLeft, pulseX - 1.0F), innerTop,
                          std::min(innerRight, pulseX + 1.5F), innerBottom,
                          {0.72F, 0.98F, 1.0F}, width, height, pulse);
            }

            if (resources_.state(handle) == ResourceState::ready) {
                const Texture* texture = resources_.texture(handle);
                image(texture->id(), element.bounds.left, element.bounds.top,
                      element.bounds.right, element.bounds.bottom,
                      0.0F, 0.0F, 1.0F, 1.0F, {1.0F, 1.0F, 1.0F}, width, height);
            }
            const float capTop = element.bounds.top + barHeight * 0.30F;
            const float capBottom = element.bounds.bottom - barHeight * 0.30F;
            rectangle(element.bounds.left + barWidth * 0.025F, capTop,
                      element.bounds.left + barWidth * 0.029F, capBottom,
                      {0.20F, 0.88F, 1.0F}, width, height, 0.65F + 0.25F * pulse);
            if (clampedProgress >= 1.0F)
                rectangle(element.bounds.right - barWidth * 0.029F, capTop,
                          element.bounds.right - barWidth * 0.025F, capBottom,
                          {0.28F, 0.94F, 1.0F}, width, height, 0.80F + 0.20F * pulse);
            continue;
        }
        const bool actionSlot = element.texture == "ui/action_slot_transparent";
        glm::vec3 tint = !element.enabled
            ? (actionSlot ? glm::vec3{0.60F, 0.62F, 0.64F} : UiTheme::disabled)
            : (element.pressed ? glm::vec3{0.72F, 0.58F, 0.42F}
            : (element.active ? glm::vec3{1.0F, 0.58F, 0.20F}
            : ((element.hovered || element.focused) ? element.hoverColor
            : (element.focused && element.kind == UiElementKind::iconButton
                ? glm::vec3{0.78F, 0.92F, 1.0F} : element.color))));
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
                glm::vec3 fillTint{1.0F, 1.0F, 1.0F};
                if (element.progressTexture == "ui/loading_progress_fill") {
                    const float seconds = std::chrono::duration<float>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    const float pulse = 0.92F + std::sin(seconds * 3.2F) * 0.08F;
                    fillTint = {pulse * 0.90F, pulse * 0.98F, pulse};
                }
                image(fill->id(), element.bounds.left, element.bounds.top, right,
                      element.bounds.bottom, 0.0F, 0.0F, element.progress, 1.0F,
                      fillTint, width, height);
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
    if (!tooltipIcons.empty()) {
        const TextureHandle atlasHandle = resources_.requestTexture(iconAtlas_.texture());
        if (resources_.state(atlasHandle) == ResourceState::ready) {
            const Texture* atlas = resources_.texture(atlasHandle);
            for (const DeferredIcon& item : tooltipIcons) {
                const IconRegion* region = iconAtlas_.region(item.key);
                if (!region) continue;
                rectangle(item.left - 1.0F, item.top - 1.0F,
                          item.right + 1.0F, item.bottom + 1.0F,
                          {0.08F, 0.20F, 0.27F}, width, height, 0.92F);
                const float u0 = static_cast<float>(region->x) / iconAtlas_.width();
                const float v0 = static_cast<float>(region->y) / iconAtlas_.height();
                const float u1 = static_cast<float>(region->x + region->width) / iconAtlas_.width();
                const float v1 = static_cast<float>(region->y + region->height) / iconAtlas_.height();
                image(atlas->id(), item.left, item.top, item.right, item.bottom,
                      u0, v0, u1, v1, {1.0F, 1.0F, 1.0F}, width, height);
            }
        }
    }
    for (const UiElement& element : document.elements()) {
        if (element.id != "loading.percentage") continue;
        rectangle(element.bounds.left - 8.0F, element.bounds.top - 4.0F,
                  element.bounds.right + 8.0F, element.bounds.bottom + 4.0F,
                  {0.005F, 0.020F, 0.030F}, width, height, 0.92F);
        rectangle(element.bounds.left - 8.0F, element.bounds.top - 4.0F,
                  element.bounds.right + 8.0F, element.bounds.top - 2.0F,
                  {0.12F, 0.52F, 0.64F}, width, height, 0.78F);
    }
    font_.drawBatch(labels, width, height);
}

} // namespace strategy
