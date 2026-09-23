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
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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

struct FairnessResult {
    bool fair{true};
    std::vector<std::size_t> deficientPlayers;
    std::size_t failedBand{0};
    std::vector<std::vector<float>> capacityByPlayerAndBand;
};

std::vector<float> terrainTravelCosts(const Terrain& terrain,
                                      std::uint32_t mapChunksPerSide,
                                      glm::vec2 origin,
                                      MovementDomainMask domainMask) {
    const MapArea map{mapChunksPerSide};
    const int side = static_cast<int>(mapChunksPerSide * Terrain::chunkCellCount *
                                      Terrain::spacing / Terrain::semanticCellSize);
    const auto index = [side](int x, int z) { return z * side + x; };
    const glm::ivec2 source = map.gridCell(origin, side);
    std::vector<float> costs(static_cast<std::size_t>(side * side),
                             std::numeric_limits<float>::infinity());
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> frontier;
    costs[index(source.x, source.y)] = 0.0F;
    frontier.emplace(0.0F, index(source.x, source.y));
    constexpr std::array<glm::ivec2, 8> directions{{
        {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
    while (!frontier.empty()) {
        const auto [cost, current] = frontier.top();
        frontier.pop();
        if (cost != costs[current]) continue;
        const int x = current % side, z = current / side;
        for (const glm::ivec2 direction : directions) {
            const int nx = x + direction.x, nz = z + direction.y;
            if (nx < 0 || nz < 0 || nx >= side || nz >= side) continue;
            const glm::vec2 position = map.gridCellCenter({nx, nz}, side);
            const float terrainCost = terrain.movementCostAt(
                position.x, position.y, domainMask);
            if (terrainCost <= 0.0F || !std::isfinite(terrainCost)) continue;
            const float distance = direction.x != 0 && direction.y != 0
                                       ? Terrain::semanticCellSize * 1.41421356F
                                       : Terrain::semanticCellSize;
            const float candidate = cost + distance * terrainCost;
            const int next = index(nx, nz);
            if (candidate >= costs[next]) continue;
            costs[next] = candidate;
            frontier.emplace(candidate, next);
        }
    }
    return costs;
}

FairnessResult evaluateFairness(const World& world,
                                const DefinitionRegistry& definitions,
                                const ResourceFieldDefinition& field,
                                std::uint32_t mapChunksPerSide,
                                const std::vector<glm::vec2>& startingAnchors,
                                const std::vector<std::vector<float>>& travelCosts) {
    FairnessResult result;
    if (!field.generation.fairness || startingAnchors.size() < 2)
        return result;
    const auto& policy = *field.generation.fairness;
    const MapArea map{mapChunksPerSide};
    const int side = static_cast<int>(mapChunksPerSide * Terrain::chunkCellCount *
                                      Terrain::spacing / Terrain::semanticCellSize);
    result.capacityByPlayerAndBand.assign(
        startingAnchors.size(), std::vector<float>(policy.travelCostBands.size(), 0.0F));
    for (const Entity& entity : world.entities()) {
        const bool belongsToField = std::any_of(
            field.variants.begin(), field.variants.end(), [&](const ResourceNodeVariant& variant) {
                return variant.node.value == entity.archetype.value;
            });
        if (!belongsToField || !entity.resource || entity.resource.remaining <= 0.0F)
            continue;
        const ResourceNodeDefinition* nodeType = definitions.resource(
            ResourceArchetypeId{entity.archetype.value});
        if (!nodeType) continue;
        const glm::ivec2 cell = map.gridCell(
            {entity.transform.position.x, entity.transform.position.z}, side);
        const int edgeRadius = std::max(
            1, static_cast<int>(std::ceil((nodeType->collisionRadius + 1.5F) /
                                         Terrain::semanticCellSize)));
        for (std::size_t player = 0; player < travelCosts.size(); ++player) {
            float edgeCost = std::numeric_limits<float>::infinity();
            for (int z = std::max(0, cell.y - edgeRadius);
                 z <= std::min(side - 1, cell.y + edgeRadius); ++z)
                for (int x = std::max(0, cell.x - edgeRadius);
                     x <= std::min(side - 1, cell.x + edgeRadius); ++x)
                    edgeCost = std::min(edgeCost, travelCosts[player][z * side + x]);
            for (std::size_t band = 0; band < policy.travelCostBands.size(); ++band)
                if (edgeCost <= policy.travelCostBands[band])
                    result.capacityByPlayerAndBand[player][band] += entity.resource.remaining;
        }
    }
    for (std::size_t band = 0; band < policy.travelCostBands.size(); ++band) {
        float maximum = 0.0F;
        for (const auto& player : result.capacityByPlayerAndBand)
            maximum = std::max(maximum, player[band]);
        const bool finalBand = band + 1 == policy.travelCostBands.size();
        for (std::size_t player = 0; player < result.capacityByPlayerAndBand.size(); ++player) {
            const float capacity = result.capacityByPlayerAndBand[player][band];
            const bool ratioFailed = maximum >= policy.minimumComparedCapacity &&
                                     capacity < maximum * policy.minimumCapacityRatio;
            const bool minimumFailed = finalBand && capacity < policy.minimumReachableCapacity;
            if (ratioFailed || minimumFailed) {
                result.fair = false;
                result.failedBand = band;
                result.deficientPlayers.push_back(player);
            }
        }
        if (!result.fair) break;
    }
    return result;
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

class ResourceNodeGrid final {
  public:
    ResourceNodeGrid(const World& world, const DefinitionRegistry& definitions) {
        for (const Entity& entity : world.entities()) {
            if (!entity.resource || entity.resource.remaining <= 0.0F) continue;
            const ResourceNodeDefinition* type = definitions.resource(
                ResourceArchetypeId{entity.archetype.value});
            if (type)
                insert({entity.transform.position.x, entity.transform.position.z},
                       type->collisionRadius);
        }
    }

    [[nodiscard]] bool canPlace(glm::vec2 position, float radius, float minimumSpacing) const {
        const float requested = std::max(radius, minimumSpacing * 0.5F);
        const glm::ivec2 center = cell(position);
        const int search = static_cast<int>(std::ceil(
            (requested + maximumRadius_) / cellSize));
        for (int z = center.y - search; z <= center.y + search; ++z)
            for (int x = center.x - search; x <= center.x + search; ++x) {
                const auto found = cells_.find(cellKey(x, z));
                if (found == cells_.end()) continue;
                for (const Entry& entry : found->second) {
                    const float separation = std::max(radius + entry.radius, minimumSpacing);
                    const glm::vec2 delta = position - entry.position;
                    if (glm::dot(delta, delta) < separation * separation) return false;
                }
            }
        return true;
    }

    void insert(glm::vec2 position, float radius) {
        cells_[cellKey(cell(position).x, cell(position).y)].push_back({position, radius});
        maximumRadius_ = std::max(maximumRadius_, radius);
    }

  private:
    struct Entry { glm::vec2 position; float radius; };
    static constexpr float cellSize = 8.0F;
    [[nodiscard]] static glm::ivec2 cell(glm::vec2 position) {
        return {static_cast<int>(std::floor(position.x / cellSize)),
                static_cast<int>(std::floor(position.y / cellSize))};
    }
    [[nodiscard]] static std::uint64_t cellKey(int x, int z) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
               static_cast<std::uint32_t>(z);
    }
    std::unordered_map<std::uint64_t, std::vector<Entry>> cells_;
    float maximumRadius_{0.0F};
};

bool suitable(const Terrain& terrain, const MatchRulesDefinition& rules,
              const ResourceFieldGenerationDefinition& generation,
              std::uint32_t mapChunksPerSide,
              float x, float z) {
    if (!MapArea{mapChunksPerSide}.contains({x, z}, rules.terrainEdgeMargin))
        return false;
    const TerrainSample& sample = terrain.sampleAt(x, z);
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

const ResourceNodeDefinition& selectVariant(const DefinitionRegistry& definitions,
                                            const ResourceFieldDefinition& field,
                                            DeterministicRandom& random) {
    if (field.variants.size() == 1)
        return *definitions.resource(field.variants.front().node);
    float totalWeight = 0.0F;
    for (const ResourceNodeVariant& variant : field.variants) totalWeight += variant.weight;
    float selected = random.range(0.0F, totalWeight);
    for (const ResourceNodeVariant& variant : field.variants) {
        selected -= variant.weight;
        if (selected <= 0.0F) return *definitions.resource(variant.node);
    }
    return *definitions.resource(field.variants.back().node);
}

bool appendNode(ResourceLayout& layout,
                ResourceNodeGrid& nodeGrid,
                const World& world,
                const DefinitionRegistry& definitions,
                const ResourceFieldDefinition& field,
                const ResourceNodeDefinition& type,
                glm::vec2 position,
                float rotation,
                float capacityMultiplier) {
    const auto& settings = field.generation;
    if (!nodeGrid.canPlace(position, type.collisionRadius, settings.minimumNodeSpacing) ||
        overlapsObject(world, definitions, position, type.collisionRadius))
        return false;
    layout.nodes.push_back({ResourceFieldId{field.id}, ResourceArchetypeId{type.id},
                            position, rotation, capacityMultiplier});
    nodeGrid.insert(position, type.collisionRadius);
    return true;
}

void instantiateLayout(World& world,
                       const DefinitionRegistry& definitions,
                       const ResourceLayout& layout) {
    for (const GeneratedResourceNode& node : layout.nodes) {
        const ResourceNodeDefinition* type = definitions.resource(node.archetype);
        if (!type || !add(world, definitions, *type, node.position.x, node.position.y,
                          node.rotationDegrees, node.capacityMultiplier))
            throw std::runtime_error("Generated resource layout became invalid before instantiation");
    }
}

void fields(ResourceLayout& layout,
              ResourceNodeGrid& nodeGrid,
              const World& world,
              const Terrain& terrain,
              const DefinitionRegistry& definitions,
              std::uint32_t seed,
              const ResourceFieldDefinition& field,
              std::uint32_t mapChunksPerSide,
              float abundanceScale,
              const std::vector<glm::vec2>& startingAnchors) {
    const auto& rules = definitions.matchRules();
    const auto& settings = field.generation;
    DeterministicRandom random(seed, settings.stream + ".regions");
    const float squareChunks = static_cast<float>(mapChunksPerSide * mapChunksPerSide);
    const float density = random.range(settings.minimumFieldsPerSquareChunk,
                                       settings.maximumFieldsPerSquareChunk);
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
        if (!suitable(terrain, rules, settings, mapChunksPerSide, center.x, center.y) ||
            !clearOfStarts(rules, startingAnchors, center.x, center.y) ||
            regionalPotential(center.x, center.y, settings.regionScale, potentialSeed) <
                settings.regionThreshold)
            continue;
        centers.push_back(center);
    }

    for (std::size_t cluster = 0; cluster < centers.size(); ++cluster) {
        const float radius = random.range(settings.minimumFieldRadius,
                                          settings.maximumFieldRadius);
        const std::uint32_t nodeRange = settings.maximumNodesPerField -
                                        settings.minimumNodesPerField + 1U;
        const std::uint32_t desiredNodes = settings.minimumNodesPerField +
                                           random.next() % nodeRange;
        layout.fields.push_back({ResourceFieldId{field.id}, centers[cluster], radius,
                                 desiredNodes});
        std::uint32_t made = 0;
        for (std::uint32_t attempt = 0;
             attempt < settings.placementAttemptsPerField && made < desiredNodes; ++attempt) {
            const float angle = random.range(0.0F, glm::two_pi<float>());
            const float distance = radius * std::sqrt(random.range(0.0F, 1.0F));
            const glm::vec2 candidate = centers[cluster] +
                glm::vec2{std::cos(angle), std::sin(angle)} * distance;
            const float potential = regionalPotential(candidate.x, candidate.y,
                                                       settings.regionScale, potentialSeed);
            const float irregularBoundary = settings.regionThreshold * 0.72F +
                                            (distance / radius) * 0.12F;
            if (potential < irregularBoundary ||
                !suitable(terrain, rules, settings, mapChunksPerSide,
                          candidate.x, candidate.y) ||
                !clearOfStarts(rules, startingAnchors, candidate.x, candidate.y))
                continue;
            const float capacity = random.range(settings.minimumCapacityMultiplier,
                                                settings.maximumCapacityMultiplier);
            const ResourceNodeDefinition& variant = selectVariant(definitions, field, random);
            // Every node is validated at its own sample. The field footprint itself is allowed to
            // cross biome boundaries and overlap any other field footprint.
            if (!suitable(terrain, rules, settings, mapChunksPerSide,
                          candidate.x, candidate.y))
                continue;
            if (appendNode(layout, nodeGrid, world, definitions, field, variant, candidate,
                           random.range(0.0F, 360.0F), capacity)) {
                ++made;
            }
        }
    }
}

void guaranteedStartingNodes(ResourceLayout& layout,
                             ResourceNodeGrid& nodeGrid,
                             const World& world,
                             const Terrain& terrain,
                             const DefinitionRegistry& definitions,
                             std::uint32_t seed,
                             const ResourceFieldDefinition& field,
                             const ResourceNodeDefinition& type,
                             std::uint32_t mapChunksPerSide,
                             const std::vector<glm::vec2>& startingAnchors) {
    const auto& settings = field.generation;
    if (settings.startingNodesPerPlayer == 0 || startingAnchors.empty()) return;
    const auto& rules = definitions.matchRules();
    const auto findNear = [&](glm::vec2 origin, DeterministicRandom& candidateRandom)
        -> std::optional<glm::vec2> {
        constexpr float goldenAngle = 2.39996323F;
        const std::uint32_t attempts =
            std::max(512U, settings.placementAttemptsPerField * 8U);
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
            if (suitable(terrain, rules, settings, mapChunksPerSide,
                         candidate.x, candidate.y) &&
                !overlapsObject(world, definitions, candidate, type.collisionRadius))
                return candidate;
        }
        return std::nullopt;
    };
    for (std::size_t player = 0; player < startingAnchors.size(); ++player) {
        DeterministicRandom random(
            seed, settings.stream + ".starting.player." + std::to_string(player + 1));
        bool completed = false;
        for (std::uint32_t fieldAttempt = 0; fieldAttempt < 32 && !completed; ++fieldAttempt) {
            const auto center = findNear(startingAnchors[player], random);
            if (!center) break;
            const float radius = random.range(settings.minimumFieldRadius,
                                              settings.maximumFieldRadius);
            ResourceNodeGrid candidateGrid = nodeGrid;
            ResourceLayout candidateLayout;
            candidateLayout.fields.push_back({ResourceFieldId{field.id}, *center, radius,
                                               settings.startingNodesPerPlayer});
            for (std::uint32_t node = 0; node < settings.startingNodesPerPlayer; ++node) {
                bool placed = false;
                for (std::uint32_t attempt = 0; attempt < 96 && !placed; ++attempt) {
                    const float angle = random.range(0.0F, glm::two_pi<float>());
                    const float distance = radius * std::sqrt(random.range(0.0F, 1.0F));
                    const glm::vec2 position = *center +
                        glm::vec2{std::cos(angle), std::sin(angle)} * distance;
                    const ResourceNodeDefinition& variant = selectVariant(definitions, field, random);
                    if (!suitable(terrain, rules, settings, mapChunksPerSide,
                                  position.x, position.y))
                        continue;
                    placed = appendNode(candidateLayout, candidateGrid, world, definitions,
                                        field, variant, position,
                                        random.range(0.0F, 360.0F), 1.0F);
                }
                if (!placed) break;
            }
            if (candidateLayout.nodes.size() == settings.startingNodesPerPlayer) {
                nodeGrid = std::move(candidateGrid);
                layout.fields.insert(layout.fields.end(), candidateLayout.fields.begin(),
                                     candidateLayout.fields.end());
                layout.nodes.insert(layout.nodes.end(), candidateLayout.nodes.begin(),
                                    candidateLayout.nodes.end());
                completed = true;
            }
        }
        if (!completed)
            throw std::runtime_error("Unable to place guaranteed starting resource nodes for " +
                                     type.id + " near player " + std::to_string(player + 1));
    }
}

ResourceLayout compensateFairness(ResourceNodeGrid& nodeGrid,
                        const World& world,
                        const Terrain& terrain,
                        const DefinitionRegistry& definitions,
                        std::uint32_t seed,
                        const ResourceFieldDefinition& field,
                        const ResourceNodeDefinition& type,
                        std::uint32_t mapChunksPerSide,
                        const std::vector<glm::vec2>& startingAnchors,
                        const std::vector<std::vector<float>>& travelCosts,
                        const FairnessResult& result,
                        std::uint32_t attempt) {
    ResourceLayout layout;
    const auto& settings = field.generation;
    const auto& policy = *settings.fairness;
    const float band = policy.travelCostBands[result.failedBand];
    const MapArea map{mapChunksPerSide};
    const int side = static_cast<int>(mapChunksPerSide * Terrain::chunkCellCount *
                                      Terrain::spacing / Terrain::semanticCellSize);
    for (const std::size_t player : result.deficientPlayers) {
        DeterministicRandom random(
            seed, settings.stream + ".fairness.player." + std::to_string(player + 1) +
                      ".attempt." + std::to_string(attempt + 1));
        for (std::uint32_t node = 0; node < policy.compensationNodesPerAttempt; ++node) {
            bool placed = false;
            for (std::uint32_t candidateIndex = 0; candidateIndex < 4096 && !placed;
                 ++candidateIndex) {
                // Do not mirror compensation. Each deficient player gets an independent angular
                // stream and a broad distance interval inside the failed travel-cost band.
                glm::vec2 candidate;
                if (candidateIndex < 768) {
                    const float distance = random.range(std::max(10.0F, band * 0.20F),
                                                        std::max(14.0F, band * 0.72F));
                    const float angle = random.range(0.0F, glm::two_pi<float>());
                    candidate = startingAnchors[player] +
                        glm::vec2{std::cos(angle), std::sin(angle)} * distance;
                } else {
                    const float extent = map.halfExtent() -
                                         definitions.matchRules().terrainEdgeMargin;
                    candidate = {random.range(-extent, extent), random.range(-extent, extent)};
                }
                const glm::ivec2 cell = map.gridCell(candidate, side);
                float edgeCost = std::numeric_limits<float>::infinity();
                const int edgeRadius = std::max(
                    1, static_cast<int>(std::ceil((type.collisionRadius + 1.5F) /
                                                 Terrain::semanticCellSize)));
                for (int z = std::max(0, cell.y - edgeRadius);
                     z <= std::min(side - 1, cell.y + edgeRadius); ++z)
                    for (int x = std::max(0, cell.x - edgeRadius);
                         x <= std::min(side - 1, cell.x + edgeRadius); ++x)
                        edgeCost = std::min(edgeCost, travelCosts[player][z * side + x]);
                if (edgeCost > band) continue;
                if (!suitable(terrain, definitions.matchRules(), settings,
                              mapChunksPerSide,
                              candidate.x, candidate.y))
                    continue;
                const ResourceNodeDefinition& variant = selectVariant(definitions, field, random);
                placed = appendNode(layout, nodeGrid, world, definitions, field, variant,
                                    candidate, random.range(0.0F, 360.0F),
                                    random.range(settings.minimumCapacityMultiplier,
                                                 settings.maximumCapacityMultiplier));
                if (placed)
                    layout.fields.push_back({ResourceFieldId{field.id}, candidate,
                                             settings.minimumFieldRadius, 1U});
            }
        }
    }
    return layout;
}
} // namespace

ResourceLayout populateResources(World& world,
                                 const Terrain& terrain,
                                 const DefinitionRegistry& definitions,
                                 std::uint32_t terrainSeed,
                                 std::uint32_t mapChunksPerSide,
                                 float abundanceScale,
                                 const std::vector<glm::vec2>& providedStartingAnchors,
                                 WorldGenerationProgress* progress) {
    ResourceLayout acceptedLayout;
    if (progress) progress->report(WorldGenerationPhase::resources, 0.0F);
    std::vector<glm::vec2> startingAnchors = providedStartingAnchors;
    if (startingAnchors.empty())
        for (const StartingRegion& region :
             selectStartingRegions(terrain, definitions, mapChunksPerSide, 2))
            startingAnchors.push_back(region.anchor);
    const std::uint32_t validatedMapSize = std::clamp(
        mapChunksPerSide, 10U, static_cast<std::uint32_t>(Terrain::chunksPerSide));
    std::vector<std::vector<float>> fairnessTravelCosts;
    fairnessTravelCosts.reserve(startingAnchors.size());
    MovementDomainMask gatheringDomains = 0;
    const auto includeGatheringDomains = [&](const auto& starts) {
        for (const StartingEntityDefinition& start : starts) {
            if (!start.gatheringEnabled) continue;
            if (const EntityArchetype* gatherer = definitions.archetype(
                    EntityArchetypeId{start.archetype}))
                gatheringDomains |= gatherer->movement.domains;
        }
    };
    includeGatheringDomains(definitions.matchRules().playerOne);
    includeGatheringDomains(definitions.matchRules().playerTwo);
    if (gatheringDomains == 0)
        gatheringDomains = movementDomainBit(MovementDomain::land);
    for (glm::vec2 anchor : startingAnchors)
        fairnessTravelCosts.push_back(terrainTravelCosts(
            terrain, validatedMapSize, anchor, gatheringDomains));
    const auto& generated = definitions.matchRules().generatedResourceFields;
    for (std::size_t index = 0; index < generated.size(); ++index) {
        if (progress)
            progress->report(WorldGenerationPhase::resources,
                             static_cast<float>(index) / std::max<std::size_t>(1, generated.size()));
        const std::string& id = generated[index];
        const ResourceFieldDefinition& field =
            *definitions.resourceField(ResourceFieldId{id});
        // The current terrain rules are field-wide. The selected node variant supplies entity
        // presentation, collision, and capacity.
        ResourceNodeDefinition type = *definitions.resource(field.variants.front().node);
        const std::uint32_t mapSize = validatedMapSize;
        const std::uint32_t maximumAttempts = field.generation.fairness
                                                  ? field.generation.fairness->maximumLayoutAttempts
                                                  : 1U;
        std::unordered_set<EntityId> generatedIds;
        FairnessResult fairness;
        ResourceLayout acceptedFieldLayout;
        for (std::uint32_t attempt = 0; attempt < maximumAttempts; ++attempt) {
            for (EntityId entity : generatedIds) world.destroyEntity(entity);
            generatedIds.clear();
            const std::uint32_t layoutSeed = attempt == 0
                ? terrainSeed
                : resourceStreamSeed(terrainSeed, field.generation.stream +
                    ".layout.attempt." + std::to_string(attempt + 1));
            ResourceNodeGrid nodeGrid{world, definitions};
            ResourceLayout layout;
            guaranteedStartingNodes(layout, nodeGrid, world, terrain, definitions, layoutSeed,
                                    field, type, mapSize, startingAnchors);
            fields(layout, nodeGrid, world, terrain, definitions, layoutSeed, field,
                   mapSize, std::clamp(abundanceScale, 0.5F, 2.0F), startingAnchors);
            instantiateLayout(world, definitions, layout);
            for (const Entity& entity : world.entities())
                if (std::any_of(field.variants.begin(), field.variants.end(),
                                [&](const ResourceNodeVariant& variant) {
                                    return variant.node.value == entity.archetype.value;
                                }))
                    generatedIds.insert(entity.id);
            fairness = evaluateFairness(world, definitions, field, mapSize, startingAnchors,
                                        fairnessTravelCosts);
            if (fairness.fair) {
                acceptedFieldLayout = std::move(layout);
                break;
            }
            for (std::uint32_t compensationPass = 0;
                 compensationPass < 8 && !fairness.fair; ++compensationPass) {
                ResourceLayout compensation = compensateFairness(
                    nodeGrid, world, terrain, definitions, layoutSeed, field, type, mapSize,
                    startingAnchors, fairnessTravelCosts, fairness,
                    attempt * 8U + compensationPass);
                instantiateLayout(world, definitions, compensation);
                layout.fields.insert(layout.fields.end(), compensation.fields.begin(),
                                     compensation.fields.end());
                layout.nodes.insert(layout.nodes.end(), compensation.nodes.begin(),
                                    compensation.nodes.end());
                for (const Entity& entity : world.entities())
                    if (std::any_of(field.variants.begin(), field.variants.end(),
                                    [&](const ResourceNodeVariant& variant) {
                                        return variant.node.value == entity.archetype.value;
                                    }))
                        generatedIds.insert(entity.id);
                fairness = evaluateFairness(world, definitions, field, mapSize, startingAnchors,
                                            fairnessTravelCosts);
            }
            if (fairness.fair) {
                acceptedFieldLayout = std::move(layout);
                break;
            }
        }
        if (!fairness.fair) {
            std::ostringstream message;
            message << "Unable to generate a fair " << field.id << " layout after "
                    << maximumAttempts << " deterministic attempts; deficient players:";
            for (const std::size_t player : fairness.deficientPlayers)
                message << ' ' << (player + 1);
            throw std::runtime_error(message.str());
        }
        acceptedLayout.fields.insert(acceptedLayout.fields.end(),
                                     acceptedFieldLayout.fields.begin(),
                                     acceptedFieldLayout.fields.end());
        acceptedLayout.nodes.insert(acceptedLayout.nodes.end(),
                                    acceptedFieldLayout.nodes.begin(),
                                    acceptedFieldLayout.nodes.end());
    }
    if (progress) progress->report(WorldGenerationPhase::resources, 1.0F);
    return acceptedLayout;
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
