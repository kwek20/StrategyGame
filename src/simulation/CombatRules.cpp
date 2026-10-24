#include "simulation/CombatRules.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "players/PlayerRegistry.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace strategy {
namespace {
bool slab(float start, float delta, float low, float high, float& enter, float& exit) {
    if (std::abs(delta) < 0.000001F)
        return start >= low && start <= high;
    float a = (low - start) / delta, b = (high - start) / delta;
    if (a > b)
        std::swap(a, b);
    enter = std::max(enter, a);
    exit = std::min(exit, b);
    return enter <= exit;
}
bool intersectsBuilding(glm::vec3 start,
                        glm::vec3 end,
                        const SpatialShape& shape,
                        float bottom,
                        float height,
                        float* fraction = nullptr) {
    const auto delta = end - start;
    float enter = 0, exit = 1;
    if (!slab(start.y, delta.y, bottom, bottom + height, enter, exit))
        return false;
    glm::vec2 p = glm::vec2{start.x, start.z} - shape.center, d{delta.x, delta.z};
    if (shape.kind == FootprintShape::rectangle) {
        const float a = glm::radians(shape.rotationDegrees), c = std::cos(a), s = std::sin(a);
        p = {c * p.x + s * p.y, -s * p.x + c * p.y};
        d = {c * d.x + s * d.y, -s * d.x + c * d.y};
        const bool hit = slab(p.x, d.x, -shape.halfExtents.x, shape.halfExtents.x, enter, exit) &&
                         slab(p.y, d.y, -shape.halfExtents.y, shape.halfExtents.y, enter, exit);
        if (hit && fraction)
            *fraction = enter;
        return hit;
    }
    const float a = glm::dot(d, d), b = 2 * glm::dot(p, d),
                c = glm::dot(p, p) - shape.radius * shape.radius;
    if (a < 0.000001F) {
        if (c <= 0 && fraction)
            *fraction = enter;
        return c <= 0;
    }
    const float discriminant = b * b - 4 * a * c;
    if (discriminant < 0)
        return false;
    enter = std::max(enter, (-b - std::sqrt(discriminant)) / (2 * a));
    exit = std::min(exit, (-b + std::sqrt(discriminant)) / (2 * a));
    if (enter <= exit && fraction)
        *fraction = enter;
    return enter <= exit;
}
} // namespace

