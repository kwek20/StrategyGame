#pragma once
#include <cstdint>
#include <glm/vec2.hpp>
#include <vector>
namespace strategy {
class DefinitionRegistry;
class Terrain;
class World;
void populateResources(World& world,
                       const Terrain& terrain,
                       const DefinitionRegistry& definitions,
                       std::uint32_t terrainSeed,
                       std::uint32_t mapChunksPerSide = 20,
                       float abundanceScale = 1.0F,
                       const std::vector<glm::vec2>& startingAnchors = {});
void populateVegetation(World& world,
                        const Terrain& terrain,
                        const DefinitionRegistry& definitions,
                        std::uint32_t terrainSeed,
                        std::uint32_t mapChunksPerSide = 20);
} // namespace strategy
