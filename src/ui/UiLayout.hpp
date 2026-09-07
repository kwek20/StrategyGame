#pragma once

#include "ui/UiDocument.hpp"

#include <cstddef>
#include <vector>

namespace strategy {

enum class UiAnchor { topLeft, topCenter, topRight, center, bottomLeft, bottomCenter, bottomRight };

class UiLayout final {
  public:
    UiLayout(int viewportWidth, int viewportHeight, float userScale = 1.0F);

    [[nodiscard]] UiRect rect(UiAnchor anchor, float offsetX, float offsetY,
                              float width, float height,
                              float minimumWidth = 0.0F,
                              float minimumHeight = 0.0F) const;
    [[nodiscard]] UiRect inset(UiRect parent, float padding) const;
    [[nodiscard]] std::vector<UiRect> row(UiRect bounds, std::size_t count,
                                          float gap, float minimumItemWidth) const;
    [[nodiscard]] std::vector<UiRect> column(UiRect bounds, std::size_t count,
                                             float gap, float minimumItemHeight) const;
    [[nodiscard]] float scale() const { return scale_; }
    [[nodiscard]] float value(float referencePixels) const { return referencePixels * scale_; }

  private:
    float viewportWidth_;
    float viewportHeight_;
    float scale_;
};

} // namespace strategy
