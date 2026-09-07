#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace strategy {

struct UiRect {
    float left{}, top{}, right{}, bottom{};
    [[nodiscard]] bool contains(glm::vec2 point) const;
};

enum class UiElementKind {
    panel,
    modalPanel,
    label,
    button,
    iconButton,
    textField,
    progressBar,
    entityCard,
    queueSlot,
    scrollList,
    tooltip
};

struct UiElement {
    std::string id;
    UiElementKind kind{UiElementKind::panel};
    UiRect bounds;
    std::string text;
    float textScale{1.5F};
    glm::vec3 color{0.10F, 0.16F, 0.22F};
    glm::vec3 hoverColor{0.22F, 0.38F, 0.52F};
    glm::vec3 textColor{0.95F, 0.98F, 0.82F};
    bool hovered{false};
    bool focused{false};
    bool pressed{false};
    bool enabled{true};
    float progress{0.0F};
    std::string icon;
    std::string tooltip;
};

class UiDocument final {
  public:
    UiElement& panel(std::string id, UiRect bounds, glm::vec3 color);
    UiElement& label(std::string id,
                     UiRect bounds,
                     std::string text,
                     float scale = 1.5F,
                     glm::vec3 color = {0.95F, 0.98F, 0.82F});
    UiElement& button(std::string id,
                      UiRect bounds,
                      std::string text,
                      glm::vec3 color = {0.10F, 0.16F, 0.22F},
                      glm::vec3 hover = {0.22F, 0.38F, 0.52F});
    UiElement& textField(std::string id, UiRect bounds, std::string text);
    UiElement& iconButton(std::string id, UiRect bounds, std::string icon,
                          std::string tooltip = {}, bool enabled = true);
    UiElement& progressBar(std::string id, UiRect bounds, float progress,
                           glm::vec3 color = {0.20F, 0.68F, 0.95F});
    UiElement& entityCard(std::string id, UiRect bounds, std::string icon,
                          std::string tooltip = {});
    UiElement& queueSlot(std::string id, UiRect bounds, std::string icon,
                         std::string tooltip = {}, bool enabled = true);
    UiElement& modal(std::string id, UiRect bounds,
                     glm::vec3 color = {0.025F, 0.04F, 0.06F});
    UiElement& scrollList(std::string id, UiRect bounds);
    UiElement& tooltip(std::string id, UiRect bounds, std::string text);

    void pointerMoved(glm::vec2 position);
    [[nodiscard]] std::optional<std::string> activate(glm::vec2 position);
    void focus(std::string_view id);
    void setText(std::string_view id, std::string text);
    [[nodiscard]] bool hovered(std::string_view id) const;
    [[nodiscard]] bool focused(std::string_view id) const;
    [[nodiscard]] const UiElement* find(std::string_view id) const;
    [[nodiscard]] const UiElement* hitTest(glm::vec2 position) const;
    [[nodiscard]] const UiElement* hoveredElement() const;
    void scaleFromReference(int viewportWidth, int viewportHeight, float userScale = 1.0F);
    [[nodiscard]] std::vector<UiElement>& elements() { return elements_; }
    [[nodiscard]] const std::vector<UiElement>& elements() const { return elements_; }

  private:
    UiElement& add(UiElement element);
    std::vector<UiElement> elements_;
};

} // namespace strategy
