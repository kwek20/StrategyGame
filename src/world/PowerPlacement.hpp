#pragma once

#include "world/Entity.hpp"

namespace strategy {
class World;

// Shared by authoritative auto-connection and placement assistance. Planned endpoints reserve
// links too; power only flows once both buildings are operational.
[[nodiscard]] float powerPlacementRange(const Entity& source, const Entity& target, PlayerId owner);
struct PowerPlacementTargets {
    const Entity* inRange{};
    const Entity* nearest{};
};
[[nodiscard]] PowerPlacementTargets
findPowerPlacementTargets(const World& world, PlayerId owner, const Entity& source);
[[nodiscard]] const Entity* findPowerPlacementTarget(const World& world,
                                                     PlayerId owner,
                                                     const Entity& source,
                                                     bool requireInRange = true);
[[nodiscard]] glm::vec3
clampPowerPlacementPosition(glm::vec3 position, const Entity& target, float range);

// Transient input assistance only; never saved or used to bypass placement validation.
struct PowerPlacementAssist {
    EntityId anchor{};
    bool lockLost{};
    [[nodiscard]] glm::vec3
    position(const World& world, PlayerId owner, const Entity& source, bool lockRange);
};
} // namespace strategy
