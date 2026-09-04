#pragma once
#include <cstdint>
namespace strategy { class Terrain; class World;
void populateResources(World& world, const Terrain& terrain, std::uint32_t terrainSeed);
}
