#include "world/Navigation.hpp"

#include "terrain/Terrain.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "world/Collision.hpp"
#include "world/World.hpp"
#include "world/GenerationProgress.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <deque>
#include <glm/common.hpp>
#include <limits>
#include <queue>

namespace strategy {
namespace {
void mix(std::uint64_t& hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}
bool sameShape(const SpatialShape& left, const SpatialShape& right) {
    return left.kind == right.kind && glm::all(glm::equal(left.center, right.center)) &&
           left.radius == right.radius &&
           glm::all(glm::equal(left.halfExtents, right.halfExtents)) &&
           left.rotationDegrees == right.rotationDegrees;
}
constexpr std::array<std::array<int, 2>, 8> directions{
    {{{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}}, {{1, 1}}, {{1, -1}}, {{-1, 1}}, {{-1, -1}}}};

float terrainTravelCost(const Terrain& terrain, glm::vec3 position, MovementDomain domain) {
    if (!terrain.v2Landscape())
        return terrain.movementCostAt(position.x, position.z, movementDomainBit(domain));
    const float speed = terrain.movementSpeedScaleAt(position.x, position.z, movementDomainBit(domain));
    return speed > 0 ? 1.F / speed : 0.F;
}
} // namespace

Navigation::Navigation(const Terrain& terrain, const DefinitionRegistry& definitions,
                       std::uint32_t mapChunksPerSide, WorldGenerationProgress* progress)
    : map_(mapChunksPerSide), definitions_(definitions) {
    rebuildTerrain(terrain, mapChunksPerSide, progress);
}

int Navigation::indexOf(int x, int z) const { return z * side_ + x; }
int Navigation::gridCoordinate(float value) const {
    return std::clamp(static_cast<int>((value + map_.halfExtent()) / cellSize), 0, side_ - 1);
}
glm::vec3 Navigation::positionOf(int x, int z) const {
    return {-map_.halfExtent() + (static_cast<float>(x) + 0.5F) * cellSize, 0.0F,
            -map_.halfExtent() + (static_cast<float>(z) + 0.5F) * cellSize};
}

void Navigation::refreshTerrainHeights(const Terrain& terrain, const TerrainFoundation* cut) {
    // Grading changes both physical clearance and the travel-time cost of slopes.
    occupancyByProfile_.clear();
    terrainEdgesByProfile_.clear();
    flowFields_.clear();
    const float reach = cut ? cut->outerRadius + cut->levelingBlendWidth + Terrain::spacing * 3 : 0;
    const int x0 = cut ? gridCoordinate(cut->center.x - reach) : 0;
    const int x1 = cut ? gridCoordinate(cut->center.x + reach) : side_ - 1;
    const int z0 = cut ? gridCoordinate(cut->center.z - reach) : 0;
    const int z1 = cut ? gridCoordinate(cut->center.z + reach) : side_ - 1;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const auto p = positionOf(x, z);
            heights_[indexOf(x, z)] = terrain.heightAt(p.x, p.z);
            if (map_.contains({p.x, p.z}, definitions_.matchRules().terrainEdgeMargin))
                for (const auto domain : movementDomains)
                    terrainMovementCosts_[indexOf(x, z)][static_cast<std::size_t>(domain)] =
                        terrainTravelCost(terrain, p, domain);
        }
}

void Navigation::rebuildTerrain(const Terrain& terrain, std::uint32_t mapChunksPerSide,
                                WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::navigation, 0.0F);
    map_ = MapArea{mapChunksPerSide};
    side_ = std::max(1, static_cast<int>(std::ceil(map_.extent() / cellSize)));
    heights_.resize(static_cast<std::size_t>(side_ * side_));
    terrainMovementCosts_.resize(static_cast<std::size_t>(side_ * side_));
    for (int z = 0; z < side_; ++z) {
        if (progress && z % 8 == 0)
            progress->report(WorldGenerationPhase::navigation,
                             static_cast<float>(z) / side_);
        for (int x = 0; x < side_; ++x) {
            const glm::vec3 position = positionOf(x, z);
            const float height = terrain.heightAt(position.x, position.z);
            heights_[indexOf(x, z)] = height;
            if (!map_.contains({position.x, position.z},
                               definitions_.matchRules().terrainEdgeMargin)) {
                // The active-map boundary is a universal barrier. Derive it from MapArea rather
                // than the maximum terrain edge so centered 10x10 and 15x15 maps behave exactly
                // like the physical edge of a 20x20 map.
                terrainMovementCosts_[indexOf(x, z)].fill(0.0F);
            } else {
                for (const auto domain : movementDomains)
                    terrainMovementCosts_[indexOf(x, z)][static_cast<std::size_t>(domain)] =
                        terrainTravelCost(terrain, position, domain);
            }
        }
    }
    obstacleShapes_.clear();
    occupancyByProfile_.clear();
    terrainEdgesByProfile_.clear();
    flowFields_.clear();
    if (progress) progress->report(WorldGenerationPhase::navigation, 1.0F);
}

