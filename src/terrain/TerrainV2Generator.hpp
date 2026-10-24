#pragma once

#include "terrain/TerrainTerraces.hpp"

namespace strategy {
class WorldGenerationProgress;

struct TerrainV2Landscape {
    TerrainV2Plan plan;
    TerrainTerraces terraces;
};

class TerrainV2Generator final {
  public:
    static constexpr std::uint32_t revision = 8;
    static TerrainV2Landscape generate(std::uint32_t seed,
                                       TerrainV2Recipe recipe,
                                       const TerrainGeneratorDefinition& fields,
                                       float extent,
                                       std::size_t players,
                                       WorldGenerationProgress* progress = nullptr);
};
} // namespace strategy
