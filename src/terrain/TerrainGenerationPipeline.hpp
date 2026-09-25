#pragma once

#include "terrain/TerrainGeneration.hpp"

#include <cstdint>

namespace strategy {

class Terrain;
class WorldGenerationProgress;

// Owns stage order, while layout, hydrology, and biome implementations remain replaceable.
// This is the single entry point for procedural and authored terrain generation.
class TerrainGenerationPipeline final {
  public:
    static void generate(Terrain& terrain, std::uint32_t seed,
                         const TerrainLayoutDefinition& layout,
                         const TerrainGenerationDefinitions& definitions,
                         WorldGenerationProgress* progress = nullptr);
    static void generateProcedural(Terrain& terrain, std::uint32_t seed,
                                   const TerrainGeneratorDefinition& generator,
                                   const TerrainGenerationDefinitions& definitions,
                                   WorldGenerationProgress* progress = nullptr,
                                   bool waterEnabled = true);

  private:
    static void loadCustomHeightfield(Terrain& terrain,
                                      const std::filesystem::path& manifest,
                                      WorldGenerationProgress* progress);
};

} // namespace strategy
