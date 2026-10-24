#pragma once
#include "world/Entity.hpp"

namespace strategy {
// Transient presentation notification. Damage is already resolved by the fixed-tick simulation.
struct CombatShotEvent {
    EntityId attacker{}, target{};
    std::uint64_t shot{};
    glm::vec3 origin{}, destination{};
    bool hit{};
    bool destroyed{};
    bool impact{};
    bool cannon{};
};
} // namespace strategy