void Navigation::synchronizeObstacles(const Terrain& terrain, const World& world) {
    std::map<EntityId, SpatialShape> current;
    for (const Entity& entity : world.entities())
        if (!entity.unitControl && !entity.flight &&
            (entity.kind != EntityKind::resource || !entity.resource ||
             entity.resource.remaining > 0.0F))
            current.emplace(entity.id, spatialShape(definitions_, entity));

    std::vector<SpatialShape> dirtyShapes;
    for (const auto& [id, oldShape] : obstacleShapes_) {
        const auto found = current.find(id);
        if (found == current.end() || !sameShape(oldShape, found->second))
            dirtyShapes.push_back(oldShape);
    }
    for (const auto& [id, newShape] : current) {
        const auto found = obstacleShapes_.find(id);
        if (found == obstacleShapes_.end() || !sameShape(found->second, newShape))
            dirtyShapes.push_back(newShape);
    }
    if (dirtyShapes.empty()) return;

    for (auto& [key, occupancyGrid] : occupancyByProfile_) {
        if (key.ignoresEntityObstacles) continue;
        const float radius = static_cast<float>(key.radiusClass) / 10.0F;
        std::vector<std::uint8_t> dirty(static_cast<std::size_t>(side_ * side_), 0);
        for (const SpatialShape& changed : dirtyShapes) {
            const SpatialShape area = expanded(changed, radius + cellSize);
            const glm::vec2 bounds = axisAlignedHalfExtents(area);
            const int minX = gridCoordinate(area.center.x - bounds.x),
                      maxX = gridCoordinate(area.center.x + bounds.x),
                      minZ = gridCoordinate(area.center.y - bounds.y),
                      maxZ = gridCoordinate(area.center.y + bounds.y);
            for (int z = minZ; z <= maxZ; ++z)
                for (int x = minX; x <= maxX; ++x)
                    dirty[indexOf(x, z)] = 1;
        }
        for (std::size_t index = 0; index < dirty.size(); ++index)
            if (dirty[index])
                occupancyGrid[index] =
                    terrainPassable(terrain, index, radius, {key.domains, key.ignoresEntityObstacles}) ? 1
                                                                                              : 0;
        for (const auto& [id, obstacle] : current) {
            (void)id;
            const SpatialShape blocked = expanded(obstacle, radius);
            const glm::vec2 bounds = axisAlignedHalfExtents(blocked);
            const int minX = gridCoordinate(blocked.center.x - bounds.x - cellSize),
                      maxX = gridCoordinate(blocked.center.x + bounds.x + cellSize),
                      minZ = gridCoordinate(blocked.center.y - bounds.y - cellSize),
                      maxZ = gridCoordinate(blocked.center.y + bounds.y + cellSize);
            for (int z = minZ; z <= maxZ; ++z)
                for (int x = minX; x <= maxX; ++x) {
                    const int index = indexOf(x, z);
                    if (!dirty[index]) continue;
                    const glm::vec3 position = positionOf(x, z);
                    if (signedDistance(blocked, {position.x, position.z}) < 0.0F)
                        occupancyGrid[index] = 0;
                }
        }
    }
    obstacleShapes_ = std::move(current);
    flowFields_.clear();
}

