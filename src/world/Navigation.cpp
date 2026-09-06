#include "world/Navigation.hpp"

#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <string_view>

namespace strategy {
namespace {
constexpr float half = Navigation::side_ * Navigation::cell_ * 0.5F;
int indexOf(int x, int z) {
    return z * Navigation::side_ + x;
}
int gridCoordinate(float value) {
    return std::clamp(
        static_cast<int>((value + half) / Navigation::cell_), 0, Navigation::side_ - 1);
}
glm::vec3 positionOf(int x, int z) {
    return {-half + (x + 0.5F) * Navigation::cell_, 0.0F, -half + (z + 0.5F) * Navigation::cell_};
}
void mix(std::uint64_t& hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}
} // namespace

Navigation::Navigation(const Terrain& terrain, const DefinitionRegistry& definitions)
    : definitions_(definitions) {
    rebuildTerrain(terrain);
}

void Navigation::rebuildTerrain(const Terrain& terrain) {
    heights_.resize(side_ * side_);
    terrainPassable_.assign(side_ * side_, 1);
    for (int z = 0; z < side_; ++z)
        for (int x = 0; x < side_; ++x) {
            const glm::vec3 p = positionOf(x, z);
            const float height = terrain.heightAt(p.x, p.z);
            heights_[indexOf(x, z)] = height;
            const float normalized = height / Terrain::heightScale;
            terrainPassable_[indexOf(x, z)] = normalized > 0.12F && normalized < 0.78F;
        }
    obstacleFingerprint_ = 0;
    occupancyByRadius_.clear();
    flowFields_.clear();
}

void Navigation::synchronizeObstacles(const World& world) {
    std::uint64_t fingerprint = 1469598103934665603ULL;
    for (const Entity& entity : world.entities())
        if (!entity.unitControl) {
            mix(fingerprint, entity.id);
            mix(fingerprint, std::bit_cast<std::uint32_t>(entity.transform.position.x));
            mix(fingerprint, std::bit_cast<std::uint32_t>(entity.transform.position.z));
            mix(fingerprint, std::hash<std::string_view>{}(entity.archetype.value));
            mix(fingerprint, !entity.resource || entity.resource.remaining > 0.0F ? 1 : 0);
        }
    if (fingerprint != obstacleFingerprint_) {
        obstacleFingerprint_ = fingerprint;
        occupancyByRadius_.clear();
        flowFields_.clear();
    }
}

const std::vector<std::uint8_t>& Navigation::occupancy(const World& world, float radius) {
    synchronizeObstacles(world);
    const int radiusClass = static_cast<int>(std::round(radius * 10.0F));
    if (const auto found = occupancyByRadius_.find(radiusClass); found != occupancyByRadius_.end())
        return found->second;
    std::vector<std::uint8_t> result = terrainPassable_;
    for (const Entity& entity : world.entities()) {
        if (entity.unitControl || (entity.resource && entity.resource.remaining <= 0.0F))
            continue;
        const float blockedRadius = radius + collisionRadius(definitions_, entity.archetype);
        const int minX = gridCoordinate(entity.transform.position.x - blockedRadius),
                  maxX = gridCoordinate(entity.transform.position.x + blockedRadius),
                  minZ = gridCoordinate(entity.transform.position.z - blockedRadius),
                  maxZ = gridCoordinate(entity.transform.position.z + blockedRadius);
        for (int z = minZ; z <= maxZ; ++z)
            for (int x = minX; x <= maxX; ++x) {
                const glm::vec3 p = positionOf(x, z);
                const float dx = p.x - entity.transform.position.x,
                            dz = p.z - entity.transform.position.z;
                if (dx * dx + dz * dz < blockedRadius * blockedRadius)
                    result[indexOf(x, z)] = 0;
            }
    }
    return occupancyByRadius_.emplace(radiusClass, std::move(result)).first->second;
}

std::vector<glm::vec3> Navigation::findPath(
    const World& world, glm::vec3 start, glm::vec3 destination, float radius, EntityId ignored) {
    (void)ignored;
    const auto& cached = occupancy(world, radius);
    std::vector<std::uint8_t> passable = cached;
    const int sx = gridCoordinate(start.x), sz = gridCoordinate(start.z),
              gx = gridCoordinate(destination.x), gz = gridCoordinate(destination.z),
              source = indexOf(sx, sz);
    passable[source] = 1;
    int target = indexOf(gx, gz);
    if (!passable[target]) {
        float best = std::numeric_limits<float>::max();
        for (int z = 0; z < side_; ++z)
            for (int x = 0; x < side_; ++x)
                if (passable[indexOf(x, z)]) {
                    const float dx = float(x - gx), dz = float(z - gz),
                                distance = dx * dx + dz * dz;
                    if (distance < best) {
                        best = distance;
                        target = indexOf(x, z);
                    }
                }
    }
    constexpr std::array<std::array<int, 2>, 8> directions{
        {{{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}}, {{1, 1}}, {{1, -1}}, {{-1, 1}}, {{-1, -1}}}};
    const int radiusClass = static_cast<int>(std::round(radius * 10.0F));
    const std::uint64_t flowKey =
        (static_cast<std::uint64_t>(radiusClass) << 32) | static_cast<std::uint32_t>(target);
    auto flow = flowFields_.find(flowKey);
    if (flow == flowFields_.end()) {
        if (flowFields_.size() >= 64)
            flowFields_.clear();
        struct Open {
            float cost;
            int node;
            bool operator<(const Open& other) const {
                return cost == other.cost ? node > other.node : cost > other.cost;
            }
        };
        std::priority_queue<Open> open;
        std::vector<float> costs(side_ * side_, std::numeric_limits<float>::max());
        costs[target] = 0.0F;
        open.push({0.0F, target});
        while (!open.empty()) {
            const Open item = open.top();
            open.pop();
            if (item.cost != costs[item.node])
                continue;
            const int x = item.node % side_, z = item.node / side_;
            for (const auto& direction : directions) {
                const int nx = x + direction[0], nz = z + direction[1];
                if (nx < 0 || nz < 0 || nx >= side_ || nz >= side_ || !passable[indexOf(nx, nz)])
                    continue;
                if (direction[0] && direction[1] &&
                    (!passable[indexOf(x + direction[0], z)] ||
                     !passable[indexOf(x, z + direction[1])]))
                    continue;
                const int next = indexOf(nx, nz);
                const float rise = std::abs(heights_[item.node] - heights_[next]);
                if (rise > 1.5F)
                    continue;
                const float nextCost =
                    item.cost + (direction[0] && direction[1] ? 1.4142F : 1.0F) + rise * 0.15F;
                if (nextCost < costs[next]) {
                    costs[next] = nextCost;
                    open.push({nextCost, next});
                }
            }
        }
        flow = flowFields_.emplace(flowKey, std::move(costs)).first;
    }
    if (!std::isfinite(flow->second[source]))
        return {};
    std::vector<glm::vec3> path;
    int current = source;
    for (int step = 0; step < side_ * side_ && current != target; ++step) {
        const int x = current % side_, z = current / side_;
        int best = current;
        float bestCost = flow->second[current];
        for (const auto& direction : directions) {
            const int nx = x + direction[0], nz = z + direction[1];
            if (nx < 0 || nz < 0 || nx >= side_ || nz >= side_ || !passable[indexOf(nx, nz)])
                continue;
            if (direction[0] && direction[1] &&
                (!passable[indexOf(x + direction[0], z)] ||
                 !passable[indexOf(x, z + direction[1])]))
                continue;
            const int candidate = indexOf(nx, nz);
            if (flow->second[candidate] < bestCost) {
                bestCost = flow->second[candidate];
                best = candidate;
            }
        }
        if (best == current)
            return {};
        current = best;
        path.push_back(positionOf(current % side_, current / side_));
    }
    if (!path.empty())
        path.back() = destination;
    return path;
}
} // namespace strategy
