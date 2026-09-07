#include "ui/UiLayout.hpp"

#include <algorithm>

namespace strategy {

UiLayout::UiLayout(int viewportWidth, int viewportHeight, float userScale)
    : viewportWidth_(static_cast<float>(viewportWidth))
    , viewportHeight_(static_cast<float>(viewportHeight))
    , scale_(std::clamp(std::min(viewportWidth_ / 1280.0F, viewportHeight_ / 720.0F) *
                            std::clamp(userScale, 0.75F, 1.5F),
                        0.65F, 2.5F)) {}

UiRect UiLayout::rect(UiAnchor anchor, float offsetX, float offsetY,
                      float width, float height, float minimumWidth,
                      float minimumHeight) const {
    const float w = std::max(width, minimumWidth) * scale_;
    const float h = std::max(height, minimumHeight) * scale_;
    const float x = offsetX * scale_, y = offsetY * scale_;
    float left = x, top = y;
    switch (anchor) {
    case UiAnchor::topCenter: left = viewportWidth_ * 0.5F - w * 0.5F + x; break;
    case UiAnchor::topRight: left = viewportWidth_ - x - w; break;
    case UiAnchor::center:
        left = viewportWidth_ * 0.5F - w * 0.5F + x;
        top = viewportHeight_ * 0.5F - h * 0.5F + y;
        break;
    case UiAnchor::bottomLeft: top = viewportHeight_ - y - h; break;
    case UiAnchor::bottomCenter:
        left = viewportWidth_ * 0.5F - w * 0.5F + x;
        top = viewportHeight_ - y - h;
        break;
    case UiAnchor::bottomRight:
        left = viewportWidth_ - x - w;
        top = viewportHeight_ - y - h;
        break;
    case UiAnchor::topLeft: break;
    }
    return {left, top, left + w, top + h};
}

UiRect UiLayout::inset(UiRect parent, float padding) const {
    const float p = value(padding);
    return {parent.left + p, parent.top + p, parent.right - p, parent.bottom - p};
}

std::vector<UiRect> UiLayout::row(UiRect bounds, std::size_t count,
                                  float gap, float minimumItemWidth) const {
    if (count == 0) return {};
    const float spacing = value(gap);
    const float available = std::max(0.0F, bounds.right - bounds.left - spacing * (count - 1));
    const float width = std::max(value(minimumItemWidth), available / count);
    std::vector<UiRect> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float left = bounds.left + i * (width + spacing);
        result.push_back({left, bounds.top, std::min(left + width, bounds.right), bounds.bottom});
    }
    return result;
}

std::vector<UiRect> UiLayout::column(UiRect bounds, std::size_t count,
                                     float gap, float minimumItemHeight) const {
    if (count == 0) return {};
    const float spacing = value(gap);
    const float available = std::max(0.0F, bounds.bottom - bounds.top - spacing * (count - 1));
    const float height = std::max(value(minimumItemHeight), available / count);
    std::vector<UiRect> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float top = bounds.top + i * (height + spacing);
        result.push_back({bounds.left, top, bounds.right, std::min(top + height, bounds.bottom)});
    }
    return result;
}

} // namespace strategy
