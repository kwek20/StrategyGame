#pragma once
#include "world/Entity.hpp"

#include <glm/vec2.hpp>
#include <string>
namespace strategy {
class DefinitionRegistry;
class World;
[[nodiscard]] float collisionRadius(const DefinitionRegistry& definitions,
                                    const std::string& archetypeId);
[[nodiscard]] inline float collisionRadius(const DefinitionRegistry& definitions,
                                           EntityArchetypeId archetypeId) {
    return collisionRadius(definitions, archetypeId.value);
}
[[nodiscard]] bool
overlapsObject(const World& world,
               const DefinitionRegistry& definitions,
               glm::vec2 position,
               float radius,
               EntityId ignored = 0);
} // namespace strategy
