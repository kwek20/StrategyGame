#include "world/PowerPlacement.hpp"

#include "world/World.hpp"

#include <algorithm>
#include <glm/geometric.hpp>
#include <limits>

namespace strategy {
float powerPlacementRange(const Entity& source, const Entity& target, PlayerId owner) {
    if (source.id == target.id || source.authority.owner != owner ||
        target.authority.owner != owner || !source.power || !target.power ||
        !source.power.enabled || !target.power.enabled ||
        (target.construction && target.construction.state == BuildingLifecycleState::destroyed) ||
        source.power.maximumConnections == 0 || target.power.maximumConnections == 0 ||
        source.power.connections.size() >= source.power.maximumConnections ||
        target.power.connections.size() >= target.power.maximumConnections)
        return 0.0F;
    return std::max(0.0F, std::min(source.power.connectionRange, target.power.connectionRange));
}

PowerPlacementTargets
findPowerPlacementTargets(const World& world, PlayerId owner, const Entity& source) {
    PowerPlacementTargets result;
    float nearestDistance = std::numeric_limits<float>::max();
    float inRangeDistance = nearestDistance;
    for (const Entity& candidate : world.entities()) {
        const float range = powerPlacementRange(source, candidate, owner);
        if (range <= 0.0F)
            continue;
        const glm::vec2 delta{source.transform.position.x - candidate.transform.position.x,
                              source.transform.position.z - candidate.transform.position.z};
        const float distance = glm::dot(delta, delta);
        const auto nearer = [&](const Entity* previous, float previousDistance) {
            return !previous || distance < previousDistance ||
                   (distance == previousDistance && candidate.id < previous->id);
        };
        if (nearer(result.nearest, nearestDistance)) {
            result.nearest = &candidate;
            nearestDistance = distance;
        }
        if (distance <= range * range && nearer(result.inRange, inRangeDistance)) {
            result.inRange = &candidate;
            inRangeDistance = distance;
        }
    }
    return result;
}

const Entity* findPowerPlacementTarget(const World& world,
                                       PlayerId owner,
                                       const Entity& source,
                                       bool requireInRange) {
    const auto targets = findPowerPlacementTargets(world, owner, source);
    return requireInRange ? targets.inRange : targets.nearest;
}

glm::vec3 clampPowerPlacementPosition(glm::vec3 position, const Entity& target, float range) {
    if (range <= 0.0F)
        return position;
    const glm::vec2 center{target.transform.position.x, target.transform.position.z};
    const glm::vec2 delta = glm::vec2{position.x, position.z} - center;
    const float distance = glm::length(delta);
    // Stay just inside the authoritative boundary despite floating-point rounding.
    const float safeRange = range * (1.0F - 0.00001F);
    if (glm::dot(delta, delta) > range * range) {
        const glm::vec2 clamped = center + delta * (safeRange / distance);
        position.x = clamped.x;
        position.z = clamped.y;
    }
    return position;
}

glm::vec3 PowerPlacementAssist::position(const World& world,
                                         PlayerId owner,
                                         const Entity& source,
                                         bool lockRange) {
    if (!lockRange) {
        anchor = 0;
        lockLost = false;
        return source.transform.position;
    }
    if (anchor) {
        const auto* target = world.findEntity(anchor);
        if (!target || powerPlacementRange(source, *target, owner) <= 0.0F) {
            anchor = 0;
            lockLost = true;
        }
    }
    if (!anchor && !lockLost)
        if (const auto* target = findPowerPlacementTarget(world, owner, source))
            anchor = target->id;
    if (const auto* target = world.findEntity(anchor))
        return clampPowerPlacementPosition(
            source.transform.position, *target, powerPlacementRange(source, *target, owner));
    return source.transform.position;
}
} // namespace strategy
