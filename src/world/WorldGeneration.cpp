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

std::vector<float> terrainTravelCosts(const Terrain& terrain,
                                      std::uint32_t mapChunksPerSide,
                                      glm::vec2 origin,
                                      MovementDomainMask domainMask, WorldGenerationProgress* progress) {
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
    std::uint32_t visited = 0;
    while (!frontier.empty()) {
        if (progress && ++visited % 256 == 0) progress->throwIfCancellationRequested();
        const auto [cost, current] = frontier.top();
        frontier.pop();
        if (cost != costs[current]) continue;
        const int x = current % side, z = current / side;
        for (const glm::ivec2 direction : directions) {
            const int nx = x + direction.x, nz = z + direction.y;
            if (nx < 0 || nz < 0 || nx >= side || nz >= side) continue;
            const glm::vec2 position = map.gridCellCenter({nx, nz}, side);
            if (terrain.mode() == TerrainMode::v2 &&
                !hasMovementDomain(domainMask, MovementDomain::air) &&
                hasMovementDomain(domainMask, MovementDomain::land) &&
                !terrain.permitsLandSegment(map.gridCellCenter({x, z}, side), position))
                continue;
            const float terrainCost = terrain.movementCostAt(
                position.x, position.y, domainMask);
            if (terrainCost <= 0.0F || !std::isfinite(terrainCost)) continue;
            if (direction.x != 0 && direction.y != 0) {
                const auto a = map.gridCellCenter({nx, z}, side);
                const auto b = map.gridCellCenter({x, nz}, side);
                if (terrain.movementCostAt(a.x, a.y, domainMask) <= 0 ||
                    terrain.movementCostAt(b.x, b.y, domainMask) <= 0) continue;
            }
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
              std::uint32_t mapChunksPerSide, float x, float z) {
    if (!MapArea{mapChunksPerSide}.contains({x, z}, rules.terrainEdgeMargin)) return false;
    const auto& sample = terrain.sampleAt(x, z);
    return !sample.submerged &&
        (sample.tags & generation.requiredTerrainTags) == generation.requiredTerrainTags &&
        (sample.tags & generation.forbiddenTerrainTags) == 0 &&
        sample.slopeDegrees <= generation.maximumSlope;
}

float preferencePenalty(const Terrain& terrain, const ResourceFieldGenerationDefinition& settings,
                        glm::vec2 position) {
    const auto& sample = terrain.sampleAt(position.x, position.y);
    const float height = terrain.heightAt(position.x, position.y) / Terrain::heightScale;
    float penalty = std::find(settings.preferredBiomes.begin(), settings.preferredBiomes.end(),
                             sample.biome) == settings.preferredBiomes.end() ? 1.0F : 0.0F;
    if (sample.moisture < settings.minimumMoisture || sample.moisture > settings.maximumMoisture)
        penalty += 1.0F;
    if (height < settings.minimumHeight || height > settings.maximumHeight) penalty += 1.0F;
    return penalty;
}

bool clearOfStarts(const MatchRulesDefinition& rules,
                   const std::vector<glm::vec2>& startingAnchors,
                   float x,
                   float z,
                   float reservedRadius = 0) {
    const glm::vec2 point{x, z};
    for (const glm::vec2 anchor : startingAnchors)
        if (glm::distance(point, anchor) <= std::max(rules.baseExclusionRadius, reservedRadius))
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

void instantiateLayout(World& world,
                       const DefinitionRegistry& definitions,
                       const ResourceLayout& layout) {
    for (const GeneratedResourceNode& node : layout.nodes) {
        const ResourceNodeDefinition* type = definitions.resource(node.archetype);
        if (!type || !add(world, definitions, *type, node.position.x, node.position.y,
                          node.rotationDegrees))
            throw std::runtime_error("Generated resource layout became invalid before instantiation");
    }
}

float inventoryCapacity(const DefinitionRegistry& definitions,
                        const std::vector<ResourceArchetypeId>& inventory) {
    float capacity = 0;
    for (const auto& id : inventory) capacity += definitions.resource(id)->resourceCapacity;
    return capacity;
}

struct ResourceCandidates {
    std::vector<glm::vec2> positions;
};

float travelCost(const MapArea& map, const std::vector<float>& costs, glm::vec2 position) {
    const int side = static_cast<int>(map.extent() / Terrain::semanticCellSize);
    const auto cell = map.gridCell(position, side);
    return costs[cell.y * side + cell.x];
}

bool comparableAccess(float a, float b, const ResourceFieldGenerationDefinition& settings) {
    return std::isfinite(a) && std::isfinite(b) &&
        (std::abs(a - b) <= settings.accessCostTolerance ||
         std::min(a, b) >= std::max(a, b) * settings.minimumAccessRatio);
}

// Use whole-field capacity conservatively: any radius-R circle intersecting this field can
// only intersect fields whose centers are within 2R plus both field radii. Checking each
// neighborhood before insertion bounds even overlapping/sliding windows between node centers.
bool fieldSpaceAvailable(const ResourceLayout& layout, const DefinitionRegistry& definitions,
                         const ResourceFieldDefinition& field, glm::vec2 center,
                         float radius, float capacity) {
    const auto& settings = field.generation;
    for (const auto& existing : layout.fields) {
        const auto& other = definitions.resourceField(existing.definition)->generation;
        if (glm::distance(center, existing.center) < radius + existing.radius +
            std::max(settings.minimumFieldSeparation, other.minimumFieldSeparation)) return false;
    }
    const auto neighborhoodCapacity = [&](glm::vec2 origin, float originRadius) {
        float total = glm::distance(origin, center) <=
            2 * settings.localCapacityRadius + originRadius + radius ? capacity : 0.0F;
        for (const auto& existing : layout.fields)
            if (existing.definition.value == field.id && glm::distance(origin, existing.center) <=
                2 * settings.localCapacityRadius + originRadius + existing.radius)
                total += existing.capacity;
        return total;
    };
    if (neighborhoodCapacity(center, radius) > settings.maximumLocalCapacity) return false;
    for (const auto& existing : layout.fields)
        if (existing.definition.value == field.id &&
            neighborhoodCapacity(existing.center, existing.radius) > settings.maximumLocalCapacity)
            return false;
    return true;
}

bool placeField(ResourceLayout& layout, const World& world, const Terrain& terrain,
                const DefinitionRegistry& definitions, const ResourceFieldDefinition& field,
                const ResourceCandidates& candidates, std::uint32_t seed, std::uint32_t ordinal,
                std::uint32_t mapSize, const std::vector<glm::vec2>& anchors,
                const std::vector<std::vector<float>>& costs, std::size_t player,
                bool starting, float matchedCost, WorldGenerationProgress* progress) {
    const auto& settings = field.generation;
    const auto& inventory = starting ? settings.startingNodes : settings.expansionNodes;
    const float capacity = inventoryCapacity(definitions, inventory);
    const MapArea map{mapSize};
    DeterministicRandom random(seed, settings.stream + ".field." + std::to_string(ordinal));
    const bool constructive = terrain.mode() == TerrainMode::v2 && terrain.repairLevel() == 2;
    const float radius =
        constructive ? settings.minimumFieldRadius
                     : random.range(settings.minimumFieldRadius, settings.maximumFieldRadius);
    const float openingRadius =
        terrain.v2Landscape() ? terrain.v2Landscape()->plan.recipe().openingRadius : 0;
    const auto noiseSeed = resourceStreamSeed(seed, settings.stream + ".potential");
    struct Candidate { glm::vec2 position; float score; float access; };
    std::vector<Candidate> ordered;
    for (std::size_t index = 0; index < candidates.positions.size(); ++index) {
        if (progress && index % 1024 == 0) progress->throwIfCancellationRequested();
        const auto position = candidates.positions[index];
        if (!clearOfStarts(definitions.matchRules(),
                           anchors,
                           position.x,
                           position.y,
                           openingRadius > 0 ? openingRadius + radius : 0))
            continue;
        const float own = travelCost(map, costs[player], position);
        if (!std::isfinite(own)) continue;
        bool accessible = true;
        float closest = own, farthest = own;
        for (std::size_t other = 0; other < anchors.size(); ++other) {
            const float cost = travelCost(map, costs[other], position);
            closest = std::min(closest, cost); farthest = std::max(farthest, cost);
            if (!std::isfinite(cost)) accessible = false;
            if (!settings.contested && other != player && cost <= own + radius * 2) accessible = false;
            if (!starting) {
                float protectedCost = settings.minimumExpansionTravelCost;
                for (const auto& existing : layout.fields)
                    if (existing.startingPlayer == other + 1)
                        protectedCost = std::max(protectedCost,
                            existing.accessCost + existing.radius + settings.minimumFieldSeparation + radius);
                if (cost < protectedCost) accessible = false;
            }
        }
        if (!accessible || (settings.contested && !comparableAccess(closest, farthest, settings)) ||
            (matchedCost >= 0 && !comparableAccess(own, matchedCost, settings))) continue;
        const float preference = preferencePenalty(terrain, settings, position);
        const float potential = regionalPotential(position.x, position.y, settings.regionScale, noiseSeed);
        // Starting supply favors proximity. Expansion fields spread across biome-favored regions;
        // the random score changes locations without changing their inventory or budget.
        const float jitter = static_cast<float>(coordinateHash(
            static_cast<int>(index), static_cast<int>(ordinal), seed) >> 8U) / 16777215.0F;
        float score = starting ? own + preference * 3 + jitter * 3 :
            preference * 12 + (1 - potential) * 18 + jitter * 65;
        if (!starting && matchedCost >= 0) score += std::abs(own - matchedCost);
        if (!starting && settings.contested) score += farthest - closest;
        if (constructive)
            score =
                starting ? own : jitter * 65 + (matchedCost >= 0 ? std::abs(own - matchedCost) : 0);
        ordered.push_back({position, score, own});
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.score < b.score;
    });
    for (const auto& candidate : ordered) {
        if (progress) progress->throwIfCancellationRequested();
        if (!fieldSpaceAvailable(layout, definitions, field, candidate.position, radius, capacity)) continue;
        ResourceNodeGrid grid{world, definitions};
        for (const auto& node : layout.nodes)
            grid.insert(node.position, definitions.resource(node.archetype)->collisionRadius);
        std::vector<GeneratedResourceNode> nodes;
        float maximumCost = 0;
        bool valid = true;
        for (const auto& id : inventory) {
            const auto& type = *definitions.resource(id);
            bool placed = false;
            for (std::uint32_t attempt = 0;
                 attempt < (constructive ? 32U : settings.placementAttemptsPerField);
                 ++attempt) {
                const float angle = constructive ? glm::two_pi<float>() *
                                                       (static_cast<float>(nodes.size()) /
                                                            static_cast<float>(inventory.size()) +
                                                        static_cast<float>(attempt) / 32.0F)
                                                 : random.range(0, glm::two_pi<float>());
                const float distance =
                    constructive ? radius * 0.65F : radius * std::sqrt(random.range(0, 1));
                const auto position = candidate.position + glm::vec2{std::cos(angle), std::sin(angle)} * distance;
                if (!suitable(terrain,
                              definitions.matchRules(),
                              settings,
                              mapSize,
                              position.x,
                              position.y) ||
                    !clearOfStarts(definitions.matchRules(),
                                   anchors,
                                   position.x,
                                   position.y,
                                   openingRadius > 0 ? openingRadius + type.collisionRadius : 0) ||
                    !grid.canPlace(position, type.collisionRadius, settings.minimumNodeSpacing) ||
                    overlapsObject(world, definitions, position, type.collisionRadius))
                    continue;
                const float access = travelCost(map, costs[player], position);
                if (!std::isfinite(access)) continue;
                bool safe = true;
                for (std::size_t other = 0; other < anchors.size(); ++other) {
                    const float otherCost = travelCost(map, costs[other], position);
                    if (!std::isfinite(otherCost) || (!settings.contested && other != player && otherCost <= access))
                        safe = false;
                    if (!starting) {
                        float protectedCost = settings.minimumExpansionTravelCost;
                        for (const auto& existing : layout.fields)
                            if (existing.startingPlayer == other + 1)
                                protectedCost = std::max(protectedCost,
                                    existing.accessCost + existing.radius + settings.minimumFieldSeparation);
                        if (otherCost < protectedCost) safe = false;
                    }
                }
                if (!safe) continue;
                nodes.push_back({ResourceFieldId{field.id}, id, position, random.range(0, 360), layout.fields.size()});
                grid.insert(position, type.collisionRadius);
                maximumCost = std::max(maximumCost, access);
                placed = true;
                break;
            }
            if (!placed) { valid = false; break; }
        }
        if (!valid || (matchedCost >= 0 && !comparableAccess(maximumCost, matchedCost, settings))) continue;
        layout.fields.push_back({ResourceFieldId{field.id}, candidate.position, radius,
            static_cast<std::uint32_t>(inventory.size()), starting ? static_cast<std::uint32_t>(player + 1) : 0,
            capacity, maximumCost});
        layout.nodes.insert(layout.nodes.end(), nodes.begin(), nodes.end());
        return true;
    }
    return false;
}
} // namespace

ResourceLayout populateResources(World& world, const Terrain& terrain,
                                 const DefinitionRegistry& definitions, std::uint32_t terrainSeed,
                                 std::uint32_t mapChunksPerSide, float abundanceScale,
                                 const std::vector<glm::vec2>& providedStartingAnchors,
                                 WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::resources, 0);
    const auto mapSize = std::clamp(mapChunksPerSide, 10U, static_cast<std::uint32_t>(Terrain::chunksPerSide));
    const MapArea map{mapSize};
    auto anchors = providedStartingAnchors;
    if (anchors.empty())
        for (const auto& region : selectStartingRegions(terrain, definitions, mapSize, 2, terrainSeed, progress))
            anchors.push_back(region.anchor);
    MovementDomainMask domains = 0;
    for (const auto* starts : {&definitions.matchRules().playerOne, &definitions.matchRules().playerTwo})
        for (const auto& start : *starts)
            if (start.gatheringEnabled)
                if (const auto* type = definitions.archetype(EntityArchetypeId{start.archetype}))
                    domains |= type->movement.domains;
    if (domains == 0) domains = movementDomainBit(MovementDomain::land);
    std::vector<std::vector<float>> costs;
    for (auto anchor : anchors) costs.push_back(terrainTravelCosts(terrain, mapSize, anchor, domains, progress));
    const auto& generated = definitions.matchRules().generatedResourceFields;
    std::vector<ResourceCandidates> candidates(generated.size());
    const int side = static_cast<int>(map.extent() / Terrain::semanticCellSize);
    for (std::size_t family = 0; family < generated.size(); ++family) {
        const auto& field = *definitions.resourceField(ResourceFieldId{generated[family]});
        for (int z = 0; z < side; ++z) {
            if (progress) progress->throwIfCancellationRequested();
            for (int x = 0; x < side; ++x) {
                const auto position = map.gridCellCenter({x, z}, side);
                if (suitable(terrain, definitions.matchRules(), field.generation, mapSize, position.x, position.y))
                    candidates[family].positions.push_back(position);
            }
        }
    }
    ResourceLayout layout;
    // Place every family's reserved starting inventory before any expansion field.
    for (std::size_t family = 0; family < generated.size(); ++family) {
        const auto& field = *definitions.resourceField(ResourceFieldId{generated[family]});
        if (field.generation.startingNodes.empty()) continue;
        float matchedCost = -1;
        for (std::size_t player = 0; player < anchors.size(); ++player) {
            if (!placeField(layout, world, terrain, definitions, field, candidates[family], terrainSeed,
                static_cast<std::uint32_t>(player), mapSize, anchors, costs, player, true, matchedCost, progress))
                throw ResourcePlacementFailure("Unable to place reserved starting " + field.id +
                                               " for player " + std::to_string(player + 1));
            matchedCost = layout.fields.back().accessCost;
            if (progress) progress->diagnostic("opening " + field.id + " player=" + std::to_string(player + 1) +
                " capacity=" + std::to_string(layout.fields.back().capacity) + " travel=" + std::to_string(matchedCost));
        }
    }
    for (std::size_t family = 0; family < generated.size(); ++family) {
        const auto& field = *definitions.resourceField(ResourceFieldId{generated[family]});
        const auto& settings = field.generation;
        const auto players = static_cast<std::uint32_t>(anchors.size());
        const auto roundedFields = static_cast<std::uint32_t>(std::round(
                                       mapSize * mapSize * settings.fieldsPerSquareChunk *
                                       std::clamp(abundanceScale, 0.5F, 2.0F) / players)) *
                                   players;
        // V2 allocation can resolve to zero. Repairs must never invent extra expansion supply.
        const auto totalFields =
            terrain.mode() == TerrainMode::v2 ? roundedFields : std::max(players, roundedFields);
        const auto startingFields = settings.startingNodes.empty() ? 0U : players;
        const auto expansionFields =
            terrain.mode() == TerrainMode::v2 ? totalFields : totalFields - startingFields;
        const float budget = startingFields * inventoryCapacity(definitions, settings.startingNodes) +
            expansionFields * inventoryCapacity(definitions, settings.expansionNodes);
        if (progress) progress->diagnostic("budget " + field.id + " capacity=" + std::to_string(budget) +
            " startingFields=" + std::to_string(startingFields) + " expansionFields=" + std::to_string(expansionFields));
        const auto base = layout;
        bool placed = false;
        for (std::uint32_t attempt = 0; attempt < settings.maximumLayoutAttempts && !placed; ++attempt) {
            if (progress) progress->throwIfCancellationRequested();
            layout = base;
            placed = true;
            const auto seed = resourceStreamSeed(
                terrain.mode() == TerrainMode::v2 && terrain.repairLevel() == 2 ? 0U : terrainSeed,
                settings.stream + ".layout." + std::to_string(attempt));
            for (std::uint32_t ordinal = 0; ordinal < expansionFields && placed; ++ordinal) {
                const auto player = ordinal % players;
                const float matched = player == 0 ? -1.0F : layout.fields.back().accessCost;
                placed = placeField(layout, world, terrain, definitions, field, candidates[family], seed,
                    players + ordinal, mapSize, anchors, costs, player, false,
                    settings.contested ? -1.0F : matched, progress);
            }
            if (progress) progress->diagnostic("layout " + field.id + " attempt=" + std::to_string(attempt + 1) +
                " complete=" + std::to_string(placed));
        }
        if (!placed)
            throw ResourcePlacementFailure("Unable to place budgeted " + field.id + " after " +
                                           std::to_string(settings.maximumLayoutAttempts) +
                                           " deterministic attempts");
        if (progress) progress->report(WorldGenerationPhase::resources,
            static_cast<float>(family + 1) / std::max<std::size_t>(1, generated.size()));
    }
    // No entities are created until all budgets are successfully placed. A failed layout never
    // leaks partial resources into the caller's world.
    if (progress) progress->throwIfCancellationRequested();
    instantiateLayout(world, definitions, layout);
    if (progress)
        for (const auto& id : generated) {
            const auto count = std::count_if(layout.nodes.begin(), layout.nodes.end(),
                [&](const auto& node) { return node.field.value == id; });
            progress->diagnostic("accepted " + id + " nodes=" + std::to_string(count));
            for (const auto& field : layout.fields)
                if (field.definition.value == id)
                    progress->diagnostic("field " + id + " x=" + std::to_string(field.center.x) +
                        " z=" + std::to_string(field.center.y) + " capacity=" + std::to_string(field.capacity) +
                        " startingPlayer=" + std::to_string(field.startingPlayer) + " travel=" + std::to_string(field.accessCost));
        }
    return layout;
}

