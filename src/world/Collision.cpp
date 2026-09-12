#include "world/Collision.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "world/World.hpp"

#include <glm/geometric.hpp>
namespace strategy {
float collisionRadius(const DefinitionRegistry& definitions, const std::string& archetypeId) {
    return definitions.collisionRadius(archetypeId);
}

SpatialShape spatialShape(const DefinitionRegistry& definitions,
                          EntityArchetypeId archetype,
                          glm::vec2 position,
                          float rotationDegrees) {
    SpatialShape result;
    result.center = position;
    const EntityArchetype* definition = definitions.archetype(archetype);
    if (definition && definition->footprint) {
        result.kind = definition->footprint->shape;
        result.radius = definition->footprint->radius;
        result.halfExtents = definition->footprint->halfExtents;
        result.rotationDegrees = rotationDegrees + definition->footprint->rotationDegrees;
    } else {
        result.kind = FootprintShape::circle;
        result.radius = collisionRadius(definitions, archetype);
        result.halfExtents = glm::vec2{result.radius};
        result.rotationDegrees = rotationDegrees;
    }
    return result;
}

SpatialShape spatialShape(const DefinitionRegistry& definitions, const Entity& entity) {
    return spatialShape(definitions,
                        entity.archetype,
                        {entity.transform.position.x, entity.transform.position.z},
                        entity.transform.rotationDegrees.y);
}

bool overlapsObject(const World& world,
                    const DefinitionRegistry& definitions,
                    const SpatialShape& shape,
                    EntityId ignored) {
    for (const Entity& entity : world.entities()) {
        if (entity.id == ignored || entity.flight ||
            (entity.resource && entity.resource.remaining <= 0.0F))
            continue;
        if (overlaps(shape, spatialShape(definitions, entity)))
            return true;
    }
    return false;
}

bool overlapsObject(const World& world,
                    const DefinitionRegistry& definitions,
                    glm::vec2 position,
                    float radius,
                    EntityId ignored) {
    return overlapsObject(world,
                          definitions,
                          SpatialShape{FootprintShape::circle, position, radius},
                          ignored);
}
} // namespace strategy
