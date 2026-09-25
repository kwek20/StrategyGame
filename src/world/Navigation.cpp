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
                terrainMovementCosts_[indexOf(x, z)] =
                    terrain.sampleAt(position.x, position.z).movementCosts;
            }
        }
    }
    obstacleShapes_.clear();
    occupancyByProfile_.clear();
    flowFields_.clear();
    if (progress) progress->report(WorldGenerationPhase::navigation, 1.0F);
}

void Navigation::synchronizeObstacles(const World& world) {
    std::map<EntityId, SpatialShape> current;
    for (const Entity& entity : world.entities())
        if (!entity.unitControl && !entity.flight &&
            (!entity.resource || entity.resource.remaining > 0.0F))
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
                occupancyGrid[index] = movementCost(index, key.domains) > 0.0F ? 1 : 0;
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

const std::vector<std::uint8_t>& Navigation::occupancy(const World& world, float radius,
                                                       NavigationProfile profile) {
    synchronizeObstacles(world);
    const OccupancyKey key{static_cast<int>(std::round(radius * 10.0F)), profile.domains,
                           profile.ignoresEntityObstacles};
    if (const auto found = occupancyByProfile_.find(key); found != occupancyByProfile_.end())
        return found->second;
    std::vector<std::uint8_t> result(terrainMovementCosts_.size(), 0);
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = movementCost(index, profile.domains) > 0.0F ? 1 : 0;
    if (profile.ignoresEntityObstacles)
        return occupancyByProfile_.emplace(key, std::move(result)).first->second;
    for (const Entity& entity : world.entities()) {
        if (entity.flight || entity.unitControl ||
            (entity.resource && entity.resource.remaining <= 0.0F)) continue;
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
    const World& world, glm::vec3 start, glm::vec3 destination, float radius, EntityId ignored,
    NavigationProfile profile) {
    const int target = indexOf(gridCoordinate(destination.x), gridCoordinate(destination.z));
    const auto& cached = occupancy(world, radius, profile);
    const int source = indexOf(gridCoordinate(start.x), gridCoordinate(start.z));
    const SpatialShape actorAtDestination{FootprintShape::circle,
                                          {destination.x, destination.z}, radius,
                                          glm::vec2{radius}, 0.0F};
    const bool preciseDestinationValid =
        cached[target] != 0 && map_.contains({destination.x, destination.z}, radius) &&
        (profile.ignoresEntityObstacles ||
         !overlapsObject(world, definitions_, actorAtDestination, ignored));
    auto direct = pathFromGoals(world, start, {target}, {destination.x, destination.z}, radius,
                                ignored, static_cast<std::uint64_t>(target), profile);
    if (!direct.empty() && cached[target] != 0) {
        if (preciseDestinationValid) direct.back() = destination;
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
            if (nx < 0 || nz < 0 || nx >= side_ || nz >= side_) continue;
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
    auto result = pathFromGoals(world, start, {resolved}, {destination.x, destination.z}, radius,
                                ignored, goalKey, profile);
    if (!result.empty() && resolved == target && preciseDestinationValid)
        result.back() = destination;
    return result;
}

std::vector<glm::vec3> Navigation::findPath(
    const World& world, glm::vec3 start, const NavigationGoalRegion& goal,
    float radius, EntityId ignored, NavigationProfile profile) {
    const auto& passable = occupancy(world, radius, profile);
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
    auto result = pathFromGoals(world, start, goals, preferred, radius, ignored, goalKey,
                                profile);
    const SpatialShape actorAtPreferred{
        FootprintShape::circle, preferred, radius, glm::vec2{radius}, 0.0F};
    if (!result.empty() && map_.contains(preferred, radius) &&
        (profile.ignoresEntityObstacles ||
         !overlapsObject(world, definitions_, actorAtPreferred, ignored))) {
        result.push_back({preferred.x, 0.0F, preferred.y});
    }
    return result;
}

std::vector<glm::vec3> Navigation::pathFromGoals(
    const World& world, glm::vec3 start, const std::vector<int>& goals,
    glm::vec2 preferredGoal, float radius, EntityId ignored, std::uint64_t goalKey,
    NavigationProfile profile) {
    (void)ignored;
    const auto& cached = occupancy(world, radius, profile);
    std::vector<std::uint8_t> passable = cached;
    const int sx = gridCoordinate(start.x), sz = gridCoordinate(start.z), source = indexOf(sx, sz);
    passable[source] = 1;
    mix(goalKey, static_cast<std::uint64_t>(static_cast<int>(std::round(radius * 10.0F))));
    mix(goalKey, profile.ignoresEntityObstacles ? 1U : 0U);
    mix(goalKey, profile.domains);
    auto flow = flowFields_.find(goalKey);
    if (flow == flowFields_.end()) {
        if (flowFields_.size() >= 64) flowFields_.clear();
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
                if (nx < 0 || nz < 0 || nx >= side_ || nz >= side_ || !passable[indexOf(nx, nz)]) continue;
                if (direction[0] && direction[1] &&
                    (!passable[indexOf(x + direction[0], z)] || !passable[indexOf(x, z + direction[1])])) continue;
                const int next = indexOf(nx, nz);
                const float rise = std::abs(heights_[item.node] - heights_[next]);
                const float distance = direction[0] && direction[1] ? 1.4142F : 1.0F;
                const float traversalCost =
                    (movementCost(item.node, profile.domains) +
                     movementCost(next, profile.domains)) * 0.5F;
                const float nextCost = item.cost + distance * traversalCost + rise * 0.15F;
                if (nextCost < costs[next]) { costs[next] = nextCost; open.push({nextCost, next}); }
            }
        }
        flow = flowFields_.emplace(goalKey, std::move(costs)).first;
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
        for (const auto& direction : directions) {
            const int nx = x + direction[0], nz = z + direction[1];
            if (nx < 0 || nz < 0 || nx >= side_ || nz >= side_ || !passable[indexOf(nx, nz)]) continue;
            if (direction[0] && direction[1] &&
                (!passable[indexOf(x + direction[0], z)] || !passable[indexOf(x, z + direction[1])])) continue;
            const int candidate = indexOf(nx, nz);
            if (flow->second[candidate] < bestCost) { bestCost = flow->second[candidate]; best = candidate; }
        }
        if (best == current) return {};
        current = best;
        path.push_back(positionOf(current % side_, current / side_));
    }
    return path;
}
} // namespace strategy
