#include "ui/UiController.hpp"

#include <algorithm>

namespace strategy {
namespace {
bool focusable(const UiElement& element) {
    return element.enabled && (element.kind == UiElementKind::button ||
        element.kind == UiElementKind::iconButton || element.kind == UiElementKind::textField ||
        element.kind == UiElementKind::entityCard || element.kind == UiElementKind::queueSlot);
}
}

void UiController::pointerMoved(glm::vec2 position) {
    pointer_ = position;
}

void UiController::advance(float deltaSeconds) {
    if (!hoveredId_.empty()) hoverSeconds_ += std::max(0.0F, deltaSeconds);
}

void UiController::apply(UiDocument& document) {
    document.pointerMoved(pointer_);
    const UiElement* hovered = document.hoveredElement();
    const std::string nextHovered = hovered ? hovered->id : std::string{};
    if (nextHovered != hoveredId_) {
        hoveredId_ = nextHovered;
        hoverSeconds_ = 0.0F;
    }
    document.focus(focusedId_);
    for (UiElement& element : document.elements())
        element.pressed = element.id == pressedId_;
    if (document.find("entity.tooltip")) {
        const UiElement* source = document.find(hoveredId_);
        document.setText("entity.tooltip",
            source && hoverSeconds_ >= 0.45F ? source->tooltip : std::string{});
    }
}

std::optional<std::string> UiController::press(UiDocument& document, glm::vec2 position) {
    pointerMoved(position);
    apply(document);
    const UiElement* hit = document.hitTest(position);
    pressedId_ = hit ? hit->id : std::string{};
    if (!hit || !hit->enabled) return std::nullopt;
    focusedId_ = hit->id;
    document.focus(focusedId_);
    return hit->id;
}

std::optional<std::string> UiController::release(UiDocument& document, glm::vec2 position) {
    pointerMoved(position);
    apply(document);
    const UiElement* hit = document.hitTest(position);
    const bool activates = hit && hit->enabled && hit->id == pressedId_;
    pressedId_.clear();
    return activates ? std::optional<std::string>{hit->id} : std::nullopt;
}

void UiController::clearPressed() {
    pressedId_.clear();
}

bool UiController::moveFocus(UiDocument& document, int direction) {
    std::vector<UiElement*> candidates;
    for (UiElement& element : document.elements())
        if (focusable(element)) candidates.push_back(&element);
    if (candidates.empty()) return false;
    auto current = std::find_if(candidates.begin(), candidates.end(),
        [&](const UiElement* element) { return element->id == focusedId_; });
    std::ptrdiff_t index = current == candidates.end() ? (direction < 0 ? 0 : -1)
        : std::distance(candidates.begin(), current);
    index = (index + (direction < 0 ? -1 : 1) + static_cast<std::ptrdiff_t>(candidates.size())) %
            static_cast<std::ptrdiff_t>(candidates.size());
    focusedId_ = candidates[static_cast<std::size_t>(index)]->id;
    document.focus(focusedId_);
    return true;
}

std::optional<std::string> UiController::activateFocused(const UiDocument& document) const {
    const UiElement* element = document.find(focusedId_);
    return element && focusable(*element) ? std::optional<std::string>{element->id} : std::nullopt;
}

std::optional<std::string> UiController::visibleTooltip(const UiDocument& document,
                                                        float delaySeconds) const {
    if (hoverSeconds_ < delaySeconds) return std::nullopt;
    const UiElement* element = document.find(hoveredId_);
    return element && !element->tooltip.empty()
        ? std::optional<std::string>{element->tooltip} : std::nullopt;
}

} // namespace strategy
