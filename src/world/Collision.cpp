#include "world/Collision.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "world/World.hpp"

#include <glm/geometric.hpp>
namespace strategy {
float collisionRadius(const DefinitionRegistry& definitions, const std::string& archetypeId) {
    return definitions.collisionRadius(archetypeId);
}
bool overlapsObject(const World& world,
                    const DefinitionRegistry& definitions,
                    glm::vec2 position,
                    float radius,
                    EntityId ignored) {
    for (const Entity& entity : world.entities()) {
        if (entity.id == ignored)
            continue;
        const float combined = radius + collisionRadius(definitions, entity.modelKey);
        const glm::vec2 delta =
            position - glm::vec2{entity.transform.position.x, entity.transform.position.z};
        if (glm::dot(delta, delta) < combined * combined)
            return true;
    }
    return false;
}
} // namespace strategy
