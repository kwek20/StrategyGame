#include "terrain/TerrainV2Generator.hpp"

#include "world/GenerationProgress.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <stdexcept>

namespace strategy {
namespace {
float smooth(float value) {
    const float t = std::clamp(value, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

float naturalHeight(glm::vec2 position,
                    const TerrainV2Plan& plan,
                    const TerrainFieldGenerator& fields) {
    const auto& recipe = plan.recipe();
    const auto sample = fields.sample(position.x, position.y);
    // Broad regional fields, rather than fine detail noise, define buildable contours.
    const float regional = 0.65F * sample.continentalness + 0.35F * sample.hills;
    float height = recipe.baseHeight + plan.relief() * (regional - 0.35F);
    if (recipe.shape == TerrainLayoutShape::centralHill) {
        const float radius = glm::length(position) / (plan.extent() * 0.30F);
        height += plan.relief() * (1.0F - smooth(radius));
    }
    const float shoulder = smooth((sample.mountainBelt - 0.55F) / 0.35F);
    height += plan.relief() * shoulder * sample.peaks * (1.0F - plan.routeInfluence(position));
    if (plan.hasWater()) {
        const float distance = plan.waterDistance(position);
        // One continuous local lake level; no global sea flooding or automatic rivers.
        const float bed = recipe.lakeBedHeight(distance);
        height = std::lerp(
            height, bed, 1.0F - smooth(std::max(0.0F, distance) / (recipe.bankWidth * 2.0F)));
        height += plan.isletInfluence(position) * (recipe.lakeDepth + recipe.isletHeight);
    }
    height += plan.peakInfluence(position) * recipe.peakHeight;
    // Reserve broad openings before quantization; this is not runtime terrain deformation.
    const float influence = plan.openingInfluence(position);
    height = std::lerp(height, recipe.baseHeight, influence);
    return height;
}
} // namespace

TerrainV2Landscape TerrainV2Generator::generate(std::uint32_t seed,
                                                TerrainV2Recipe recipe,
                                                const TerrainGeneratorDefinition& definition,
                                                float extent,
                                                std::size_t players,
                                                WorldGenerationProgress* progress) {
    if (progress)
        progress->report(WorldGenerationPhase::terrainFields, 0.0F);
    if (!recipe.competitive)
        throw std::runtime_error("V2 layout requires supported transport planning");
    auto plan = TerrainV2Plan::create(seed, std::move(recipe), extent, players);
    if (plan.hasWater() && plan.recipe().waterFeature == TerrainWaterFeature::ocean)
        throw std::runtime_error("V2 ocean layouts require supported island transport planning");
    const auto& settings = plan.recipe().cells;
    const int side = static_cast<int>(std::lround(extent / settings.side));
    if (std::abs(side * settings.side - extent) > 0.001F)
        throw std::runtime_error("V2 map extent must contain whole terrain cells");
    const TerrainFieldGenerator fields{seed, definition};
    std::vector<TerrainCellSource> source(static_cast<std::size_t>(side) * side);
    // Fine cells already resolve the source at twice the old density. Four area
    // samples suffice there; larger authored cells retain sixteen samples.
    const int samples = settings.side <= 3.0F ? 2 : 4;
    for (int z = 0; z < side; ++z) {
        if (progress)
            progress->report(WorldGenerationPhase::terrainFields,
                             static_cast<float>(z) / side * 0.8F);
        for (int x = 0; x < side; ++x) {
            auto& cell = source[static_cast<std::size_t>(z * side + x)];
            const glm::vec2 center =
                -glm::vec2{extent * 0.5F} + (glm::vec2{x, z} + glm::vec2{0.5F}) * settings.side;
            cell.minimum = std::numeric_limits<float>::max();
            cell.maximum = std::numeric_limits<float>::lowest();
            for (int sz = 0; sz < samples; ++sz) {
                for (int sx = 0; sx < samples; ++sx) {
                    const auto offset =
                        ((glm::vec2{sx, sz} + glm::vec2{0.5F}) / static_cast<float>(samples) -
                         glm::vec2{0.5F}) *
                        settings.side;
                    const float height = naturalHeight(center + offset, plan, fields);
                    cell.mean += height / (samples * samples);
                    cell.minimum = std::min(cell.minimum, height);
                    cell.maximum = std::max(cell.maximum, height);
                }
            }
            const float half = settings.side * 0.5F;
            cell.gradient = {(naturalHeight(center + glm::vec2{half, 0}, plan, fields) -
                              naturalHeight(center - glm::vec2{half, 0}, plan, fields)) /
                                 settings.side,
                             (naturalHeight(center + glm::vec2{0, half}, plan, fields) -
                              naturalHeight(center - glm::vec2{0, half}, plan, fields)) /
                                 settings.side};
            if (plan.openingInfluence(center) == 1.0F)
                cell.role = TerrainCellRole::opening;
            else if (plan.hasWater() &&
                     std::abs(plan.waterDistance(center)) < plan.recipe().bankWidth)
                cell.role = TerrainCellRole::shore;
            else if (plan.waterDistance(center) < 0.0F)
                cell.role = TerrainCellRole::basin;
            else if (plan.reservedRoute(center))
                cell.role = TerrainCellRole::route;
            else if (plan.peakInfluence(center) > 0.2F)
                cell.role = TerrainCellRole::peak;
            else if (glm::length(cell.gradient) > 0.25F)
                cell.role = TerrainCellRole::hill;
        }
    }
    auto terraces = TerrainTerraces::resolve(source, side, extent, settings, progress);
    if (progress)
        progress->report(WorldGenerationPhase::terrainFields, 1.0F);
    return {std::move(plan), std::move(terraces)};
}
} // namespace strategy
