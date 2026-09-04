#pragma once
#include "world/Entity.hpp"
#include <glm/vec2.hpp>
#include <string>
namespace strategy {
class World;
[[nodiscard]] float collisionRadius(const std::string& modelKey);
[[nodiscard]] bool overlapsObject(const World& world,glm::vec2 position,float radius,
                                  EntityId ignored=0);
}
