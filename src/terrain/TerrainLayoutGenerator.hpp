#pragma once

#include "terrain/TerrainGeneration.hpp"

#include <cstdint>
#include <span>

namespace strategy {

// Applies broad topology after the reusable regional height generator and before hydrology.
// It intentionally does not generate water, biomes, resources, starts, or decoration.
class TerrainLayoutGenerator final {
  public:
    static void apply(std::span<float> heights, int side, std::uint32_t seed,
                      const TerrainLayoutDefinition& layout, float waterLevel);
};

} // namespace strategy
