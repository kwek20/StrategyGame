#pragma once

#include "ui/EntityHudModel.hpp"
#include "ui/UiDocument.hpp"

namespace strategy {

class EntityHudLayout final {
  public:
    [[nodiscard]] static UiDocument construction(const EntityHudModel& model,
                                                 int viewportWidth,
                                                 int viewportHeight,
                                                 float uiScale = 1.0F);
    [[nodiscard]] static UiDocument actions(const EntityHudModel& model,
                                            int viewportWidth,
                                            int viewportHeight,
                                            float uiScale = 1.0F);
    [[nodiscard]] static UiDocument selection(const EntityHudModel& model,
                                              int viewportWidth,
                                              int viewportHeight,
                                              float uiScale = 1.0F);
    [[nodiscard]] static UiDocument directControl(const EntityHudModel& model,
                                                  int viewportWidth,
                                                  int viewportHeight,
                                                  float uiScale = 1.0F);

    [[nodiscard]] static std::string actionElementId(std::string_view actionId);
    [[nodiscard]] static std::string queueElementId(std::size_t index);
    [[nodiscard]] static std::optional<std::string> actionId(std::string_view elementId);
    [[nodiscard]] static std::optional<std::size_t> queueIndex(std::string_view elementId);
    [[nodiscard]] static std::string selectionElementId(std::string_view archetype);
    [[nodiscard]] static std::optional<std::string> selectionArchetype(std::string_view elementId);
};

} // namespace strategy
