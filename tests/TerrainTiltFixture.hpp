#pragma once
#include "TerrainLevelingFixture.hpp"

namespace strategy::test {
inline Terrain
tiltFixture(float rise = .5F, float bumpHeight = 0, bool cliff = false, bool diagonal = false) {
    const auto definitions = TerrainGenerationDefinitions::load();
    auto recipe =
        TerrainV2Recipes::load().resolve(definitions.layout(TerrainLayoutId{"open_plains"}));
    recipe.cells = TerrainCellSettings{};
    recipe.cells.elevationStep = rise;
    recipe.cells.sourceWeight = 100;
    recipe.cells.isolatedWeight = 0;
    recipe.cells.boundaryWeight = 0;
    std::vector<TerrainCellSource> source(1600);
    for (int z = 0; z < 40; ++z)
        for (int x = 0; x < 40; ++x) {
            const float slope = cliff ? (z - 20) * rise : (x - 20) * rise;
            const float h = 16 + slope + (diagonal ? (z - 20) * rise : 0) +
                            (x == (cliff ? 18 : 20) && z == 20 ? bumpHeight : 0) -
                            (cliff && x >= 20 ? 4.0F : 0);
            source[z * 40 + x] = {h, h, h, {rise / 6, 0}, TerrainCellRole::route};
        }
    Terrain terrain{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(
        terrain,
        7,
        definitions.layout(recipe.layout),
        definitions,
        {TerrainV2Plan::create(7, recipe, 240, 2),
         TerrainTerraces::resolve(source, 40, 240, recipe.cells)});
    return terrain;
}
inline TerrainPlacementProfile tiltProfile() {
    auto profile = levelingProfile();
    profile.maximumBuildingTiltDegrees = 5;
    return profile;
}
} // namespace strategy::test
