#pragma once
#include <cstdint>
namespace strategy {
class DefinitionRegistry;
class Terrain;
class World;
void populateResources(World& world,
                       const Terrain& terrain,
                       const DefinitionRegistry& definitions,
                       std::uint32_t terrainSeed,
                       std::uint32_t mapChunksPerSide = 20,
                       float abundanceScale = 1.0F);
} // namespace strategy
