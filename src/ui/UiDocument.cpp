#include "ui/UiDocument.hpp"

#include <algorithm>
#include <cmath>

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

UiElement& UiDocument::iconButton(std::string id, UiRect bounds, std::string icon,
                                  std::string tooltipText, bool enabled) {
    UiElement element{std::move(id), UiElementKind::iconButton, bounds};
    element.icon = std::move(icon);
    element.tooltip = std::move(tooltipText);
    element.enabled = enabled;
    return add(std::move(element));
}

UiElement& UiDocument::progressBar(std::string id, UiRect bounds, float progress,
                                   glm::vec3 color) {
    UiElement element{std::move(id), UiElementKind::progressBar, bounds};
    element.progress = std::clamp(progress, 0.0F, 1.0F);
    element.hoverColor = color;
    return add(std::move(element));
}

UiElement& UiDocument::entityCard(std::string id, UiRect bounds, std::string icon,
                                  std::string tooltipText) {
    UiElement element{std::move(id), UiElementKind::entityCard, bounds};
    element.icon = std::move(icon);
    element.tooltip = std::move(tooltipText);
    return add(std::move(element));
}

UiElement& UiDocument::queueSlot(std::string id, UiRect bounds, std::string icon,
                                 std::string tooltipText, bool enabled) {
    UiElement element{std::move(id), UiElementKind::queueSlot, bounds};
    element.icon = std::move(icon);
    element.tooltip = std::move(tooltipText);
    element.enabled = enabled;
    return add(std::move(element));
}

UiElement& UiDocument::modal(std::string id, UiRect bounds, glm::vec3 color) {
    UiElement element{std::move(id), UiElementKind::modalPanel, bounds};
    element.color = color;
    return add(std::move(element));
}

UiElement& UiDocument::scrollList(std::string id, UiRect bounds) {
    return add({std::move(id), UiElementKind::scrollList, bounds});
}

UiElement& UiDocument::tooltip(std::string id, UiRect bounds, std::string text) {
    UiElement element{std::move(id), UiElementKind::tooltip, bounds, std::move(text)};
    element.color = {0.02F, 0.03F, 0.04F};
    return add(std::move(element));
}

void UiDocument::pointerMoved(glm::vec2 position) {
    for (UiElement& element : elements_)
        element.hovered = (element.kind == UiElementKind::button ||
                           element.kind == UiElementKind::iconButton ||
                           element.kind == UiElementKind::textField ||
                           element.kind == UiElementKind::entityCard ||
                           element.kind == UiElementKind::queueSlot) &&
                          element.bounds.contains(position);
}

std::optional<std::string> UiDocument::activate(glm::vec2 position) {
    pointerMoved(position);
    for (auto iterator = elements_.rbegin(); iterator != elements_.rend(); ++iterator)
        if (iterator->enabled && (iterator->kind == UiElementKind::button ||
             iterator->kind == UiElementKind::iconButton ||
             iterator->kind == UiElementKind::textField ||
             iterator->kind == UiElementKind::entityCard ||
             iterator->kind == UiElementKind::queueSlot) &&
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

const UiElement* UiDocument::find(std::string_view id) const {
    const auto found = std::find_if(elements_.begin(), elements_.end(),
        [&](const UiElement& item) { return item.id == id; });
    return found == elements_.end() ? nullptr : &*found;
}

const UiElement* UiDocument::hitTest(glm::vec2 position) const {
    for (auto iterator = elements_.rbegin(); iterator != elements_.rend(); ++iterator)
        if (iterator->bounds.contains(position) &&
            (iterator->kind == UiElementKind::button ||
             iterator->kind == UiElementKind::iconButton ||
             iterator->kind == UiElementKind::textField ||
             iterator->kind == UiElementKind::entityCard ||
             iterator->kind == UiElementKind::queueSlot))
            return &*iterator;
    return nullptr;
}

const UiElement* UiDocument::hoveredElement() const {
    const auto found = std::find_if(elements_.rbegin(), elements_.rend(),
        [](const UiElement& item) { return item.hovered; });
    return found == elements_.rend() ? nullptr : &*found;
}

void UiDocument::scaleFromReference(int viewportWidth, int viewportHeight, float userScale) {
    const float resolutionScale =
        std::min(viewportWidth / 1280.0F, viewportHeight / 720.0F);
    const float requestedScale = resolutionScale * std::clamp(userScale, 0.75F, 1.5F);
    float minLeft = 1280.0F, minTop = 720.0F, maxRight = 0.0F, maxBottom = 0.0F;
    for (const UiElement& element : elements_) {
        if (element.bounds.right <= element.bounds.left ||
            element.bounds.bottom <= element.bounds.top)
            continue;
        minLeft = std::min(minLeft, element.bounds.left);
        minTop = std::min(minTop, element.bounds.top);
        maxRight = std::max(maxRight, element.bounds.right);
        maxBottom = std::max(maxBottom, element.bounds.bottom);
    }
    const float contentWidth = std::max(1.0F, maxRight - minLeft);
    const float contentHeight = std::max(1.0F, maxBottom - minTop);
    const float safeFitScale = std::min(
        (viewportWidth - 60.0F) / contentWidth, (viewportHeight - 60.0F) / contentHeight);
    const float scale = std::clamp(std::min(requestedScale, safeFitScale), 0.65F, 2.5F);
    const float offsetX = viewportWidth * 0.5F - (minLeft + maxRight) * 0.5F * scale;
    const float offsetY = viewportHeight * 0.5F - (minTop + maxBottom) * 0.5F * scale;
    for (UiElement& element : elements_) {
        element.bounds.left = offsetX + element.bounds.left * scale;
        element.bounds.top = offsetY + element.bounds.top * scale;
        element.bounds.right = offsetX + element.bounds.right * scale;
        element.bounds.bottom = offsetY + element.bounds.bottom * scale;
        element.textScale *= scale;
    }
}

} // namespace strategy
