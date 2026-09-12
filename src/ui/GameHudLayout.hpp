#pragma once

#include "ui/UiDocument.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace strategy {

class GameHudLayout final {
  public:
    [[nodiscard]] static UiDocument resources(std::size_t localResourceCount,
                                              std::size_t powerDeviceCount,
                                              bool powerOverlayVisible,
                                              int viewportWidth,
                                              int viewportHeight,
                                              float uiScale = 1.0F);
    [[nodiscard]] static UiDocument minimap(int viewportWidth,
                                           int viewportHeight,
                                           float uiScale = 1.0F);
    [[nodiscard]] static UiDocument loading(float progress,
                                            const std::string& status,
                                            int viewportWidth,
                                            int viewportHeight,
                                            float uiScale = 1.0F);
    [[nodiscard]] static UiDocument alerts(const std::vector<std::string>& messages,
                                           int viewportWidth, int viewportHeight,
                                           float uiScale = 1.0F);
};

} // namespace strategy
