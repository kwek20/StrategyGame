#include "ui/UiDocument.hpp"

#include <algorithm>

namespace strategy {

bool UiRect::contains(glm::vec2 point) const {
    return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
}

UiElement& UiDocument::add(UiElement element) {
    elements_.push_back(std::move(element));
    return elements_.back();
}

UiElement& UiDocument::panel(std::string id, UiRect bounds, glm::vec3 color) {
    return add({std::move(id), UiElementKind::panel, bounds, {}, 1.5F, color});
}

UiElement& UiDocument::label(
    std::string id, UiRect bounds, std::string text, float scale, glm::vec3 color) {
    UiElement element{std::move(id), UiElementKind::label, bounds, std::move(text)};
    element.textScale = scale;
    element.textColor = color;
    return add(std::move(element));
}

UiElement& UiDocument::button(
    std::string id, UiRect bounds, std::string text, glm::vec3 color, glm::vec3 hover) {
    UiElement element{std::move(id), UiElementKind::button, bounds, std::move(text)};
    element.color = color;
    element.hoverColor = hover;
    return add(std::move(element));
}

UiElement& UiDocument::textField(std::string id, UiRect bounds, std::string text) {
    UiElement element{std::move(id), UiElementKind::textField, bounds, std::move(text)};
    element.hoverColor = {0.22F, 0.34F, 0.46F};
    return add(std::move(element));
}

void UiDocument::pointerMoved(glm::vec2 position) {
    for (UiElement& element : elements_)
        element.hovered = element.kind != UiElementKind::label &&
                          element.kind != UiElementKind::panel && element.bounds.contains(position);
}

std::optional<std::string> UiDocument::activate(glm::vec2 position) {
    pointerMoved(position);
    for (auto iterator = elements_.rbegin(); iterator != elements_.rend(); ++iterator)
        if ((iterator->kind == UiElementKind::button ||
             iterator->kind == UiElementKind::textField) &&
            iterator->bounds.contains(position))
            return iterator->id;
    return std::nullopt;
}

void UiDocument::focus(std::string_view id) {
    for (UiElement& element : elements_)
        element.focused = element.id == id;
}

void UiDocument::setText(std::string_view id, std::string text) {
    const auto found = std::find_if(
        elements_.begin(), elements_.end(), [&](const UiElement& item) { return item.id == id; });
    if (found != elements_.end())
        found->text = std::move(text);
}

bool UiDocument::hovered(std::string_view id) const {
    const auto found = std::find_if(
        elements_.begin(), elements_.end(), [&](const UiElement& item) { return item.id == id; });
    return found != elements_.end() && found->hovered;
}

bool UiDocument::focused(std::string_view id) const {
    const auto found = std::find_if(
        elements_.begin(), elements_.end(), [&](const UiElement& item) { return item.id == id; });
    return found != elements_.end() && found->focused;
}

} // namespace strategy
