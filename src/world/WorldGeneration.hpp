#pragma once
#include <cstdint>
#include <glm/vec2.hpp>
#include <vector>
namespace strategy {
class DefinitionRegistry;
class Terrain;
class World;
class VegetationField;
class WorldGenerationProgress;
void populateResources(World& world,
                       const Terrain& terrain,
                       const DefinitionRegistry& definitions,
                       std::uint32_t terrainSeed,
                       std::uint32_t mapChunksPerSide = 20,
                       float abundanceScale = 1.0F,
                       const std::vector<glm::vec2>& startingAnchors = {},
                       WorldGenerationProgress* progress = nullptr);
[[nodiscard]] VegetationField generateVegetation(const World& world,
                                                 const Terrain& terrain,
                                                 const DefinitionRegistry& definitions,
                                                 std::uint32_t terrainSeed,
                                                 std::uint32_t mapChunksPerSide = 20,
                                                 WorldGenerationProgress* progress = nullptr);
} // namespace strategy