const std::vector<std::uint8_t>& Navigation::occupancy(const Terrain& terrain, const World& world, float radius,
                                                       NavigationProfile profile) {
    synchronizeObstacles(terrain, world);
    const OccupancyKey key{static_cast<int>(std::round(radius * 10.0F)), profile.domains,
                           profile.ignoresEntityObstacles};
    if (const auto found = occupancyByProfile_.find(key); found != occupancyByProfile_.end())
        return found->second;
    std::vector<std::uint8_t> result(terrainMovementCosts_.size(), 0);
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = terrainPassable(terrain, index, radius, profile) ? 1 : 0;
    if (profile.ignoresEntityObstacles)
        return occupancyByProfile_.emplace(key, std::move(result)).first->second;
    for (const Entity& entity : world.entities()) {
        if (entity.flight || entity.unitControl ||
            (entity.kind == EntityKind::resource && entity.resource &&
             entity.resource.remaining <= 0.0F))
            continue;
        const SpatialShape blocked = expanded(spatialShape(definitions_, entity), radius);
        const glm::vec2 bounds = axisAlignedHalfExtents(blocked);
        const int minimumX = gridCoordinate(blocked.center.x - bounds.x - cellSize),
                  maximumX = gridCoordinate(blocked.center.x + bounds.x + cellSize),
                  minimumZ = gridCoordinate(blocked.center.y - bounds.y - cellSize),
                  maximumZ = gridCoordinate(blocked.center.y + bounds.y + cellSize);
        for (int z = minimumZ; z <= maximumZ; ++z)
            for (int x = minimumX; x <= maximumX; ++x) {
                const glm::vec3 position = positionOf(x, z);
                if (signedDistance(blocked, {position.x, position.z}) < 0.0F)
                    result[indexOf(x, z)] = 0;
            }
    }
    return occupancyByProfile_.emplace(key, std::move(result)).first->second;
}

bool Navigation::terrainPassable(const Terrain& terrain, std::size_t index, float radius, NavigationProfile profile) const {
    if (movementCost(index, profile.domains) <= 0)
        return false;
    if (!terrain.v2Landscape() || hasMovementDomain(profile.domains, MovementDomain::air) ||
        !hasMovementDomain(profile.domains, MovementDomain::land))
        return true;
    const auto p = positionOf(static_cast<int>(index) % side_, static_cast<int>(index) / side_);
    return terrain.permitsMovementSegment({p.x, p.z}, {p.x, p.z}, radius, profile.domains);
}

bool Navigation::canStep(
    const Terrain& terrain, int x, int z, int nx, int nz, float radius, NavigationProfile profile) const {
    if (!terrain.v2Landscape() || hasMovementDomain(profile.domains, MovementDomain::air) ||
        !hasMovementDomain(profile.domains, MovementDomain::land))
        return true;
    const auto a = positionOf(x, z), b = positionOf(nx, nz);
    const OccupancyKey key{static_cast<int>(std::round(radius * 10.0F)), profile.domains,
                           profile.ignoresEntityObstacles};
    auto& edges = terrainEdgesByProfile_[key];
    if (edges.empty()) edges.resize(static_cast<std::size_t>(side_ * side_));
    const auto direction = std::find(directions.begin(), directions.end(),
                                     std::array<int, 2>{nx - x, nz - z});
    const auto bit = static_cast<unsigned>(direction - directions.begin());
    auto& flags = edges[indexOf(x, z)];
    if (!(flags & (1U << (bit + 8)))) {
        const bool allowed = terrain.permitsMovementSegment(
            {a.x, a.z}, {b.x, b.z}, radius, profile.domains);
        flags |= static_cast<std::uint16_t>((1U << (bit + 8)) | (allowed ? 1U << bit : 0));
        const auto reverse = std::find(directions.begin(), directions.end(),
                                      std::array<int, 2>{x - nx, z - nz});
        const auto reverseBit = static_cast<unsigned>(reverse - directions.begin());
        edges[indexOf(nx, nz)] |= static_cast<std::uint16_t>(
            (1U << (reverseBit + 8)) | (allowed ? 1U << reverseBit : 0));
    }
    return (flags & (1U << bit)) != 0;
}

float Navigation::movementCost(std::size_t index, MovementDomainMask domains) const {
    float result = std::numeric_limits<float>::max();
    for (const MovementDomain domain : movementDomains) {
        if (!hasMovementDomain(domains, domain)) continue;
        const float cost = terrainMovementCosts_[index][static_cast<std::size_t>(domain)];
        if (cost > 0.0F) result = std::min(result, cost);
    }
    return result == std::numeric_limits<float>::max() ? 0.0F : result;
}

