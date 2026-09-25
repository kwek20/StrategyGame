#pragma once

#include "terrain/TerrainGeneration.hpp"

#include <span>

namespace strategy {

// Biome selection is intentionally separate from height and water generation. A layout may
// enable a subset of biome generators, and custom maps can feed authored regional fields into
// the same classifier.
class TerrainBiomeGenerator final {
  public:
    TerrainBiomeGenerator(const TerrainGenerationDefinitions& definitions,
                          std::span<const TerrainBiomeId> enabledBiomes = {});

    [[nodiscard]] const TerrainBiomeDefinition& classify(
        const TerrainRegionalFields& fields, float normalizedHeight, float slopeDegrees) const;

  private:
    const TerrainGenerationDefinitions* definitions_{nullptr};
    std::span<const TerrainBiomeId> enabledBiomes_;

    [[nodiscard]] bool enabled(const TerrainBiomeDefinition& biome) const;
};

} // namespace strategy