const WeaponDefinition* primaryWeapon(const DefinitionRegistry& definitions, const Entity& actor) {
    const auto* type = definitions.archetype(actor.archetype);
    return actor.combat && type && !type->weapons.empty()
               ? definitions.weapon(type->weapons.front())
               : nullptr;
}
bool validCombatTarget(const DefinitionRegistry& definitions,
                       const PlayerRegistry& players,
                       const Entity& actor,
                       const Entity& target) {
    const auto* weapon = primaryWeapon(definitions, actor);
    const auto* owner = players.find(actor.authority.owner);
    const auto* enemy = players.find(target.authority.owner);
    if (!weapon || !target.health || !owner || !enemy || owner->team == enemy->team ||
        (target.health.current <= 0 && (!target.construction || isOperational(target))))
        return false;
    if (target.flight)
        return weapon->validTargets.contains("air");
    return weapon->validTargets.contains("ground") ||
           (target.kind == EntityKind::building && weapon->validTargets.contains("structure"));
}
bool combatTargetVisible(const PlayerRegistry& players,
                         const Entity& actor,
                         const Entity& target,
                         std::uint32_t /*chunks*/) {
    const auto* player = players.find(actor.authority.owner);
    if (!player)
        return false;
    return player->visibility.visibleAt({target.transform.position.x, target.transform.position.z});
}
glm::vec3 combatAimPoint(const DefinitionRegistry& definitions,
                         const Terrain& terrain,
                         const Entity& entity) {
    const auto* type = definitions.archetype(entity.archetype);
    auto position = entity.transform.position;
    position.y += terrain.entityBaseHeight(entity.id, position.x, position.z) +
                  std::min(type ? type->bodyHeight * .5F : 1.F, 2.F);
    return position;
}
glm::vec3
combatMuzzle(const Terrain& terrain, const Entity& actor, const WeaponDefinition& weapon) {
    auto position = actor.transform.position;
    const float yaw =
        glm::radians(weapon.turretTurnRateDegrees > 0 ? actor.combat.turretYawDegrees
                                                      : actor.transform.rotationDegrees.y);
    const float pitch = glm::radians(actor.combat.turretPitchDegrees);
    const float reach = weapon.muzzleForward - weapon.pitchPivotForward;
    position.y += terrain.entityBaseHeight(actor.id, position.x, position.z) + weapon.muzzleHeight;
    position.y += std::sin(pitch) * reach;
    const float forward = weapon.pitchPivotForward + std::cos(pitch) * reach;
    position.x += std::sin(yaw) * forward + std::cos(yaw) * weapon.muzzleSide;
    position.z += std::cos(yaw) * forward - std::sin(yaw) * weapon.muzzleSide;
    return position;
}
glm::vec3 weaponDirection(const Entity& actor, const WeaponDefinition& weapon) {
    const float yaw =
        glm::radians(weapon.turretTurnRateDegrees > 0 ? actor.combat.turretYawDegrees
                                                      : actor.transform.rotationDegrees.y);
    const float pitch = glm::radians(actor.combat.turretPitchDegrees);
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}
float aimWeapon(Entity& actor,
                const Terrain& terrain,
                const WeaponDefinition& weapon,
                glm::vec3 point,
                float dt) {
    auto pivot = actor.transform.position;
    pivot.y += terrain.entityBaseHeight(actor.id, pivot.x, pivot.z) + weapon.muzzleHeight;
    const auto d = point - pivot;
    const float yaw = glm::degrees(std::atan2(d.x, d.z));
    const float pitch =
        glm::degrees(std::atan2(d.y, std::max(.001F, glm::length(glm::vec2{d.x, d.z}))));
    if (weapon.turretTurnRateDegrees <= 0)
        return turnToward(actor, yaw, 0, dt);
    const float yawError = std::remainder(yaw - actor.combat.turretYawDegrees, 360.F);
    actor.combat.turretYawDegrees = std::remainder(
        actor.combat.turretYawDegrees + std::clamp(yawError,
                                                   -weapon.turretTurnRateDegrees * dt,
                                                   weapon.turretTurnRateDegrees * dt),
        360.F);
    const float desiredPitch =
        std::clamp(pitch, weapon.minimumPitchDegrees, weapon.maximumPitchDegrees);
    actor.combat.turretPitchDegrees += std::clamp(desiredPitch - actor.combat.turretPitchDegrees,
                                                  -weapon.turretPitchRateDegrees * dt,
                                                  weapon.turretPitchRateDegrees * dt);
    return std::max(std::abs(std::remainder(yaw - actor.combat.turretYawDegrees, 360.F)),
                    std::abs(pitch - actor.combat.turretPitchDegrees));
}
ShotContact traceWeapon(const World& world,
                        const Terrain& terrain,
                        const DefinitionRegistry& definitions,
                        const PlayerRegistry& players,
                        const Entity& actor,
                        glm::vec3 origin,
                        glm::vec3 direction,
                        float range,
                        std::uint32_t chunks) {
    const auto end = origin + glm::normalize(direction) * range;
    ShotContact contact{end};
    float nearest = 1;
    const int steps = std::max(1, static_cast<int>(std::ceil(range / .2F)));
    for (int i = 1; i <= steps; ++i) {
        const float f = static_cast<float>(i) / steps;
        const auto p = glm::mix(origin, end, f);
        if (!MapArea{chunks}.contains({p.x, p.z}) || terrain.heightAt(p.x, p.z) >= p.y) {
            nearest = f;
            contact = {p, 0, true};
            break;
        }
    }
    for (const auto& entity : world.entities()) {
        if (entity.id == actor.id || !entity.health || entity.health.current <= 0)
            continue;
        if (entity.kind == EntityKind::building) {
            if (!isOperational(entity))
                continue;
        } else if (!validCombatTarget(definitions, players, actor, entity) ||
                   !combatTargetVisible(players, actor, entity, chunks))
            continue;
        const auto* type = definitions.archetype(entity.archetype);
        if (!type)
            continue;
        const float base = terrain.entityBaseHeight(entity.id,
                                                    entity.transform.position.x,
                                                    entity.transform.position.z) +
                           entity.transform.position.y;
        float fraction{};
        if (intersectsBuilding(origin,
                               end,
                               spatialShape(definitions, entity),
                               base,
                               type->bodyHeight,
                               &fraction) &&
            fraction < nearest) {
            nearest = fraction;
            contact = {glm::mix(origin, end, fraction), entity.id, true};
        }
    }
    return contact;
}
bool clearFiringLine(const World& world,
                     const Terrain& terrain,
                     const DefinitionRegistry& definitions,
                     const Entity& actor,
                     const Entity& target,
                     const WeaponDefinition& weapon) {
    // Check from the hull too, so a barrel protruding through a wall cannot bypass cover.
    auto start = actor.transform.position;
    start.y += terrain.entityBaseHeight(actor.id, start.x, start.z) + weapon.muzzleHeight;
    const auto end = combatAimPoint(definitions, terrain, target);
    const int samples = std::max(1, static_cast<int>(std::ceil(glm::length(end - start) / .375F)));
    for (int i = 1; i < samples; ++i) {
        const auto p = glm::mix(start, end, static_cast<float>(i) / samples);
        if (terrain.heightAt(p.x, p.z) + .05F >= p.y)
            return false;
    }
    for (const auto& building : world.entities()) {
        if (building.id == actor.id || building.id == target.id ||
            building.kind != EntityKind::building || !isOperational(building))
            continue;
        const auto* type = definitions.archetype(building.archetype);
        if (type && intersectsBuilding(start,
                                       end,
                                       spatialShape(definitions, building),
                                       terrain.entityBaseHeight(building.id,
                                                                building.transform.position.x,
                                                                building.transform.position.z) +
                                           building.transform.position.y,
                                       type->bodyHeight))
            return false;
    }
    return true;
}
bool weaponHits(std::uint32_t seed, EntityId attacker, std::uint64_t shot, float accuracy) {
    if (accuracy <= 0)
        return false;
    if (accuracy >= 1)
        return true;
    std::uint64_t value = shot ^ (attacker * 0x9E3779B97F4A7C15ULL) ^ seed;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    value ^= value >> 31;
    return static_cast<float>(value >> 40) * (1.F / 16777216.F) < accuracy;
}
float turnToward(Entity& entity, float desired, float rate, float dt) {
    const float error = std::remainder(desired - entity.transform.rotationDegrees.y, 360.F);
    entity.transform.rotationDegrees.y =
        std::remainder(entity.transform.rotationDegrees.y +
                           (rate > 0 ? std::clamp(error, -rate * dt, rate * dt) : error),
                       360.F);
    return std::abs(std::remainder(desired - entity.transform.rotationDegrees.y, 360.F));
}
glm::vec2 turnLimitedMovement(Entity& entity, glm::vec2 desired, float rate, float dt) {
    const float magnitude = glm::length(desired);
    if (magnitude < .001F)
        return desired;
    const float heading = glm::degrees(std::atan2(desired.x, desired.y));
    const float error = turnToward(entity, heading, rate, dt);
    if (rate <= 0)
        return desired;
    // Pivot before driving into a new path segment. Driving through a sharp turn can cut
    // an obstacle corner or orbit a waypoint closer than the vehicle's turning circle.
    if (error > 5.F)
        return {0, 0};
    const float yaw = glm::radians(entity.transform.rotationDegrees.y);
    return glm::vec2{std::sin(yaw), std::cos(yaw)} * magnitude *
           std::max(0.F, std::cos(glm::radians(error)));
}
} // namespace strategy
