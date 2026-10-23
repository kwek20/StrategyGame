#pragma once
#include "terrain/Terrain.hpp"
#include "terrain/TerrainGenerationPipeline.hpp"
#include "terrain/TerrainV2Recipe.hpp"

namespace strategy::test {
inline Terrain
levelingFixture(float bump = .5F, bool wet = false, bool cliff = false, float cellSide = 6) {
    const auto definitions = TerrainGenerationDefinitions::load();
    auto recipe = TerrainV2Recipes::load().resolve(
        definitions.layout(TerrainLayoutId{wet ? "central_water" : "open_plains"}));
    recipe.cells = TerrainCellSettings{};
    if (wet)
        recipe.waterLevel = 16.1F;
    recipe.cells.elevationStep = .5F;
    recipe.cells.side = cellSide;
    recipe.cells.rampRun = cellSide;
    recipe.cells.sourceWeight = 100;
    recipe.cells.isolatedWeight = 0;
    recipe.cells.boundaryWeight = 0;
    const int count = static_cast<int>(240 / cellSide);
    std::vector<TerrainCellSource> source(count * count);
    for (int z = 0; z < count; ++z)
        for (int x = 0; x < count; ++x) {
            const float h = 16 + (x == count / 2 && z == count / 2 ? bump : 0);
            source[z * count + x] = {
                h, h, h, {0, 0}, cliff ? TerrainCellRole::ordinary : TerrainCellRole::route};
        }
    Terrain terrain{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(
        terrain,
        7,
        definitions.layout(recipe.layout),
        definitions,
        {TerrainV2Plan::create(7, recipe, 240, 2),
         TerrainTerraces::resolve(source, count, 240, recipe.cells)});
    return terrain;
}
inline TerrainPlacementProfile levelingProfile() {
    TerrainPlacementProfile p;
    p.levelingMaximumRelief = .75F;
    p.levelingMaximumCut = .6F;
    p.levelingMaximumAverageCut = .1F;
    p.levelingBlendWidth = 1.5F;
    return p;
}
} // namespace strategy::test
