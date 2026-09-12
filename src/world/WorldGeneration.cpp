#include "world/WorldGeneration.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "simulation/DeterministicRandom.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace strategy {
namespace {
bool suitable(const Terrain& terrain, const MatchRulesDefinition& rules,
              std::uint32_t mapChunksPerSide,
              float x, float z) {
    if (!MapArea{mapChunksPerSide}.contains({x, z}, rules.terrainEdgeMargin))
        return false;
    const float normalized = terrain.heightAt(x, z) / Terrain::heightScale;
    if (normalized < rules.minimumResourceHeight || normalized > rules.maximumResourceHeight)
        return false;
    const float step = Terrain::spacing * 2.0F;
    const float dx = terrain.heightAt(x + step, z) - terrain.heightAt(x - step, z);
    const float dz = terrain.heightAt(x, z + step) - terrain.heightAt(x, z - step);
    return std::sqrt(dx * dx + dz * dz) < rules.maximumResourceSlope;
}

bool clearOfStarts(const MatchRulesDefinition& rules,
                   const DefinitionRegistry& definitions,
                   float x,
                   float z) {
    const glm::vec2 point{x, z};
    for (const auto* starts : {&rules.playerOne, &rules.playerTwo})
        for (const StartingEntityDefinition& start : *starts) {
            const EntityArchetype* archetype = definitions.archetype(start.archetype);
            if (archetype && archetype->tags.contains("headquarters"))
            if (glm::distance(point, glm::vec2{start.position.x, start.position.z}) <=
                rules.baseExclusionRadius)
                return false;
        }
    return true;
}

bool add(World& world,
         const DefinitionRegistry& definitions,
         const ResourceNodeDefinition& type,
         float x,
         float z,
         float rotation) {
    if (overlapsObject(world, definitions, {x, z}, type.collisionRadius))
        return false;
    Entity& entity = world.createEntity(Text::get(type.nameKey), type.id, 0);
    definitions.initializeEntity(entity);
    entity.transform.position = {x, 0.0F, z};
    entity.transform.rotationDegrees.y = rotation;
    entity.resource.type = type.resourceType;
    entity.resource.remaining = type.resourceCapacity;
    return true;
}

void clusters(World& world,
              const Terrain& terrain,
              const DefinitionRegistry& definitions,
              std::uint32_t seed,
              const ResourceNodeDefinition& type,
              std::uint32_t mapChunksPerSide,
              float abundanceScale) {
    const auto& rules = definitions.matchRules();
    const auto& settings = *type.generation;
    DeterministicRandom random(seed, settings.stream);
    std::uint32_t made = 0;
    const std::uint32_t desiredPairs = std::max(1U, static_cast<std::uint32_t>(
        std::round(static_cast<float>(settings.clusterPairs) * abundanceScale)));
    const std::uint32_t maximumAttempts = desiredPairs * settings.attemptsPerCluster;
    for (std::uint32_t attempt = 0; attempt < maximumAttempts && made < desiredPairs;
         ++attempt) {
        const float centerX = random.range(-settings.centerExtent, settings.centerExtent);
        const float centerZ = random.range(-settings.centerExtent, settings.centerExtent);
        if (centerX < 0.0F || !suitable(terrain, rules, mapChunksPerSide, centerX, centerZ) ||
            !suitable(terrain, rules, mapChunksPerSide, -centerX, -centerZ) ||
            !clearOfStarts(rules, definitions, centerX, centerZ) ||
            !clearOfStarts(rules, definitions, -centerX, -centerZ))
            continue;
        for (std::uint32_t member = 0; member < settings.nodesPerCluster; ++member) {
            const float x = centerX + random.range(-settings.spread, settings.spread);
            const float z = centerZ + random.range(-settings.spread, settings.spread);
            if (!suitable(terrain, rules, mapChunksPerSide, x, z) ||
                !clearOfStarts(rules, definitions, x, z))
                continue;
            const float angle = random.range(0.0F, 360.0F);
            if (!overlapsObject(world, definitions, {x, z}, type.collisionRadius) &&
                !overlapsObject(world, definitions, {-x, -z}, type.collisionRadius)) {
                add(world, definitions, type, x, z, angle);
                add(world, definitions, type, -x, -z, angle + 180.0F);
            }
        }
        ++made;
    }
}

void guaranteedStartingNodes(World& world,
                             const DefinitionRegistry& definitions,
                             std::uint32_t seed,
                             const ResourceNodeDefinition& type,
                             std::uint32_t mapChunksPerSide) {
    const auto& settings = *type.generation;
    if (settings.startingNodesPerPlayer == 0) return;
    const auto& rules = definitions.matchRules();
    DeterministicRandom random(seed, settings.stream + ".starting");
    const StartingEntityDefinition* headquarters = nullptr;
    for (const auto& start : rules.playerOne) {
        const EntityArchetype* archetype = definitions.archetype(EntityArchetypeId{start.archetype});
        if (archetype && archetype->tags.contains("headquarters")) { headquarters = &start; break; }
    }
    if (!headquarters) return;
    std::uint32_t made = 0;
    const float centerDirection = std::atan2(-headquarters->position.z, -headquarters->position.x);
    for (std::uint32_t attempt = 0;
         attempt < settings.attemptsPerCluster && made < settings.startingNodesPerPlayer;
         ++attempt) {
        const float distance = random.range(settings.startingMinimumDistance,
                                            settings.startingMaximumDistance);
        const float angle = centerDirection + random.range(-0.85F, 0.85F);
        const float x = headquarters->position.x + std::cos(angle) * distance;
        const float z = headquarters->position.z + std::sin(angle) * distance;
        const MapArea map{mapChunksPerSide};
        // Starting gatherers are flying drones. Preserve a guaranteed, mirrored opening
        // supply even when a seed has unusually steep terrain around a headquarters.
        if (map.contains({x, z}, rules.terrainEdgeMargin) &&
            map.contains({-x, -z}, rules.terrainEdgeMargin) &&
            !overlapsObject(world, definitions, {x, z}, type.collisionRadius) &&
            !overlapsObject(world, definitions, {-x, -z}, type.collisionRadius)) {
            const float rotation = random.range(0.0F, 360.0F);
            add(world, definitions, type, x, z, rotation);
            add(world, definitions, type, -x, -z, rotation + 180.0F);
            ++made;
        }
    }
    if (made != settings.startingNodesPerPlayer)
        throw std::runtime_error("Unable to place guaranteed mirrored starting resource nodes for " +
                                 type.id);
}
} // namespace

void populateResources(World& world,
                       const Terrain& terrain,
                       const DefinitionRegistry& definitions,
                       std::uint32_t terrainSeed,
                       std::uint32_t mapChunksPerSide,
                       float abundanceScale) {
    for (const std::string& id : definitions.matchRules().generatedResourceNodes)
    {
        const ResourceNodeDefinition& type = *definitions.resource(ResourceArchetypeId{id});
        guaranteedStartingNodes(world, definitions, terrainSeed, type,
                                std::clamp(mapChunksPerSide, 10U,
                                           static_cast<std::uint32_t>(Terrain::chunksPerSide)));
        clusters(world,
                 terrain,
                 definitions,
                 terrainSeed,
                 type,
                 std::clamp(mapChunksPerSide, 10U,
                            static_cast<std::uint32_t>(Terrain::chunksPerSide)),
                 std::clamp(abundanceScale, 0.5F, 2.0F));
    }
}
} // namespace strategy
