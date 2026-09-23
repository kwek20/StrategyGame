#include "world/WorldGeneration.hpp"
#include "world/Vegetation.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "simulation/DeterministicRandom.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"
#include "world/StartingPlacement.hpp"
#include "world/World.hpp"
#include "world/GenerationProgress.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <unordered_map>

namespace strategy {
namespace {
std::uint32_t resourceStreamSeed(std::uint32_t seed, std::string_view stream) {
    std::uint32_t result = seed;
    for (const char value : stream)
        result = (result ^ static_cast<unsigned char>(value)) * 16777619U;
    return result;
}

std::uint32_t coordinateHash(int x, int z, std::uint32_t seed) {
    std::uint32_t value = seed ^ (static_cast<std::uint32_t>(x) * 0x9E3779B9U);
    value ^= static_cast<std::uint32_t>(z) * 0x85EBCA6BU;
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

float regionalPotential(float x, float z, float scale, std::uint32_t seed) {
    const float gridX = x / scale, gridZ = z / scale;
    const int x0 = static_cast<int>(std::floor(gridX));
    const int z0 = static_cast<int>(std::floor(gridZ));
    const float rawX = gridX - static_cast<float>(x0);
    const float rawZ = gridZ - static_cast<float>(z0);
    const float tx = rawX * rawX * (3.0F - 2.0F * rawX);
    const float tz = rawZ * rawZ * (3.0F - 2.0F * rawZ);
    const auto value = [seed](int px, int pz) {
        return static_cast<float>(coordinateHash(px, pz, seed) >> 8U) /
               16777215.0F;
    };
    return std::lerp(std::lerp(value(x0, z0), value(x0 + 1, z0), tx),
                     std::lerp(value(x0, z0 + 1), value(x0 + 1, z0 + 1), tx), tz);
}

class VegetationSpacingGrid final {
  public:
    [[nodiscard]] bool canPlace(std::string_view group,
                                glm::vec2 position,
                                float minimumSpacing) const {
        if (minimumSpacing <= 0.0F) return true;
        const glm::ivec2 center = cell(position);
        const auto foundGroup = groups_.find(std::string{group});
        if (foundGroup == groups_.end()) return true;
        const float searchSpacing = std::max(minimumSpacing, foundGroup->second.maximumSpacing);
        const int radius = static_cast<int>(std::ceil(searchSpacing / cellSize));
        for (int z = center.y - radius; z <= center.y + radius; ++z)
            for (int x = center.x - radius; x <= center.x + radius; ++x) {
                const auto foundCell = foundGroup->second.cells.find(cellKey(x, z));
                if (foundCell == foundGroup->second.cells.end()) continue;
                for (const Entry& existing : foundCell->second) {
                    const glm::vec2 delta = position - existing.position;
                    const float requiredSpacing = std::max(minimumSpacing, existing.spacing);
                    if (glm::dot(delta, delta) < requiredSpacing * requiredSpacing) return false;
                }
            }
        return true;
    }

    void insert(std::string_view group, glm::vec2 position, float spacing) {
        const glm::ivec2 target = cell(position);
        const std::string key{group};
        Group& targetGroup = groups_[key];
        targetGroup.cells[cellKey(target.x, target.y)].push_back({position, spacing});
        targetGroup.maximumSpacing = std::max(targetGroup.maximumSpacing, spacing);
    }

  private:
    struct Entry {
        glm::vec2 position{0.0F};
        float spacing{0.0F};
    };
    struct Group {
        std::unordered_map<std::uint64_t, std::vector<Entry>> cells;
        float maximumSpacing{0.0F};
    };
    static constexpr float cellSize = 1.0F;
    [[nodiscard]] static glm::ivec2 cell(glm::vec2 position) {
        return {static_cast<int>(std::floor(position.x / cellSize)),
                static_cast<int>(std::floor(position.y / cellSize))};
    }
    [[nodiscard]] static std::uint64_t cellKey(int x, int z) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
               static_cast<std::uint32_t>(z);
    }
    std::unordered_map<std::string, Group> groups_;
};

class VegetationObstacleGrid final {
  public:
    VegetationObstacleGrid(const World& world, const DefinitionRegistry& definitions) {
        for (const Entity& entity : world.entities()) {
            if (entity.flight || (entity.resource && entity.resource.remaining <= 0.0F)) continue;
            const EntityArchetype* definition = definitions.archetype(entity.archetype);
            if (definition && definition->collisionRadius <= 0.0F) continue;
            const SpatialShape shape = spatialShape(definitions, entity);
            const std::size_t index = shapes_.size();
            shapes_.push_back(shape);
            const glm::vec2 extents = axisAlignedHalfExtents(shape);
            const glm::ivec2 minimum = cell(shape.center - extents);
            const glm::ivec2 maximum = cell(shape.center + extents);
            for (int z = minimum.y; z <= maximum.y; ++z)
                for (int x = minimum.x; x <= maximum.x; ++x)
                    cells_[cellKey(x, z)].push_back(index);
        }
    }

