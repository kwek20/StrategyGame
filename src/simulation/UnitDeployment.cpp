#include "simulation/UnitDeployment.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <numbers>

namespace strategy {
std::optional<glm::vec2> findUnitDeployment(const World& world,
                                            const Terrain& terrain,
                                            const DefinitionRegistry& definitions,
                                            unsigned int mapChunksPerSide,
                                            const Entity& producer,
                                            const std::string& product) {
    const auto* source = definitions.archetype(producer.archetype);
    const auto* unit = definitions.archetype(product);
    if (!source || !unit)
        return std::nullopt;
    const MapArea map{mapChunksPerSide};
    const bool flying = hasMovementDomain(unit->movement.domains, MovementDomain::air);
    const float radius = flying ? 0.0F : unit->collisionRadius;
    const float margin = radius + definitions.matchRules().terrainEdgeMargin;
    const glm::vec2 origin{producer.transform.position.x, producer.transform.position.z};
    const float firstRadius = source->collisionRadius + radius + source->spawnClearance;
    const float spacing = std::max(1.0F, radius);
    constexpr float tau = 2 * std::numbers::pi_v<float>;
    // Snapshot static shapes once, rather than resolving every entity for every candidate.
    std::vector<SpatialShape> obstacles;
    if (!flying)
        for (const auto& entity : world.entities()) {
            if (entity.flight || (entity.kind == EntityKind::resource && entity.resource &&
                                  entity.resource.remaining <= 0))
                continue;
            const auto* type = definitions.archetype(entity.archetype);
            if (type && type->collisionRadius > 0)
                obstacles.push_back(spatialShape(definitions, entity));
        }
    const auto legal = [&](glm::vec2 position) {
        if (!map.contains(position, margin))
            return false;
        if (flying)
            return true;
        const SpatialShape footprint{FootprintShape::circle, position, radius};
        for (const auto& obstacle : obstacles)
            if (overlaps(footprint, obstacle))
                return false;
        return terrain.permitsMovementSegment(position, position, radius, unit->movement.domains);
    };
    float preferredAngle = 0.F;
    const bool hasRally = producer.production && producer.production.rallyPoint.has_value();
    if (hasRally) {
        const auto rally = *producer.production.rallyPoint;
        const auto shape = spatialShape(definitions, producer);
        const float clearance = radius + source->spawnClearance + .01F;
        const auto boundary = closestBoundaryPoint(shape, rally);
        const auto outward = rally - boundary;
        const auto preferred = signedDistance(shape, rally) > .001F && glm::length(outward) > .001F
                                   ? boundary + glm::normalize(outward) * clearance
                                   : closestBoundaryPoint(expanded(shape, clearance), rally);
        if (legal(preferred))
            return preferred;
        const auto direction = preferred - origin;
        preferredAngle = std::atan2(direction.y, direction.x);
    }
    const float farthest = glm::length(glm::abs(origin) + glm::vec2{map.halfExtent()});
    for (float distance = firstRadius; distance <= farthest + spacing; distance += spacing) {
        const auto samples =
            std::max(source->spawnCandidateCount,
                     static_cast<unsigned int>(std::ceil(tau * distance / spacing)));
        for (unsigned int candidate = 0; candidate < samples; ++candidate) {
            // Prefer the rally-facing side, then alternate to either side of that direction.
            const int offset = hasRally ? (candidate % 2 ? static_cast<int>((candidate + 1) / 2)
                                                         : -static_cast<int>(candidate / 2))
                                        : static_cast<int>(candidate);
            const float angle = preferredAngle + tau * offset / samples;
            const glm::vec2 position =
                origin + glm::vec2{std::cos(angle), std::sin(angle)} * distance;
            if (legal(position))
                return position;
        }
    }
    return std::nullopt;
}
} // namespace strategy
