#pragma once

#include "terrain/TerrainTerraces.hpp"

#include <span>

namespace strategy {
class WorldGenerationProgress;

// Finds connections in the sampled landscape. Only blocked elevation transitions
// on the accepted connections are eased; this does not generate a road network.
void connectV2Landscape(TerrainV2Plan& plan,
                        std::span<TerrainCellSource> source,
                        int side,
                        WorldGenerationProgress* progress);
} // namespace strategy
