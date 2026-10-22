#pragma once

#include "terrain/TerrainGeneration.hpp"

#include <filesystem>
#include <map>

namespace strategy {

struct TerrainRange {
    float minimum{};
    float maximum{};
    [[nodiscard]] TerrainRange intersect(TerrainRange other) const;
};

enum class TerrainWaterPolicy { forbidden, optional, required };
enum class TerrainWaterFeature { none, centralLake, ocean };

struct TerrainCellSettings {
    float side{6.0F};
    float elevationStep{2.0F};
    float cliffRun{0.75F};
    float rampRun{6.0F};
    float sourceWeight{1.0F};
    float boundaryWeight{0.12F};
    float isolatedWeight{0.6F};
    std::uint32_t sweeps{8};
    void validate() const;
};

// Resolved once, before sampling terrain. All distances/heights are world units.
struct TerrainV2Recipe {
    TerrainLayoutId layout;
    std::string biomeProfile;
    TerrainLayoutShape shape{TerrainLayoutShape::natural};
    TerrainBiomeId lowlandBiome;
    TerrainBiomeId hillBiome;
    TerrainBiomeId cliffBiome;
    std::uint32_t peakCount{};
    float peakHeight{12.0F};
    TerrainRange ridgeLength;
    TerrainRange ridgeWidth;
    TerrainRange ridgeHeightScale;
    TerrainRange ridgeBend;
    TerrainRange lakeAspect;
    TerrainCellSettings cells;
    TerrainRange relief;
    float baseHeight{16.0F};
    float openingRadius{24.0F};
    float openingSeparationPadding{};
    float openingBlendWidth{12.0F};
    TerrainWaterPolicy waterPolicy{TerrainWaterPolicy::forbidden};
    TerrainWaterFeature waterFeature{TerrainWaterFeature::none};
    TerrainRange waterCoverage;
    float waterLevel{15.0F};
    float lakeDepth{6.0F};
    float bankWidth{12.0F};
    std::uint32_t isletCount{};
    float isletRadius{6.0F};
    float isletHeight{2.0F};
    float forestDensity{1.0F};
    float forestScale{1.0F};
    float forestSpacing{5.5F};
    float forestClusterSize{1.0F};
    float forestCoverage{0.55F};
    float forestDensityScale{180.0F};
    bool competitive{true};
    void validate(float mapExtent, std::size_t players) const;
    // Shared by natural basin shaping and the constructive resource fallback.
    [[nodiscard]] float lakeBedHeight(float shoreDistance) const;
};

class TerrainV2Recipes final {
  public:
    static TerrainV2Recipes
    load(const std::filesystem::path& path = "assets/gameplay/terrain/v2.json");
    [[nodiscard]] TerrainV2Recipe resolve(const TerrainLayoutDefinition& layout) const;

  private:
    std::map<std::string, TerrainV2Recipe> recipes_;
};

} // namespace strategy
