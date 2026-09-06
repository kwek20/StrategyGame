#pragma once
#include <cstdint>
namespace strategy {
class DefinitionRegistry;
class Terrain;
class World;
void populateResources(World& world,
                       const Terrain& terrain,
                       const DefinitionRegistry& definitions,
                       std::uint32_t terrainSeed);
} // namespace strategy
