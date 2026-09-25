#include "terrain/TerrainWaterGenerator.hpp"

#include "terrain/Terrain.hpp"
#include "world/GenerationProgress.hpp"

#include <limits>

namespace strategy {

void TerrainWaterGenerator::generate(Terrain& terrain, std::uint32_t seed,
                                     const TerrainGeneratorDefinition& generator,
                                     WorldGenerationProgress* progress) {
    terrain.applyHydrology(seed, generator, progress);
}

void TerrainWaterGenerator::clear(Terrain& terrain, WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::water, 0.0F);
    const std::size_t count = terrain.heights_.size();
    terrain.waterSurfaceHeights_ = terrain.heights_;
    terrain.waterCoverage_.assign(count, 0.0F);
    terrain.generatedWaterSurfaceHeights_ = terrain.heights_;
    terrain.generatedWaterCoverage_.assign(count, 0.0F);
    terrain.drainage_.assign(count, 0.0F);
    terrain.waterKinds_.assign(count, WaterKind::none);
    terrain.wetlandFlags_.assign(count, 0);
    terrain.riverPaths_.clear();
    terrain.riverDistances_.assign(count, std::numeric_limits<float>::infinity());
    terrain.riverHalfWidths_.assign(count, 0.0F);
    terrain.riverFlowDirections_.assign(count, glm::vec2{0.0F});
    terrain.riverStreamOrders_.assign(count, 0);
    terrain.floodplainFlags_.assign(count, 0);
    terrain.riverBankFlags_.assign(count, 0);
    terrain.sedimentFlags_.assign(count, 0);
    terrain.estuaryFlags_.assign(count, 0);
    terrain.confluenceFlags_.assign(count, 0);
    if (progress) progress->report(WorldGenerationPhase::water, 1.0F);
}

} // namespace strategy