    [[nodiscard]] bool overlapsCircle(glm::vec2 position, float radius) const {
        const SpatialShape candidate{FootprintShape::circle, position, radius};
        const glm::ivec2 minimum = cell(position - glm::vec2{radius});
        const glm::ivec2 maximum = cell(position + glm::vec2{radius});
        for (int z = minimum.y; z <= maximum.y; ++z)
            for (int x = minimum.x; x <= maximum.x; ++x) {
                const auto found = cells_.find(cellKey(x, z));
                if (found == cells_.end()) continue;
                for (const std::size_t index : found->second)
                    if (overlaps(candidate, shapes_[index])) return true;
            }
        return false;
    }

  private:
    static constexpr float cellSize = 16.0F;
    [[nodiscard]] static glm::ivec2 cell(glm::vec2 position) {
        return {static_cast<int>(std::floor(position.x / cellSize)),
                static_cast<int>(std::floor(position.y / cellSize))};
    }
    [[nodiscard]] static std::uint64_t cellKey(int x, int z) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
               static_cast<std::uint32_t>(z);
    }
    std::vector<SpatialShape> shapes_;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells_;
};

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
    if (std::find(generation.allowedBiomes.begin(), generation.allowedBiomes.end(),
                  sample.biome) == generation.allowedBiomes.end() ||
        sample.moisture < generation.minimumMoisture ||
        sample.moisture > generation.maximumMoisture)
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
         float rotation,
         float capacityMultiplier = 1.0F) {
    if (overlapsObject(world, definitions, {x, z}, type.collisionRadius))
        return false;
    Entity& entity = world.createEntity(Text::get(type.nameKey), type.id, 0);
    definitions.initializeEntity(entity);
    entity.transform.position = {x, 0.0F, z};
    entity.transform.rotationDegrees.y = rotation;
    entity.resource.type = type.resourceType;
    entity.resource.remaining = type.resourceCapacity * capacityMultiplier;
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
    DeterministicRandom random(seed, settings.stream + ".regions");
    const float squareChunks = static_cast<float>(mapChunksPerSide * mapChunksPerSide);
    const float density = random.range(settings.minimumClustersPerSquareChunk,
                                       settings.maximumClustersPerSquareChunk);
    const std::uint32_t desiredClusters = std::max(
        1U, static_cast<std::uint32_t>(std::round(squareChunks * density * abundanceScale)));
    const std::uint32_t potentialSeed = resourceStreamSeed(seed, settings.stream + ".potential");
    const MapArea map{mapChunksPerSide};
    const float extent = map.halfExtent() - rules.terrainEdgeMargin;
    std::vector<glm::vec2> centers;
    const std::uint32_t centerAttempts = desiredClusters * 240U;
    for (std::uint32_t attempt = 0;
         attempt < centerAttempts && centers.size() < desiredClusters; ++attempt) {
        const glm::vec2 center{random.range(-extent, extent), random.range(-extent, extent)};
        if (!suitable(terrain, rules, type, mapChunksPerSide, center.x, center.y) ||
            !clearOfStarts(rules, startingAnchors, center.x, center.y) ||
            regionalPotential(center.x, center.y, settings.regionScale, potentialSeed) <
                settings.regionThreshold)
            continue;
        if (std::any_of(centers.begin(), centers.end(), [&](glm::vec2 existing) {
                return glm::distance(existing, center) < settings.minimumClusterSpacing;
            }))
            continue;
        centers.push_back(center);
    }

    std::vector<glm::vec2> placedNodes;
    for (std::size_t cluster = 0; cluster < centers.size(); ++cluster) {
        const float radius = random.range(settings.minimumClusterRadius,
                                          settings.maximumClusterRadius);
        const std::uint32_t nodeRange = settings.maximumNodesPerCluster -
                                        settings.minimumNodesPerCluster + 1U;
        const std::uint32_t desiredNodes = settings.minimumNodesPerCluster +
                                           random.next() % nodeRange;
        std::uint32_t made = 0;
        for (std::uint32_t attempt = 0;
             attempt < settings.placementAttemptsPerCluster && made < desiredNodes; ++attempt) {
            const float angle = random.range(0.0F, glm::two_pi<float>());
            const float distance = radius * std::sqrt(random.range(0.0F, 1.0F));
            const glm::vec2 candidate = centers[cluster] +
                glm::vec2{std::cos(angle), std::sin(angle)} * distance;
            const float potential = regionalPotential(candidate.x, candidate.y,
                                                       settings.regionScale, potentialSeed);
            const float irregularBoundary = settings.regionThreshold * 0.72F +
                                            (distance / radius) * 0.12F;
            if (potential < irregularBoundary ||
                !suitable(terrain, rules, type, mapChunksPerSide,
                          candidate.x, candidate.y) ||
                !clearOfStarts(rules, startingAnchors, candidate.x, candidate.y) ||
                std::any_of(placedNodes.begin(), placedNodes.end(), [&](glm::vec2 existing) {
                    return glm::distance(existing, candidate) < settings.minimumNodeSpacing;
                }))
                continue;
            const float capacity = random.range(settings.minimumCapacityMultiplier,
                                                settings.maximumCapacityMultiplier);
            if (add(world, definitions, type, candidate.x, candidate.y,
                    random.range(0.0F, 360.0F), capacity)) {
                placedNodes.push_back(candidate);
                ++made;
            }
        }
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
        const std::uint32_t attempts =
            std::max(512U, settings.placementAttemptsPerCluster * 8U);
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
                       const std::vector<glm::vec2>& providedStartingAnchors,
                       WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::resources, 0.0F);
    std::vector<glm::vec2> startingAnchors = providedStartingAnchors;
    if (startingAnchors.empty())
        for (const StartingRegion& region :
             selectStartingRegions(terrain, definitions, mapChunksPerSide, 2))
            startingAnchors.push_back(region.anchor);
    const auto& generated = definitions.matchRules().generatedResourceNodes;
    for (std::size_t index = 0; index < generated.size(); ++index) {
        if (progress)
            progress->report(WorldGenerationPhase::resources,
                             static_cast<float>(index) / std::max<std::size_t>(1, generated.size()));
        const std::string& id = generated[index];
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
    if (progress) progress->report(WorldGenerationPhase::resources, 1.0F);
}

VegetationField generateVegetation(const World& world,
                                   const Terrain& terrain,
                                   const DefinitionRegistry& definitions,
                                   std::uint32_t terrainSeed,
                                   std::uint32_t mapChunksPerSide,
                                   WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::decoration, 0.0F);
    const MapArea map{mapChunksPerSide};
    VegetationField result;
    const float chunkSize = static_cast<float>(Terrain::chunkCellCount) * Terrain::spacing;
    const float origin = -map.halfExtent();
    result.chunks().reserve(mapChunksPerSide * mapChunksPerSide);
    for (std::uint32_t z = 0; z < mapChunksPerSide; ++z)
        for (std::uint32_t x = 0; x < mapChunksPerSide; ++x)
            result.chunks().push_back({{static_cast<int>(x), static_cast<int>(z)},
                                       {origin + (x + 0.5F) * chunkSize,
                                        origin + (z + 0.5F) * chunkSize},
                                       chunkSize * 0.72F,
                                       {}});

    VegetationSpacingGrid spacingGrid;
    const VegetationObstacleGrid obstacleGrid{world, definitions};
    const auto& vegetationRules = definitions.matchRules().vegetation;
    for (std::size_t ruleIndex = 0; ruleIndex < vegetationRules.size(); ++ruleIndex) {
        if (progress)
            progress->report(WorldGenerationPhase::decoration,
                             static_cast<float>(ruleIndex) /
                                 std::max<std::size_t>(1, vegetationRules.size()));
        const VegetationGenerationDefinition& settings = vegetationRules[ruleIndex];
        const EntityArchetype* type = definitions.archetype(settings.archetype);
        if (!type) continue;
        const std::uint32_t densitySeed =
            resourceStreamSeed(terrainSeed, settings.stream + ".density");
        for (VegetationChunk& chunk : result.chunks()) {
            const std::string stream = settings.stream + "." +
                std::to_string(chunk.coordinate.x) + "." + std::to_string(chunk.coordinate.y);
            DeterministicRandom random(terrainSeed, stream);
            const float integral = std::floor(settings.clustersPerChunk);
            std::uint32_t clusterOpportunities = static_cast<std::uint32_t>(integral);
            if (random.range(0.0F, 1.0F) < settings.clustersPerChunk - integral)
                ++clusterOpportunities;
            for (std::uint32_t cluster = 0; cluster < clusterOpportunities; ++cluster) {
                const glm::vec2 center{
                    chunk.center.x + random.range(-chunkSize * 0.5F, chunkSize * 0.5F),
                    chunk.center.y + random.range(-chunkSize * 0.5F, chunkSize * 0.5F)};
                const float potential = regionalPotential(
                    center.x, center.y, settings.densityScale, densitySeed);
                const float density = std::clamp(
                    (potential - (1.0F - settings.coverage)) / settings.coverage, 0.0F, 1.0F);
                if (random.range(0.0F, 1.0F) > density) continue;

                const std::uint32_t countRange = settings.maximumInstancesPerCluster -
                                                 settings.minimumInstancesPerCluster + 1U;
                const std::uint32_t desired = settings.minimumInstancesPerCluster +
                                              random.next() % countRange;
                const float radius = random.range(settings.minimumClusterRadius,
                                                  settings.maximumClusterRadius);
                const std::uint32_t attempts = std::max(
                    settings.requiredTerrainTags != 0 ? 160U : 24U,
                    desired * (settings.requiredTerrainTags != 0 ? 160U : 24U));
                std::uint32_t made = 0;
                for (std::uint32_t attempt = 0; attempt < attempts && made < desired; ++attempt) {
                    const float angle = random.range(0.0F, glm::two_pi<float>());
                    const float distance = radius * std::sqrt(random.range(0.0F, 1.0F));
                    const float x = center.x + std::cos(angle) * distance;
                    const float z = center.y + std::sin(angle) * distance;
                    if (!map.contains({x, z}, definitions.matchRules().terrainEdgeMargin)) continue;
                    const TerrainSample& terrainSample = terrain.sampleAt(x, z);
                    const float normalizedHeight = terrainSample.baseHeight / Terrain::heightScale;
                    if (terrainSample.submerged ||
                        (terrainSample.tags & settings.requiredTerrainTags) !=
                            settings.requiredTerrainTags ||
                        (terrainSample.tags & settings.forbiddenTerrainTags) != 0 ||
                        std::find(settings.allowedBiomes.begin(),
                                  settings.allowedBiomes.end(),
                                  terrainSample.biome) == settings.allowedBiomes.end() ||
                        std::find(settings.allowedSurfaces.begin(),
                                  settings.allowedSurfaces.end(),
                                  terrainSample.surface) == settings.allowedSurfaces.end() ||
                        terrainSample.moisture < settings.minimumMoisture ||
                        terrainSample.moisture > settings.maximumMoisture ||
                        normalizedHeight < settings.minimumHeight ||
                        normalizedHeight > settings.maximumHeight ||
                        terrainSample.slopeDegrees > settings.maximumSlopeDegrees)
                        continue;
                    if (obstacleGrid.overlapsCircle(
                            {x, z}, std::max(0.2F, type->collisionRadius)))
                        continue;
                    if (!spacingGrid.canPlace(
                            settings.spacingGroup, {x, z}, settings.minimumSpacing))
                        continue;
                    VegetationInstance instance;
                    instance.archetype = EntityArchetypeId{type->id};
                    instance.presentation = PresentationId{type->presentation};
                    instance.transform.position = {x, 0.0F, z};
                    instance.transform.rotationDegrees.y = random.range(0.0F, 360.0F);
                    const float scale = random.range(settings.minimumScale, settings.maximumScale);
                    instance.transform.scale = {scale, scale, scale};
                    chunk.instances.push_back(std::move(instance));
                    chunk.radius = std::max(
                        chunk.radius,
                        glm::distance(chunk.center, glm::vec2{x, z}));
                    spacingGrid.insert(settings.spacingGroup, {x, z}, settings.minimumSpacing);
                    ++made;
                }
            }
        }
    }
    if (progress) progress->report(WorldGenerationPhase::decoration, 1.0F);
    return result;
}
} // namespace strategy
