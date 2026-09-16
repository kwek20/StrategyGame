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
              const ResourceNodeDefinition& type,
              std::uint32_t mapChunksPerSide,
              float x, float z) {
    if (!MapArea{mapChunksPerSide}.contains({x, z}, rules.terrainEdgeMargin))
        return false;
    const TerrainSample& sample = terrain.sampleAt(x, z);
    const auto& generation = *type.generation;
    if ((sample.tags & generation.requiredTerrainTags) != generation.requiredTerrainTags ||
        (sample.tags & generation.forbiddenTerrainTags) != 0)
        return false;
    const float normalized = terrain.heightAt(x, z) / Terrain::heightScale;
    const float minimumHeight = generation.minimumHeight >= 0.0F
                                    ? generation.minimumHeight : rules.minimumResourceHeight;
    const float maximumHeight = generation.maximumHeight >= 0.0F
                                    ? generation.maximumHeight : rules.maximumResourceHeight;
    if (normalized < minimumHeight || normalized > maximumHeight)
        return false;
    if (generation.maximumSlope >= 0.0F)
        return sample.slopeDegrees <= generation.maximumSlope;
    const float step = Terrain::spacing * 2.0F;
    const float dx = terrain.heightAt(x + step, z) - terrain.heightAt(x - step, z);
    const float dz = terrain.heightAt(x, z + step) - terrain.heightAt(x, z - step);
    return std::sqrt(dx * dx + dz * dz) < rules.maximumResourceSlope;
}

bool clearOfStarts(const MatchRulesDefinition& rules,
                   const std::vector<glm::vec2>& startingAnchors,
                   float x,
                   float z) {
    const glm::vec2 point{x, z};
    for (const glm::vec2 anchor : startingAnchors)
        if (glm::distance(point, anchor) <= rules.baseExclusionRadius)
            return false;
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
              float abundanceScale,
              const std::vector<glm::vec2>& startingAnchors) {
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
        if (centerX < 0.0F || !suitable(terrain, rules, type, mapChunksPerSide, centerX, centerZ) ||
            !suitable(terrain, rules, type, mapChunksPerSide, -centerX, -centerZ) ||
            !clearOfStarts(rules, startingAnchors, centerX, centerZ) ||
            !clearOfStarts(rules, startingAnchors, -centerX, -centerZ))
            continue;
        for (std::uint32_t member = 0; member < settings.nodesPerCluster; ++member) {
            const float x = centerX + random.range(-settings.spread, settings.spread);
            const float z = centerZ + random.range(-settings.spread, settings.spread);
            if (!suitable(terrain, rules, type, mapChunksPerSide, x, z) ||
                !clearOfStarts(rules, startingAnchors, x, z))
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
                             std::uint32_t mapChunksPerSide,
                             const std::vector<glm::vec2>& startingAnchors) {
    const auto& settings = *type.generation;
    if (settings.startingNodesPerPlayer == 0 || startingAnchors.empty()) return;
    const auto& rules = definitions.matchRules();
    const auto findNear = [&](glm::vec2 origin, DeterministicRandom& candidateRandom)
        -> std::optional<glm::vec2> {
        constexpr float goldenAngle = 2.39996323F;
        const std::uint32_t attempts = std::max(512U, settings.attemptsPerCluster * 8U);
        const float phase = candidateRandom.range(-3.14159265F, 3.14159265F);
        const float center = std::atan2(-origin.y, -origin.x);
        for (std::uint32_t attempt = 0; attempt < attempts; ++attempt) {
            const float progress = attempts > 1
                                       ? static_cast<float>(attempt) /
                                             static_cast<float>(attempts - 1)
                                       : 0.0F;
            const float distance = settings.startingMinimumDistance +
                (settings.startingMaximumDistance * 1.75F -
                 settings.startingMinimumDistance) * std::sqrt(progress);
            const float angle = center + phase + goldenAngle * static_cast<float>(attempt);
            const glm::vec2 candidate{origin.x + std::cos(angle) * distance,
                                      origin.y + std::sin(angle) * distance};
            if (suitable(terrain, rules, type, mapChunksPerSide, candidate.x, candidate.y) &&
                !overlapsObject(world, definitions, candidate, type.collisionRadius))
                return candidate;
        }
        return std::nullopt;
    };
    for (std::size_t player = 0; player < startingAnchors.size(); ++player) {
        DeterministicRandom random(
            seed, settings.stream + ".starting.player." + std::to_string(player + 1));
        std::uint32_t made = 0;
        while (made < settings.startingNodesPerPlayer) {
            const auto candidate = findNear(startingAnchors[player], random);
            if (!candidate) break;
            if (add(world, definitions, type, candidate->x, candidate->y,
                    random.range(0.0F, 360.0F)))
                ++made;
        }
        if (made != settings.startingNodesPerPlayer)
            throw std::runtime_error("Unable to place guaranteed starting resource nodes for " +
                                     type.id + " near player " + std::to_string(player + 1));
    }
}
} // namespace

void populateResources(World& world,
                       const Terrain& terrain,
                       const DefinitionRegistry& definitions,
                       std::uint32_t terrainSeed,
                       std::uint32_t mapChunksPerSide,
                       float abundanceScale,
                       const std::vector<glm::vec2>& providedStartingAnchors) {
    std::vector<glm::vec2> startingAnchors = providedStartingAnchors;
    if (startingAnchors.empty())
        for (const StartingRegion& region :
             selectStartingRegions(terrain, definitions, mapChunksPerSide, 2))
            startingAnchors.push_back(region.anchor);
    for (const std::string& id : definitions.matchRules().generatedResourceNodes)
    {
        const ResourceNodeDefinition& type = *definitions.resource(ResourceArchetypeId{id});
        guaranteedStartingNodes(world, terrain, definitions, terrainSeed, type,
                                std::clamp(mapChunksPerSide, 10U,
                                           static_cast<std::uint32_t>(Terrain::chunksPerSide)),
                                startingAnchors);
        clusters(world,
                 terrain,
                 definitions,
                 terrainSeed,
                 type,
                 std::clamp(mapChunksPerSide, 10U,
                            static_cast<std::uint32_t>(Terrain::chunksPerSide)),
                 std::clamp(abundanceScale, 0.5F, 2.0F), startingAnchors);
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
            if (terrainSample.submerged ||
                std::find(settings.allowedBiomes.begin(),
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
