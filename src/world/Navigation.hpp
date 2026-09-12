#pragma once

#include "world/Entity.hpp"
#include "world/MapArea.hpp"
#include "world/SpatialShape.hpp"

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace strategy {
class DefinitionRegistry;
class Terrain;
class World;

struct NavigationGoalRegion {
    SpatialShape target;
    float interactionRange{0.0F};
    glm::vec2 approachOrigin{0.0F};
    EntityId targetEntity{0};
    EntityId requesterEntity{0};
};

class Navigation final {
  public:
    static constexpr float cellSize = 2.0F;
    Navigation(const Terrain& terrain, const DefinitionRegistry& definitions,
               std::uint32_t mapChunksPerSide);
    void rebuildTerrain(const Terrain& terrain, std::uint32_t mapChunksPerSide);
    [[nodiscard]] std::vector<glm::vec3> findPath(
        const World& world, glm::vec3 start, glm::vec3 destination, float radius, EntityId ignored);
    [[nodiscard]] std::vector<glm::vec3> findPath(
        const World& world, glm::vec3 start, const NavigationGoalRegion& goal,
        float radius, EntityId ignored);
    [[nodiscard]] int cellsPerSide() const { return side_; }
    [[nodiscard]] float worldExtent() const { return map_.extent(); }

  private:
    MapArea map_;
    int side_{1};
    std::vector<float> heights_;
    std::vector<std::uint8_t> terrainPassable_;
    std::unordered_map<int, std::vector<std::uint8_t>> occupancyByRadius_;
    std::unordered_map<std::uint64_t, std::vector<float>> flowFields_;
    std::map<EntityId, SpatialShape> obstacleShapes_;
    const DefinitionRegistry& definitions_;
    [[nodiscard]] int indexOf(int x, int z) const;
    [[nodiscard]] int gridCoordinate(float value) const;
    [[nodiscard]] glm::vec3 positionOf(int x, int z) const;
    void synchronizeObstacles(const World& world);
    const std::vector<std::uint8_t>& occupancy(const World& world, float radius);
    [[nodiscard]] std::vector<glm::vec3> pathFromGoals(
        const World& world, glm::vec3 start, const std::vector<int>& goals,
        glm::vec2 preferredGoal, float radius, EntityId ignored, std::uint64_t goalKey);
};
} // namespace strategy