std::vector<glm::vec3> Navigation::findPath(
    const Terrain& terrain, const World& world, glm::vec3 start, glm::vec3 destination, float radius, EntityId ignored,
    NavigationProfile profile) {
    const int target = indexOf(gridCoordinate(destination.x), gridCoordinate(destination.z));
    const auto& cached = occupancy(terrain, world, radius, profile);
    const int source = indexOf(gridCoordinate(start.x), gridCoordinate(start.z));
    const SpatialShape actorAtDestination{FootprintShape::circle,
                                          {destination.x, destination.z}, radius,
                                          glm::vec2{radius}, 0.0F};
    const bool preciseDestinationValid =
        cached[target] != 0 && map_.contains({destination.x, destination.z}, radius) &&
        terrain.permitsMovementSegment({destination.x, destination.z},
                                         {destination.x, destination.z}, radius, profile.domains) &&
        (profile.ignoresEntityObstacles ||
         !overlapsObject(world, definitions_, actorAtDestination, ignored));
    auto direct = pathFromGoals(terrain, world, start, {target}, {destination.x, destination.z}, radius,
                                ignored, static_cast<std::uint64_t>(target), profile);
    if (!direct.empty() && cached[target] != 0) {
        const auto last = direct.back();
        if (preciseDestinationValid && terrain.permitsMovementSegment(
                {last.x, last.z}, {destination.x, destination.z}, radius, profile.domains))
            direct.push_back(destination);
        return direct;
    }

    std::vector<std::uint8_t> passable = cached;
    passable[source] = 1;

    // An order may point into water, a cliff, a building, outside the map, or a
    // disconnected region. Search the actor's connected component and choose the
    // reachable cell closest to the requested world-space point. Stable index
    // tie-breaking keeps this authoritative choice deterministic.
    std::vector<std::uint8_t> visited(passable.size(), 0);
    std::deque<int> open;
    open.push_back(source);
    visited[source] = 1;
    int resolved = source;
    auto distanceSquared = [&](int index) {
        const glm::vec3 position = positionOf(index % side_, index / side_);
        const glm::vec2 offset{position.x - destination.x, position.z - destination.z};
        return glm::dot(offset, offset);
    };
    float bestDistance = distanceSquared(source);
    while (!open.empty()) {
        const int current = open.front();
        open.pop_front();
        const float candidateDistance = distanceSquared(current);
        if (candidateDistance < bestDistance ||
            (candidateDistance == bestDistance && current < resolved)) {
            resolved = current;
            bestDistance = candidateDistance;
        }
        const int x = current % side_, z = current / side_;
        for (const auto& direction : directions) {
            const int nx = x + direction[0], nz = z + direction[1];
            if (nx < 0 || nz < 0 || nx >= side_ || nz >= side_ ||
                !canStep(terrain, x, z, nx, nz, radius, profile))
                continue;
            const int next = indexOf(nx, nz);
            if (visited[next] || !passable[next]) continue;
            if (direction[0] && direction[1] &&
                (!passable[indexOf(x + direction[0], z)] ||
                 !passable[indexOf(x, z + direction[1])]))
                continue;
            visited[next] = 1;
            open.push_back(next);
        }
    }

    if (resolved == source) return {start};
    std::uint64_t goalKey = static_cast<std::uint64_t>(target);
    mix(goalKey, static_cast<std::uint64_t>(resolved));
    auto result = pathFromGoals(terrain, world, start, {resolved}, {destination.x, destination.z}, radius,
                                ignored, goalKey, profile);
    if (!result.empty() && resolved == target && preciseDestinationValid) {
        const auto last = result.back();
        if (terrain.permitsMovementSegment(
                {last.x, last.z}, {destination.x, destination.z}, radius, profile.domains))
            result.push_back(destination);
    }
    return result;
}

