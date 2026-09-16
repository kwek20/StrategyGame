#include "world/WorldGeneration.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "simulation/DeterministicRandom.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"
#include "world/StartingPlacement.hpp"
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
    const TerrainSample& sample = terrain.sampleAt(x, z);
    if (sample.traversal == TerrainTraversalClass::impassable)
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
                   std::uint32_t mapChunksPerSide,
                   float x,
                   float z) {
    const glm::vec2 point{x, z};
    for (PlayerId player = 1; player <= 2; ++player) {
        const auto& starts = player == 1 ? rules.playerOne : rules.playerTwo;
        for (const StartingEntityDefinition& start : starts) {
            const EntityArchetype* archetype = definitions.archetype(start.archetype);
            if (archetype && archetype->tags.contains("headquarters"))
                if (const glm::vec3 position = startingEntityPosition(
                        start, player, rules, mapChunksPerSide);
                    glm::distance(point, glm::vec2{position.x, position.z}) <=
                    rules.baseExclusionRadius)
                    return false;
        }
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
            !clearOfStarts(rules, definitions, mapChunksPerSide, centerX, centerZ) ||
            !clearOfStarts(rules, definitions, mapChunksPerSide, -centerX, -centerZ))
            continue;
        for (std::uint32_t member = 0; member < settings.nodesPerCluster; ++member) {
            const float x = centerX + random.range(-settings.spread, settings.spread);
            const float z = centerZ + random.range(-settings.spread, settings.spread);
            if (!suitable(terrain, rules, mapChunksPerSide, x, z) ||
                !clearOfStarts(rules, definitions, mapChunksPerSide, x, z))
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
                             const Terrain& terrain,
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
    const glm::vec3 headquartersPosition = startingEntityPosition(
        *headquarters, 1, rules, mapChunksPerSide);
    std::uint32_t made = 0;
    const float centerDirection = std::atan2(-headquartersPosition.z, -headquartersPosition.x);
    for (std::uint32_t attempt = 0;
         attempt < settings.attemptsPerCluster && made < settings.startingNodesPerPlayer;
         ++attempt) {
        const float distance = random.range(settings.startingMinimumDistance,
                                            settings.startingMaximumDistance);
        const float angle = centerDirection + random.range(-0.85F, 0.85F);
        const float x = headquartersPosition.x + std::cos(angle) * distance;
        const float z = headquartersPosition.z + std::sin(angle) * distance;
        const MapArea map{mapChunksPerSide};
        // Drones ignore entities but not authoritative impassable terrain, so guaranteed
        // opening nodes must still occupy traversable semantic cells.
        if (map.contains({x, z}, rules.terrainEdgeMargin) &&
            map.contains({-x, -z}, rules.terrainEdgeMargin) &&
            terrain.sampleAt(x, z).traversal != TerrainTraversalClass::impassable &&
            terrain.sampleAt(-x, -z).traversal != TerrainTraversalClass::impassable &&
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
        guaranteedStartingNodes(world, terrain, definitions, terrainSeed, type,
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

void populateVegetation(World& world,
                        const Terrain& terrain,
                        const DefinitionRegistry& definitions,
                        std::uint32_t terrainSeed,
                        std::uint32_t mapChunksPerSide) {
    const MapArea map{mapChunksPerSide};
    for (const VegetationGenerationDefinition& settings : definitions.matchRules().vegetation) {
        const EntityArchetype* type = definitions.archetype(settings.archetype);
        if (!type) continue;
        DeterministicRandom random(terrainSeed, settings.stream);
        const std::uint32_t desired = static_cast<std::uint32_t>(std::round(
            settings.instancesPerChunk * static_cast<float>(mapChunksPerSide * mapChunksPerSide)));
        const std::uint32_t attempts = std::max(64U, desired * 30U);
        std::uint32_t made = 0;
        for (std::uint32_t attempt = 0; attempt < attempts && made < desired; ++attempt) {
            const float extent = map.halfExtent() - definitions.matchRules().terrainEdgeMargin;
            const float x = random.range(-extent, extent);
            const float z = random.range(-extent, extent);
            const TerrainSample& terrainSample = terrain.sampleAt(x, z);
            if (std::find(settings.allowedBiomes.begin(),
                          settings.allowedBiomes.end(),
                          terrainSample.biome) == settings.allowedBiomes.end() ||
                std::find(settings.allowedSurfaces.begin(),
                          settings.allowedSurfaces.end(),
                          terrainSample.surface) == settings.allowedSurfaces.end() ||
                terrainSample.slopeDegrees > settings.maximumSlopeDegrees)
                continue;
            if (overlapsObject(world, definitions, {x, z}, std::max(0.2F, type->collisionRadius)))
                continue;
            bool spaced = true;
            for (const Entity& existing : world.entities()) {
                const EntityArchetype* existingType = definitions.archetype(existing.archetype);
                if (!existingType || !existingType->tags.contains("vegetation")) continue;
                const glm::vec2 delta{x - existing.transform.position.x,
                                      z - existing.transform.position.z};
                if (glm::dot(delta, delta) < settings.minimumSpacing * settings.minimumSpacing) {
                    spaced = false;
                    break;
                }
            }
            if (!spaced) continue;
            Entity& entity = world.createEntity(Text::get(type->nameKey), type->id, 0);
            definitions.initializeEntity(entity);
            entity.transform.position = {x, 0.0F, z};
            entity.transform.rotationDegrees.y = random.range(0.0F, 360.0F);
            const float scale = random.range(settings.minimumScale, settings.maximumScale);
            entity.transform.scale = {scale, scale, scale};
            ++made;
        }
    }
}
} // namespace strategy
