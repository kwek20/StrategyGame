#include "simulation/UnitMovement.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"
#include "world/Navigation.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <numbers>
#include <limits>

namespace strategy {
namespace {
bool segmentTouchesShape(glm::vec2 start, glm::vec2 end, const SpatialShape& shape, float radius) {
    if (shape.kind == FootprintShape::circle) {
        const auto delta = end - start;
        const float lengthSquared = glm::dot(delta, delta);
        const float t = lengthSquared > 0 ? std::clamp(glm::dot(shape.center - start, delta) / lengthSquared, 0.F, 1.F) : 0.F;
        return glm::length(start + delta * t - shape.center) < shape.radius + radius;
    }
    const float angle = glm::radians(-shape.rotationDegrees);
    const auto local = [&](glm::vec2 point) {
        point -= shape.center;
        return glm::vec2{std::cos(angle) * point.x - std::sin(angle) * point.y,
                         std::sin(angle) * point.x + std::cos(angle) * point.y};
    };
    const auto a = local(start), delta = local(end) - a;
    const auto bounds = shape.halfExtents + glm::vec2{radius};
    float first = 0, last = 1;
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(delta[axis]) < .00001F) {
            if (std::abs(a[axis]) > bounds[axis]) return false;
        } else {
            const float t0 = (-bounds[axis] - a[axis]) / delta[axis];
            const float t1 = (bounds[axis] - a[axis]) / delta[axis];
            first = std::max(first, std::min(t0, t1));
            last = std::min(last, std::max(t0, t1));
            if (first > last) return false;
        }
    }
    return true;
}
} // namespace

NavigationProfile navigationProfile(const DefinitionRegistry& definitions, const Entity& entity) {
    const auto* type = definitions.archetype(entity.archetype);
    return type ? NavigationProfile{type->movement.domains, type->movement.ignoresEntityObstacles}
                : NavigationProfile{};
}

glm::vec2 avoidUnitTraffic(const World& world, const DefinitionRegistry& definitions,
                           const Entity& unit, glm::vec2 direction, float remainingDistance) {
    if (unit.flight || unit.authority.directController != 0 || glm::length(direction) < .001F)
        return direction;
    const auto forward = glm::normalize(direction);
    const glm::vec2 right{forward.y, -forward.x};
    const glm::vec2 origin{unit.transform.position.x, unit.transform.position.z};
    const float radius = collisionRadius(definitions, unit.archetype);
    float nearest = std::numeric_limits<float>::infinity(), bias = 0, nearClearance = 0;
    glm::vec2 nearRelative{0};
    for (const auto& other : world.entities()) {
        if (other.id == unit.id || other.flight || !other.unitControl) continue;
        const auto relative = glm::vec2{other.transform.position.x, other.transform.position.z} - origin;
        const float ahead = glm::dot(relative, forward), side = glm::dot(relative, right);
        const float clearance = radius + collisionRadius(definitions, other.archetype) + .6F;
        if (ahead <= 0 || ahead >= std::min(remainingDistance, clearance + 3.F) ||
            std::abs(side) >= clearance || ahead >= nearest) continue;
        nearest = ahead;
        nearRelative = relative;
        nearClearance = clearance;
        // Continue on the side already approached; exact head-on traffic keeps right.
        const float sign = side > .25F ? -1.F : 1.F;
        bias = sign * 2.5F * (1.F - std::abs(side) / clearance);
    }
    if (bias == 0) return direction;
    auto adjusted = direction + right * bias;
    const float distance = glm::length(nearRelative);
    if (distance > .001F && distance < nearClearance + .75F) {
        const auto toward = nearRelative / distance;
        const float closing = glm::dot(adjusted, toward);
        if (closing > 0)
            adjusted -= toward * (closing + .25F); // Tangent with a little room to separate.
    }
    return glm::length(adjusted) > .001F ? glm::normalize(adjusted) : glm::vec2{0};
}

