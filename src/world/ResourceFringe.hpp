#pragma once

#include <cstdint>
#include <vector>

namespace strategy {
class World;
class Terrain;
class DefinitionRegistry;
class WorldGenerationProgress;
struct VegetationChunk;

// Presentation only: no additional inventories, colliders, picking targets or nav obstacles.
void appendResourceFringe(const World& world,
                          const Terrain& terrain,
                          const DefinitionRegistry& definitions,
                          std::uint32_t seed,
                          std::uint32_t mapChunks,
                          std::vector<VegetationChunk>& chunks,
                          WorldGenerationProgress* progress);
} // namespace strategy
