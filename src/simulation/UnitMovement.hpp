#pragma once

#include "world/Entity.hpp"
#include "world/MovementDomain.hpp"
#include <vector>

namespace strategy {
class DefinitionRegistry;
class Terrain;
class World;
class Navigation;
[[nodiscard]] NavigationProfile navigationProfile(const DefinitionRegistry&, const Entity&);
[[nodiscard]] glm::vec2 avoidUnitTraffic(const World&, const DefinitionRegistry&,
                                        const Entity&, glm::vec2 direction, float remainingDistance);

// Existing move destinations reserve arrival space; no independent reservation lifecycle.
[[nodiscard]] std::vector<glm::vec3> findUnitArrivalPath(
    Navigation&, const World&, const Terrain&, const DefinitionRegistry&,
    unsigned mapChunksPerSide, const Entity&, glm::vec3 requested);

// Terrain, static obstacles and current ground-unit footprints constrain route shortcuts.
[[nodiscard]] bool clearUnitRouteSegment(
    const World&, const Terrain&, const DefinitionRegistry&, const Entity&,
    glm::vec2 start, glm::vec2 end);
} // namespace strategy
