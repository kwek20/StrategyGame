#pragma once

#include "world/Entity.hpp"

namespace strategy {
class DefinitionRegistry;
class PlayerRegistry;
class World;
class Terrain;
struct WeaponDefinition;

[[nodiscard]] const WeaponDefinition* primaryWeapon(const DefinitionRegistry&, const Entity&);
[[nodiscard]] bool validCombatTarget(const DefinitionRegistry&,
                                     const PlayerRegistry&,
                                     const Entity& actor,
                                     const Entity& target);
[[nodiscard]] bool combatTargetVisible(const PlayerRegistry&,
                                       const Entity& actor,
                                       const Entity& target,
                                       std::uint32_t chunks);
[[nodiscard]] glm::vec3 combatAimPoint(const DefinitionRegistry&, const Terrain&, const Entity&);
[[nodiscard]] glm::vec3 combatMuzzle(const Terrain&, const Entity&, const WeaponDefinition&);
[[nodiscard]] glm::vec3 weaponDirection(const Entity&, const WeaponDefinition&);
float aimWeapon(Entity&, const Terrain&, const WeaponDefinition&, glm::vec3 point, float dt);
struct ShotContact {
    glm::vec3 position{};
    EntityId entity{};
    bool impact{};
};
[[nodiscard]] ShotContact traceWeapon(const World&,
                                      const Terrain&,
                                      const DefinitionRegistry&,
                                      const PlayerRegistry&,
                                      const Entity&,
                                      glm::vec3 origin,
                                      glm::vec3 direction,
                                      float range,
                                      std::uint32_t chunks);
[[nodiscard]] bool clearFiringLine(const World&,
                                   const Terrain&,
                                   const DefinitionRegistry&,
                                   const Entity& actor,
                                   const Entity& target,
                                   const WeaponDefinition&);
[[nodiscard]] bool
weaponHits(std::uint32_t seed, EntityId attacker, std::uint64_t shot, float accuracy);
// Returns remaining angular error. Zero rate preserves instantaneous turning for pedestrians.
float turnToward(Entity&, float desiredDegrees, float rateDegrees, float deltaSeconds);
[[nodiscard]] glm::vec2
turnLimitedMovement(Entity&, glm::vec2 desired, float rateDegrees, float deltaSeconds);
} // namespace strategy
