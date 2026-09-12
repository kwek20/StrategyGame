#pragma once

#include "world/Entity.hpp"

#include <string>
#include <vector>

namespace strategy {

class DefinitionRegistry;
class World;

enum class HudBarKind { health, power };

struct HudBarModel {
    std::string label;
    float current{0.0F};
    float maximum{0.0F};
    HudBarKind kind{HudBarKind::health};
};

struct HudStatModel {
    std::string label;
    std::string value;
};

struct HudEntityCardModel {
    EntityId entity{0};
    std::string icon;
    std::vector<HudBarModel> bars;
};

struct HudSelectionGroupModel {
    std::string archetype;
    std::string name;
    std::string icon;
    std::size_t count{0};
};

struct HudActionModel {
    std::string id;
    std::string icon;
    std::string name;
    std::string description;
    std::string cost;
    std::string disabledReason;
    bool enabled{true};
    bool active{false};
    float progress{0.0F};
};

struct HudQueueItemModel {
    std::string icon;
    std::string name;
    float progress{0.0F};
    bool cancellable{false};
};

struct HudProcessorInputModel {
    std::string inputIcon, inputName, outputIcon, outputName;
    float buffered{0.0F}, capacity{0.0F}, outputPerInput{1.0F}, expectedOutput{0.0F};
};

struct EntityHudModel {
    std::string title;
    std::string portraitIcon;
    std::vector<HudBarModel> bars;
    std::vector<HudStatModel> stats;
    std::vector<HudEntityCardModel> cards;
    std::vector<HudActionModel> actions;
    std::vector<HudQueueItemModel> queue;
    std::vector<HudProcessorInputModel> processorInputs;
    std::string processorState;
    std::vector<HudSelectionGroupModel> selectionGroups;
    std::string footer;
    std::size_t totalEntities{0};
};

class EntityHudModelBuilder final {
  public:
    [[nodiscard]] static EntityHudModel build(const World& world,
                                              EntityId selected,
                                              const std::vector<EntityId>& selection,
                                              const DefinitionRegistry& definitions);
};

} // namespace strategy
