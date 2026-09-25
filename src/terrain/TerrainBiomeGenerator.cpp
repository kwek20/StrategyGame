#include "terrain/TerrainBiomeGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace strategy {
namespace {

bool inRange(float value, float minimum, float maximum) {
    return value >= minimum && value <= maximum;
}

float rangeSuitability(float value, float minimum, float maximum) {
    const float halfRange = (maximum - minimum) * 0.5F;
    if (halfRange <= 0.00001F) return value == minimum ? 1.0F : 0.0F;
    const float center = (minimum + maximum) * 0.5F;
    return 1.0F - std::clamp(std::abs(value - center) / halfRange, 0.0F, 1.0F);
}

} // namespace

TerrainBiomeGenerator::TerrainBiomeGenerator(
    const TerrainGenerationDefinitions& definitions,
    std::span<const TerrainBiomeId> enabledBiomes)
    : definitions_(&definitions), enabledBiomes_(enabledBiomes) {}

bool TerrainBiomeGenerator::enabled(const TerrainBiomeDefinition& biome) const {
    if (!biome.enabled || biome.generator == "disabled") return false;
    return enabledBiomes_.empty() ||
           std::find(enabledBiomes_.begin(), enabledBiomes_.end(), biome.id) !=
               enabledBiomes_.end();
}

const TerrainBiomeDefinition& TerrainBiomeGenerator::classify(
    const TerrainRegionalFields& fields, float normalizedHeight, float slopeDegrees) const {
    const TerrainBiomeDefinition* selected = nullptr;
    float selectedSuitability = -1.0F;
    for (const TerrainBiomeDefinition& candidate : definitions_->biomes()) {
        if (!enabled(candidate) || candidate.generator != "range" ||
            !inRange(normalizedHeight, candidate.minimumHeight, candidate.maximumHeight) ||
            !inRange(fields.moisture, candidate.minimumMoisture, candidate.maximumMoisture) ||
            !inRange(fields.erosion, candidate.minimumErosion, candidate.maximumErosion) ||
            !inRange(fields.peaks, candidate.minimumPeaks, candidate.maximumPeaks) ||
            !inRange(fields.temperature, candidate.minimumTemperature,
                     candidate.maximumTemperature) ||
            slopeDegrees > candidate.maximumSlopeDegrees)
            continue;
        const float suitability =
            rangeSuitability(normalizedHeight, candidate.minimumHeight,
                             candidate.maximumHeight) * candidate.heightWeight +
            rangeSuitability(fields.moisture, candidate.minimumMoisture,
                             candidate.maximumMoisture) * candidate.moistureWeight +
            rangeSuitability(fields.erosion, candidate.minimumErosion,
                             candidate.maximumErosion) * candidate.erosionWeight +
            rangeSuitability(fields.peaks, candidate.minimumPeaks, candidate.maximumPeaks) *
                candidate.peaksWeight +
            rangeSuitability(fields.temperature, candidate.minimumTemperature,
                             candidate.maximumTemperature) * candidate.temperatureWeight;
        if (!selected || candidate.priority > selected->priority ||
            (candidate.priority == selected->priority && suitability > selectedSuitability) ||
            (candidate.priority == selected->priority && suitability == selectedSuitability &&
             candidate.id.value < selected->id.value)) {
            selected = &candidate;
            selectedSuitability = suitability;
        }
    }
    if (selected) return *selected;
    const auto fallback = std::find_if(
        definitions_->biomes().begin(), definitions_->biomes().end(), [&](const auto& item) {
            return item.id == definitions_->fallbackBiome() && enabled(item);
        });
    if (fallback == definitions_->biomes().end())
        throw std::runtime_error("Terrain biome classification has no enabled fallback");
    return *fallback;
}

} // namespace strategy
