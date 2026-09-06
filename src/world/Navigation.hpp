#pragma once
#include "world/Entity.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <unordered_map>
#include <vector>
namespace strategy {
class DefinitionRegistry;
class Terrain;
class World;
class Navigation final {
  public:
    static constexpr int side_ = 96;
    static constexpr float cell_ = 2.0F;
    Navigation(const Terrain& terrain, const DefinitionRegistry& definitions);
    void rebuildTerrain(const Terrain& terrain);
    std::vector<glm::vec3> findPath(
        const World& world, glm::vec3 start, glm::vec3 destination, float radius, EntityId ignored);

  private:
    std::vector<float> heights_;
    std::vector<std::uint8_t> terrainPassable_;
    std::unordered_map<int, std::vector<std::uint8_t>> occupancyByRadius_;
    std::unordered_map<std::uint64_t, std::vector<float>> flowFields_;
    std::uint64_t obstacleFingerprint_{0};
    const DefinitionRegistry& definitions_;
    void synchronizeObstacles(const World& world);
    const std::vector<std::uint8_t>& occupancy(const World& world, float radius);
};
} // namespace strategy
