#pragma once

#include <cstdint>

namespace strategy {

class Terrain;
class WorldGenerationProgress;
struct TerrainGeneratorDefinition;

// Owns the optional water-generation stage. Calling clear produces a valid dry terrain state,
// including diagnostic, navigation, and semantic source fields.
class TerrainWaterGenerator final {
  public:
    static void generate(Terrain& terrain, std::uint32_t seed,
                         const TerrainGeneratorDefinition& generator,
                         WorldGenerationProgress* progress = nullptr);
    static void clear(Terrain& terrain, WorldGenerationProgress* progress = nullptr);
};

} // namespace strategy