VegetationField generateVegetation(const World& world,
                                   const Terrain& terrain,
                                   const DefinitionRegistry& definitions,
                                   std::uint32_t terrainSeed,
                                   std::uint32_t mapChunksPerSide,
                                   WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::decoration, 0.0F);
    const MapArea map{mapChunksPerSide};
    std::vector<VegetationChunk> chunks;
    const float chunkSize = static_cast<float>(Terrain::chunkCellCount) * Terrain::spacing;
    const float origin = -map.halfExtent();
    chunks.reserve(mapChunksPerSide * mapChunksPerSide);
    for (std::uint32_t z = 0; z < mapChunksPerSide; ++z)
        for (std::uint32_t x = 0; x < mapChunksPerSide; ++x)
            chunks.push_back({{static_cast<int>(x), static_cast<int>(z)},
                              {origin + (x + 0.5F) * chunkSize, origin + (z + 0.5F) * chunkSize},
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
        VegetationGenerationDefinition settings = vegetationRules[ruleIndex];
        const EntityArchetype* type = definitions.archetype(settings.archetype);
        if (!type) continue;
        if (type->tags.contains("v2-only") && !terrain.v2Landscape())
            continue;
        if (terrain.v2Landscape() && type->tags.contains("tree")) {
            const auto& recipe = terrain.v2Landscape()->plan.recipe();
            settings.clustersPerChunk *= recipe.forestDensity;
            settings.minimumInstancesPerCluster = static_cast<std::uint32_t>(
                std::ceil(settings.minimumInstancesPerCluster * recipe.forestClusterSize));
            settings.maximumInstancesPerCluster = static_cast<std::uint32_t>(
                std::ceil(settings.maximumInstancesPerCluster * recipe.forestClusterSize));
            settings.densityScale = recipe.forestDensityScale;
            settings.coverage = recipe.forestCoverage;
            settings.minimumSpacing = recipe.forestSpacing;
            settings.minimumScale *= recipe.forestScale;
            settings.maximumScale *= recipe.forestScale;
        }
        const bool isPlantFoliage = type->tags.contains("grass") ||
                                    type->tags.contains("weed") ||
                                    type->tags.contains("flower") ||
                                    type->tags.contains("tree");
        const float clustersPerChunk = settings.clustersPerChunk *
            (isPlantFoliage ? definitions.matchRules().foliageDensityMultiplier : 1.0F);
        const std::uint32_t densitySeed =
            resourceStreamSeed(terrainSeed, settings.stream + ".density");
        for (VegetationChunk& chunk : chunks) {
            const std::string stream = settings.stream + "." +
                std::to_string(chunk.coordinate.x) + "." + std::to_string(chunk.coordinate.y);
            DeterministicRandom random(terrainSeed, stream);
            const float integral = std::floor(clustersPerChunk);
            std::uint32_t clusterOpportunities = static_cast<std::uint32_t>(integral);
            if (random.range(0.0F, 1.0F) < clustersPerChunk - integral)
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
                    if (terrain.v2Landscape() &&
                        (terrain.v2Landscape()->plan.reservedRoute({x, z}) ||
                         terrain.v2Landscape()->plan.openingInfluence({x, z}) > 0.0F))
                        continue;
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
    return VegetationField{std::move(chunks)};
}
} // namespace strategy
