#pragma once

#include "terrain/Terrain.hpp"

#include <glm/vec3.hpp>
#include <vector>

namespace strategy {
struct ScaffoldVertex {
    glm::vec3 position, normal, color;
    float stage{};
    float waterSurface{}, waterCoverage{};
};
struct ScaffoldGeometry {
    std::vector<ScaffoldVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t columns{}, braces{};
};
// Pure presentation geometry shared by placement previews and finished V2 foundations.
// Progress changes visibility, never these vertices or the terrain beneath them.
ScaffoldGeometry buildScaffoldGeometry(const Terrain& terrain, const TerrainFoundation& foundation);
bool sameScaffoldGeometry(const TerrainFoundation& a, const TerrainFoundation& b);
} // namespace strategy