std::vector<glm::vec3> Navigation::findPath(
    const Terrain& terrain, const World& world, glm::vec3 start, const NavigationGoalRegion& goal,
    float radius, EntityId ignored, NavigationProfile profile) {
    const auto& passable = occupancy(terrain, world, radius, profile);
    const SpatialShape actorBoundary = expanded(
        goal.target, radius + std::max(0.0F, goal.interactionRange * 0.5F));
    const float outerDistance = radius + std::max(0.0F, goal.interactionRange) + cellSize * 0.75F;
    std::vector<int> goals;
    for (int z = 0; z < side_; ++z)
        for (int x = 0; x < side_; ++x) {
            const int index = indexOf(x, z);
            if (!passable[index]) continue;
            const glm::vec3 position = positionOf(x, z);
            const float distance = signedDistance(goal.target, {position.x, position.z});
            if (distance >= radius && distance <= outerDistance) goals.push_back(index);
        }
    if (goals.empty()) return {};
    const glm::vec2 nearestBoundary = closestBoundaryPoint(actorBoundary, goal.approachOrigin);
    std::stable_sort(goals.begin(), goals.end(), [&](int left, int right) {
        const glm::vec3 leftPosition = positionOf(left % side_, left / side_);
        const glm::vec3 rightPosition = positionOf(right % side_, right / side_);
        const glm::vec2 leftOffset = glm::vec2{leftPosition.x, leftPosition.z} - nearestBoundary;
        const glm::vec2 rightOffset = glm::vec2{rightPosition.x, rightPosition.z} - nearestBoundary;
        const float leftDistance = glm::dot(leftOffset, leftOffset);
        const float rightDistance = glm::dot(rightOffset, rightOffset);
        return leftDistance == rightDistance ? left < right : leftDistance < rightDistance;
    });
    // Stable nearby slots keep groups from selecting one identical access point without
    // introducing authoritative reservation state.
    const std::size_t nearbySlots = std::min<std::size_t>(goals.size(), 8);
    const std::size_t slot = goal.requesterEntity == 0
                                 ? 0
                                 : static_cast<std::size_t>(goal.requesterEntity % nearbySlots);
    const glm::vec3 slotPosition = positionOf(goals[slot] % side_, goals[slot] / side_);
    const glm::vec2 preferred = closestBoundaryPoint(
        actorBoundary, {slotPosition.x, slotPosition.z});
    std::uint64_t goalKey = goal.targetEntity;
    mix(goalKey, std::bit_cast<std::uint32_t>(goal.target.center.x));
    mix(goalKey, std::bit_cast<std::uint32_t>(goal.target.center.y));
    mix(goalKey, std::bit_cast<std::uint32_t>(goal.target.rotationDegrees));
    mix(goalKey, std::bit_cast<std::uint32_t>(goal.interactionRange));
    mix(goalKey, static_cast<std::uint64_t>(gridCoordinate(preferred.x)));
    mix(goalKey, static_cast<std::uint64_t>(gridCoordinate(preferred.y)));
    auto result = pathFromGoals(terrain, world, start, goals, preferred, radius, ignored, goalKey,
                                profile);
    const SpatialShape actorAtPreferred{
        FootprintShape::circle, preferred, radius, glm::vec2{radius}, 0.0F};
    if (!result.empty() && map_.contains(preferred, radius) &&
        terrain.permitsMovementSegment({result.back().x, result.back().z}, preferred,
                                         radius, profile.domains) &&
        (profile.ignoresEntityObstacles ||
         !overlapsObject(world, definitions_, actorAtPreferred, ignored))) {
        result.push_back({preferred.x, 0.0F, preferred.y});
    }
    return result;
}

