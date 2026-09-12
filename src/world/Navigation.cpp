#include "world/Navigation.hpp"

#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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
                       std::uint32_t mapChunksPerSide)
    : map_(mapChunksPerSide), definitions_(definitions) {
    rebuildTerrain(terrain, mapChunksPerSide);
}

int Navigation::indexOf(int x, int z) const { return z * side_ + x; }
int Navigation::gridCoordinate(float value) const {
    return std::clamp(static_cast<int>((value + map_.halfExtent()) / cellSize), 0, side_ - 1);
}
glm::vec3 Navigation::positionOf(int x, int z) const {
    return {-map_.halfExtent() + (static_cast<float>(x) + 0.5F) * cellSize, 0.0F,
            -map_.halfExtent() + (static_cast<float>(z) + 0.5F) * cellSize};
}

void Navigation::rebuildTerrain(const Terrain& terrain, std::uint32_t mapChunksPerSide) {
    map_ = MapArea{mapChunksPerSide};
    side_ = std::max(1, static_cast<int>(std::ceil(map_.extent() / cellSize)));
    heights_.resize(static_cast<std::size_t>(side_ * side_));
    terrainPassable_.assign(static_cast<std::size_t>(side_ * side_), 1);
    for (int z = 0; z < side_; ++z)
        for (int x = 0; x < side_; ++x) {
            const glm::vec3 position = positionOf(x, z);
            const float height = terrain.heightAt(position.x, position.z);
            heights_[indexOf(x, z)] = height;
            const float normalized = height / Terrain::heightScale;
            terrainPassable_[indexOf(x, z)] = normalized > 0.12F && normalized < 0.78F;
        }
    obstacleShapes_.clear();
    occupancyByRadius_.clear();
    flowFields_.clear();
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

    for (auto& [radiusClass, occupancyGrid] : occupancyByRadius_) {
        const float radius = static_cast<float>(radiusClass) / 10.0F;
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
            if (dirty[index]) occupancyGrid[index] = terrainPassable_[index];
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

const std::vector<std::uint8_t>& Navigation::occupancy(const World& world, float radius) {
    synchronizeObstacles(world);
    const int radiusClass = static_cast<int>(std::round(radius * 10.0F));
    if (const auto found = occupancyByRadius_.find(radiusClass); found != occupancyByRadius_.end())
        return found->second;
    std::vector<std::uint8_t> result = terrainPassable_;
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
    return occupancyByRadius_.emplace(radiusClass, std::move(result)).first->second;
}

std::vector<glm::vec3> Navigation::findPath(
    const World& world, glm::vec3 start, glm::vec3 destination, float radius, EntityId ignored) {
    const int target = indexOf(gridCoordinate(destination.x), gridCoordinate(destination.z));
    auto result = pathFromGoals(world, start, {target}, {destination.x, destination.z}, radius,
                                ignored, static_cast<std::uint64_t>(target));
    if (!result.empty()) result.back() = destination;
    return result;
}

std::vector<glm::vec3> Navigation::findPath(
    const World& world, glm::vec3 start, const NavigationGoalRegion& goal,
    float radius, EntityId ignored) {
    const auto& passable = occupancy(world, radius);
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
    auto result = pathFromGoals(world, start, goals, preferred, radius, ignored, goalKey);
    const SpatialShape actorAtPreferred{
        FootprintShape::circle, preferred, radius, glm::vec2{radius}, 0.0F};
    if (!result.empty() && map_.contains(preferred, radius) &&
        !overlapsObject(world, definitions_, actorAtPreferred, ignored)) {
        result.push_back({preferred.x, 0.0F, preferred.y});
    }
    return result;
}

std::vector<glm::vec3> Navigation::pathFromGoals(
    const World& world, glm::vec3 start, const std::vector<int>& goals,
    glm::vec2 preferredGoal, float radius, EntityId ignored, std::uint64_t goalKey) {
    (void)ignored;
    const auto& cached = occupancy(world, radius);
    std::vector<std::uint8_t> passable = cached;
    const int sx = gridCoordinate(start.x), sz = gridCoordinate(start.z), source = indexOf(sx, sz);
    passable[source] = 1;
    mix(goalKey, static_cast<std::uint64_t>(static_cast<int>(std::round(radius * 10.0F))));
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
                if (rise > 1.5F) continue;
                const float nextCost = item.cost + (direction[0] && direction[1] ? 1.4142F : 1.0F) + rise * 0.15F;
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
