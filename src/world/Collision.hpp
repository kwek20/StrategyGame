#pragma once
#include "world/Entity.hpp"
#include "world/SpatialShape.hpp"

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
[[nodiscard]] SpatialShape spatialShape(const DefinitionRegistry& definitions,
                                        const Entity& entity);
[[nodiscard]] SpatialShape spatialShape(const DefinitionRegistry& definitions,
                                        EntityArchetypeId archetype,
                                        glm::vec2 position,
                                        float rotationDegrees = 0.0F);
[[nodiscard]] bool overlapsObject(const World& world,
                                  const DefinitionRegistry& definitions,
                                  const SpatialShape& shape,
                                  EntityId ignored = 0);
[[nodiscard]] bool
overlapsObject(const World& world,
               const DefinitionRegistry& definitions,
               glm::vec2 position,
               float radius,
               EntityId ignored = 0);
} // namespace strategy
