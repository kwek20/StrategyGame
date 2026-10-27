#pragma once

#include <glm/vec2.hpp>

namespace strategy {
struct Entity;
struct MovementDefinition;

// Direct control only. RTS path following keeps its own waypoint steering rules.
[[nodiscard]] glm::vec2 driveVehicle(Entity&, const MovementDefinition&, float topSpeed, float dt);
} // namespace strategy