std::vector<glm::vec3> Navigation::pathFromGoals(
    const Terrain& terrain, const World& world, glm::vec3 start, const std::vector<int>& goals,
    glm::vec2 preferredGoal, float radius, EntityId ignored, std::uint64_t goalKey,
    NavigationProfile profile) {
    (void)ignored;
    const auto& cached = occupancy(terrain, world, radius, profile);
    std::vector<std::uint8_t> passable = cached;
    const int sx = gridCoordinate(start.x), sz = gridCoordinate(start.z), source = indexOf(sx, sz);
    if (profile.avoidMobileUnits && !profile.ignoresEntityObstacles)
        for (const auto& other : world.entities()) {
            if (other.id == ignored || other.flight || !other.unitControl) continue;
            const auto shape = spatialShape(definitions_, other);
            const auto bounds = axisAlignedHalfExtents(expanded(shape, radius + .2F));
            const int minX = gridCoordinate(shape.center.x - bounds.x), maxX = gridCoordinate(shape.center.x + bounds.x);
            const int minZ = gridCoordinate(shape.center.y - bounds.y), maxZ = gridCoordinate(shape.center.y + bounds.y);
            for (int z = minZ; z <= maxZ; ++z)
                for (int x = minX; x <= maxX; ++x) {
                    const auto p = positionOf(x, z);
                    if (overlaps(SpatialShape{FootprintShape::circle, {p.x, p.z}, radius + .2F}, shape))
                        passable[indexOf(x, z)] = 0;
                }
        }
    passable[source] = 1;
    mix(goalKey, static_cast<std::uint64_t>(static_cast<int>(std::round(radius * 10.0F))));
    mix(goalKey, profile.ignoresEntityObstacles ? 1U : 0U);
    mix(goalKey, profile.domains);
    const bool v2 = terrain.v2Landscape().has_value();
    const auto edgeCost = [&](int from, int to, bool diagonal) {
        const float rise = v2 ? 0.F : std::abs(heights_[from] - heights_[to]) * Terrain::gameplaySlopeFactor;
        const float traversal = (movementCost(from, profile.domains) + movementCost(to, profile.domains)) * .5F;
        return (diagonal ? 1.4142F : 1.F) * traversal + rise * .15F;
    };
    std::unordered_map<std::uint64_t, std::vector<float>> trafficFlow;
    auto& fields = profile.avoidMobileUnits ? trafficFlow : flowFields_;
    auto flow = fields.find(goalKey);
    if (flow == fields.end()) {
        if (fields.size() >= 64) fields.clear();
        struct Open { float cost; int node; bool operator<(const Open& other) const {
            return cost == other.cost ? node > other.node : cost > other.cost; } };
        std::priority_queue<Open> open;
        std::vector<float> costs(static_cast<std::size_t>(side_ * side_),
                                 std::numeric_limits<float>::max());
        for (const int goal : goals) {
            if (!passable[goal]) continue;
            const glm::vec3 position = positionOf(goal % side_, goal / side_);
            const float preference = glm::length(glm::vec2{position.x, position.z} - preferredGoal) /
                                     (map_.extent() + 1.0F);
            costs[goal] = preference;
            open.push({preference, goal});
        }
        while (!open.empty()) {
            const Open item = open.top(); open.pop();
            if (item.cost != costs[item.node]) continue;
            const int x = item.node % side_, z = item.node / side_;
            for (const auto& direction : directions) {
                const int nx = x + direction[0], nz = z + direction[1];
                if (nx < 0 || nz < 0 || nx >= side_ || nz >= side_ || !passable[indexOf(nx, nz)] ||
                    !canStep(terrain, x, z, nx, nz, radius, profile))
                    continue;
                if (direction[0] && direction[1] &&
                    (!passable[indexOf(x + direction[0], z)] || !passable[indexOf(x, z + direction[1])])) continue;
                const int next = indexOf(nx, nz);
                const float nextCost = item.cost + edgeCost(item.node, next, direction[0] && direction[1]);
                if (nextCost < costs[next]) { costs[next] = nextCost; open.push({nextCost, next}); }
            }
        }
        flow = fields.emplace(goalKey, std::move(costs)).first;
    }
    if (!std::isfinite(flow->second[source])) return {};
    if (std::find(goals.begin(), goals.end(), source) != goals.end())
        return {positionOf(sx, sz)};
    std::vector<glm::vec3> path;
    int current = source;
    for (int step = 0; step < side_ * side_; ++step) {
        if (std::find(goals.begin(), goals.end(), current) != goals.end()) break;
        const int x = current % side_, z = current / side_;
        int best = current; float bestCost = flow->second[current];
        float bestTravelCost = std::numeric_limits<float>::max();
        for (const auto& direction : directions) {
            const int nx = x + direction[0], nz = z + direction[1];
            if (nx < 0 || nz < 0 || nx >= side_ || nz >= side_ || !passable[indexOf(nx, nz)] ||
                !canStep(terrain, x, z, nx, nz, radius, profile))
                continue;
            if (direction[0] && direction[1] &&
                (!passable[indexOf(x + direction[0], z)] || !passable[indexOf(x, z + direction[1])])) continue;
            const int candidate = indexOf(nx, nz);
            if (v2) {
                const float travel = flow->second[candidate] + edgeCost(current, candidate, direction[0] && direction[1]);
                if (flow->second[candidate] < flow->second[current] && travel < bestTravelCost) {
                    bestTravelCost = travel;
                    best = candidate;
                }
            } else if (flow->second[candidate] < bestCost) {
                bestCost = flow->second[candidate]; best = candidate;
            }
        }
        if (best == current) return {};
        current = best;
        path.push_back(positionOf(current % side_, current / side_));
    }
    if (!path.empty() && !terrain.permitsMovementSegment(
            {start.x, start.z}, {path.front().x, path.front().z}, radius, profile.domains)) {
        const auto centre = positionOf(sx, sz);
        if (!terrain.permitsMovementSegment(
                {start.x, start.z}, {centre.x, centre.z}, radius, profile.domains))
            return {};
        path.insert(path.begin(), centre);
    }
    return path;
}
} // namespace strategy
