#pragma once

#include "ui/UiDocument.hpp"

#include <optional>
#include <string>

namespace strategy {

class UiController final {
  public:
    void pointerMoved(glm::vec2 position);
    void advance(float deltaSeconds);
    void apply(UiDocument& document);

    [[nodiscard]] std::optional<std::string> press(UiDocument& document,
                                                   glm::vec2 position);
    [[nodiscard]] std::optional<std::string> release(UiDocument& document,
                                                     glm::vec2 position);
    void clearPressed();

    bool moveFocus(UiDocument& document, int direction);
    [[nodiscard]] std::optional<std::string> activateFocused(const UiDocument& document) const;
    [[nodiscard]] std::optional<std::string> visibleTooltip(const UiDocument& document,
                                                            float delaySeconds = 0.45F) const;

    [[nodiscard]] glm::vec2 pointer() const { return pointer_; }
    [[nodiscard]] const std::string& hoveredId() const { return hoveredId_; }
    [[nodiscard]] const std::string& focusedId() const { return focusedId_; }
    [[nodiscard]] const std::string& pressedId() const { return pressedId_; }

  private:
    glm::vec2 pointer_{-1.0F, -1.0F};
    std::string hoveredId_;
    std::string focusedId_;
    std::string pressedId_;
    float hoverSeconds_{0.0F};
};

} // namespace strategy
