#pragma once
#include "terrain/Terrain.hpp"
namespace strategy {
// Clips final surface triangles to the footprint. Circular areas use analytic disk intersections.
class TerrainSupport final {
  public:
    static TerrainPlacementResult evaluate(const Terrain& terrain,
                                           glm::vec2 center,
                                           const TerrainFootprint& footprint,
                                           TerrainPlacementProfile profile);
};
} // namespace strategy