bool clearUnitRouteSegment(const World& world, const Terrain& terrain,
                           const DefinitionRegistry& definitions, const Entity& unit,
                           glm::vec2 start, glm::vec2 end) {
    const auto profile = navigationProfile(definitions, unit);
    const float radius = unit.flight ? 0.F : collisionRadius(definitions, unit.archetype);
    if (!terrain.permitsMovementSegment(start, end, radius, profile.domains)) return false;
    if (profile.ignoresEntityObstacles) return true;
    for (const auto& other : world.entities()) {
        if (other.id == unit.id || other.flight ||
            (other.kind == EntityKind::resource && other.resource && other.resource.remaining <= 0)) continue;
        const auto* type = definitions.archetype(other.archetype);
        if (type && type->collisionRadius > 0 &&
            segmentTouchesShape(start, end, spatialShape(definitions, other), radius)) return false;
    }
    return true;
}

std::vector<glm::vec3> findUnitArrivalPath(
    Navigation& navigation, const World& world, const Terrain& terrain,
    const DefinitionRegistry& definitions, unsigned mapChunksPerSide,
    const Entity& unit, glm::vec3 requested) {
    const auto profile = navigationProfile(definitions, unit);
    const float radius = unit.flight ? 0.F : collisionRadius(definitions, unit.archetype);
    const float arrivalRadius = std::max(.5F, radius);
    const MapArea map{mapChunksPerSide};
    const auto available = [&](glm::vec2 point) {
        if (!map.contains(point, radius + definitions.matchRules().terrainEdgeMargin) ||
            !terrain.permitsMovementSegment(point, point, radius, profile.domains) ||
            (!profile.ignoresEntityObstacles && overlapsObject(world, definitions, point, radius, unit.id))) return false;
        for (const auto& other : world.entities()) {
            if (other.id == unit.id || other.authority.owner != unit.authority.owner ||
                !other.unitControl || bool(other.flight) != bool(unit.flight)) continue;
            const float otherRadius = std::max(.5F, other.flight ? 0.F : collisionRadius(definitions, other.archetype));
            // Parked arrivals must leave a lane for later members to reach inner slots.
            const float lane = unit.flight ? 0.F : 2.F * std::max(arrivalRadius, otherRadius);
            const float separation = arrivalRadius + otherRadius + lane + .6F;
            const auto destination = other.authority.directController == 0 && other.unitControl.hasStrategicDestination &&
                                             other.unitControl.order == UnitOrderKind::move
                                         ? other.unitControl.strategicDestination : other.transform.position;
            if (glm::length(point - glm::vec2{destination.x, destination.z}) < separation) return false;
        }
        return true;
    };
    const glm::vec2 center{requested.x, requested.z};
    const auto route = [&](glm::vec2 point) {
        if (!available(point)) return std::vector<glm::vec3>{};
        auto path = navigation.findPath(terrain, world, unit.transform.position,
            {point.x, requested.y, point.y}, radius, unit.id, profile);
        if (!path.empty() && !available({path.back().x, path.back().z})) path.clear();
        return path;
    };
    if (auto path = route(center); !path.empty()) return path;
    // Keep out-of-map/hidden-barrier handling in navigation; this search only distributes
    // nearby arrivals. A bounded neighbourhood prevents one order walking across the map.
    const float spacing = unit.flight ? arrivalRadius * 2 + .8F : arrivalRadius * 4 + 1.2F;
    const auto count = std::count_if(world.entities().begin(), world.entities().end(), [&](const auto& other) {
        return other.unitControl && other.authority.owner == unit.authority.owner;
    });
    const float limit = std::min(32.F, std::max(12.F, spacing * std::sqrt(static_cast<float>(count + 1))));
    const auto approach = glm::vec2{unit.transform.position.x, unit.transform.position.z} - center;
    const float base = std::atan2(approach.y, approach.x);
    unsigned routeAttempts = 0;
    for (float distance = spacing; distance <= limit; distance += spacing) {
        const unsigned samples = std::max(12U, static_cast<unsigned>(std::ceil(2 * std::numbers::pi_v<float> * distance / spacing)));
        for (unsigned i = 0; i < samples; ++i) {
            const int offset = i % 2 ? static_cast<int>((i + 1) / 2) : -static_cast<int>(i / 2);
            const float angle = base + offset * 2 * std::numbers::pi_v<float> / samples;
            const auto point = center + glm::vec2{std::cos(angle), std::sin(angle)} * distance;
            if (!available(point)) continue;
            if (auto path = route(point); !path.empty()) return path;
            if (++routeAttempts == 8) return {};
        }
    }
    return {};
}
} // namespace strategy
