#pragma once

#include "core/DefinitionId.hpp"
#include "world/MovementDomain.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace strategy {

struct TerrainGeneratorTag;
struct TerrainLayoutTag;
struct TerrainBiomeTag;
struct TerrainSurfaceTag;
using TerrainGeneratorId = DefinitionId<TerrainGeneratorTag>;
using TerrainLayoutId = DefinitionId<TerrainLayoutTag>;
using TerrainBiomeId = DefinitionId<TerrainBiomeTag>;
using TerrainSurfaceId = DefinitionId<TerrainSurfaceTag>;

struct TerrainNoiseFieldDefinition {
    float scale{1.0F};
    std::uint32_t octaves{1};
    float persistence{0.5F};
};

struct TerrainHeightDefinition {
    float baseHeight{0.35F};
    float continentalAmplitude{0.2F};
    float detailAmplitude{0.03F};
    float mountainAmplitude{0.25F};
    float mountainThreshold{0.55F};
    float erosionStrength{0.55F};
    float ridgePower{2.5F};
    float warpScale{180.0F};
    float warpStrength{18.0F};
};

struct TerrainLandformDefinition {
    float plainCompression{0.55F};
    float hillAmplitude{0.07F};
    float mountainBeltStrength{1.25F};
    float outcropAmplitude{0.06F};
    float basinDepth{0.08F};
    float coastalShelfStrength{0.65F};
    float offshoreIslandAmplitude{0.12F};
    float coastalBayDepth{0.05F};
    float chainAngleDegrees{28.0F};
    float chainAnisotropy{3.0F};
    float chainSharpness{1.8F};
    float chainThresholdLow{0.55F};
    float chainThresholdHigh{0.86F};
    float secondaryChainWeight{0.45F};
    float foothillAmplitude{0.06F};
    float plainElevationCompression{0.58F};
    float hillCurvePower{1.25F};
    float mountainCurvePower{1.35F};
    float cliffCurveThreshold{0.72F};
    float cliffCurveStrength{0.05F};
};

struct TerrainPostProcessDefinition {
    std::uint32_t regionalSampleStride{4};
    std::uint32_t plainBlurRadius{7};
    float plainFlattenStrength{0.82F};
    std::uint32_t hillBlurRadius{3};
    float hillSmoothingStrength{0.48F};
    std::uint32_t mountainErosionPasses{2};
    float mountainErosionStrength{0.10F};
    std::uint32_t coastalBlurRadius{4};
    float coastalSmoothingStrength{0.64F};
};

struct TerrainHydrologyDefinition {
    float gridCellSize{2.0F};
    // Upstream catchment area in square world units. Area-based thresholds keep the generated
    // network stable when hydrology resolution changes.
    float riverCatchmentArea{832.0F};
    float wetlandCatchmentArea{544.0F};
    float riverCarveDepth{0.012F};
    float riverHalfWidth{1.8F};
    float riverMeanderStrength{0.75F};
    float riverPathSampleSpacing{0.75F};
    float minimumFirstOrderLength{8.0F};
    std::uint32_t riverSmoothingIterations{2};
    float meanderMaximumSlopeDegrees{8.0F};
    float riverBankFalloff{1.25F};
    float riverFloodplainWidthMultiplier{2.5F};
    float riverFloodplainFlattenStrength{0.35F};
    float riverBankHeight{0.003F};
    float riverMaximumIncision{0.012F};
    float confluenceWidthMultiplier{1.35F};
    float estuaryLength{12.0F};
    float estuaryWidthMultiplier{1.8F};
    std::uint32_t waterEdgeSmoothingRadius{2};
    float minimumRenderedWaterDepth{0.0025F};
    float lakeMinimumDepth{0.008F};
    float lakeMaximumDepth{0.045F};
    float wetlandMaximumSlopeDegrees{7.0F};
};

struct TerrainBarrierDefinition {
    float minimumHeight{0.72F};
    float minimumPeak{0.35F};
    float cliffSlopeDegrees{38.0F};
    float passMinimumErosion{0.68F};
    float passMaximumSlopeDegrees{24.0F};
    float passMovementCost{2.4F};
};

struct TerrainConnectivityDefinition {
    float minimumStartingLandFraction{0.12F};
    std::uint32_t maximumGenerationAttempts{8};
};

struct TerrainGeneratorDefinition {
    TerrainGeneratorId id;
    std::uint32_t version{1};
    TerrainHeightDefinition height;
    TerrainLandformDefinition landforms;
    TerrainPostProcessDefinition postProcessing;
    TerrainHydrologyDefinition hydrology;
    TerrainBarrierDefinition barriers;
    TerrainConnectivityDefinition connectivity;
    float waterLevel{0.18F};
    std::unordered_map<std::string, TerrainNoiseFieldDefinition> fields;
};

enum class TerrainLayoutSource { procedural, customMap };
enum class TerrainLayoutShape { natural, plains, hills, centralHill, centralWater, islands };

// A layout chooses the broad map topology without owning the individual generation stages.
// Procedural layouts reference a generator preset. Authored layouts reference a map manifest;
// both continue through the same water, biome, navigation, resource, and validation pipeline.
struct TerrainLayoutDefinition {
    TerrainLayoutId id;
    std::string nameKey;
    std::uint32_t version{1};
    TerrainLayoutSource source{TerrainLayoutSource::procedural};
    TerrainLayoutShape shape{TerrainLayoutShape::natural};
    TerrainGeneratorId generator;
    std::filesystem::path customMap;
    bool hydrologyEnabled{true};
    float featureStrength{0.20F};
    float featureRadius{0.28F};
    std::uint32_t islandCount{6};
    std::vector<TerrainBiomeId> enabledBiomes;
};

struct TerrainBiomeDefinition {
    TerrainBiomeId id;
    std::int32_t priority{0};
    bool enabled{true};
    // Classifier implementations are independently selectable. "range" is the current
    // suitability classifier; "disabled" preserves a definition without running it.
    std::string generator{"range"};
    float minimumHeight{0.0F};
    float maximumHeight{1.0F};
    float minimumMoisture{0.0F};
    float maximumMoisture{1.0F};
    float minimumErosion{0.0F};
    float maximumErosion{1.0F};
    float minimumPeaks{0.0F};
    float maximumPeaks{1.0F};
    float minimumTemperature{0.0F};
    float maximumTemperature{1.0F};
    float maximumSlopeDegrees{90.0F};
    float heightWeight{1.0F};
    float moistureWeight{0.0F};
    float erosionWeight{0.0F};
    float peaksWeight{0.0F};
    float temperatureWeight{0.0F};
    TerrainSurfaceId surface;
    std::string traversal;
    std::string buildability;
    std::array<float, 3> movementCosts{1.0F, 0.0F, 1.0F};
};

struct TerrainSurfaceDefinition {
    TerrainSurfaceId id;
    std::vector<float> color;
    std::vector<float> materialWeights;
    std::vector<std::string> tags;
};

class TerrainGenerationDefinitions final {
  public:
    static TerrainGenerationDefinitions load(
        const std::filesystem::path& directory = "assets/gameplay/terrain");

    [[nodiscard]] const TerrainGeneratorDefinition& activeGenerator() const;
    [[nodiscard]] const TerrainLayoutDefinition& activeLayout() const;
    [[nodiscard]] const TerrainLayoutDefinition& layout(TerrainLayoutId id) const;
    [[nodiscard]] const TerrainGeneratorDefinition& generator(TerrainGeneratorId id) const;
    [[nodiscard]] const std::vector<TerrainLayoutDefinition>& layouts() const {
        return layoutList_;
    }
    [[nodiscard]] TerrainBiomeId fallbackBiome() const { return fallbackBiome_; }
    [[nodiscard]] const std::vector<TerrainBiomeDefinition>& biomes() const { return biomes_; }
    [[nodiscard]] const std::vector<TerrainSurfaceDefinition>& surfaces() const {
        return surfaces_;
    }

  private:
    TerrainLayoutId activeLayoutId_;
    TerrainGeneratorId activeGeneratorId_;
    TerrainBiomeId fallbackBiome_;
    std::unordered_map<std::string, TerrainGeneratorDefinition> generators_;
    std::unordered_map<std::string, TerrainLayoutDefinition> layouts_;
    std::vector<TerrainLayoutDefinition> layoutList_;
    std::vector<TerrainBiomeDefinition> biomes_;
    std::vector<TerrainSurfaceDefinition> surfaces_;
};

struct TerrainRegionalFields {
    float continentalness{0.0F};
    float erosion{0.0F};
    float peaks{0.0F};
    float moisture{0.0F};
    float temperature{0.0F};
    float detail{0.0F};
    float plains{0.0F};
    float hills{0.0F};
    float mountainBelt{0.0F};
    float rockyOutcrops{0.0F};
    float coastalShelf{0.0F};
    float basin{0.0F};
};

class TerrainFieldGenerator final {
  public:
    TerrainFieldGenerator(std::uint32_t worldSeed,
                          const TerrainGeneratorDefinition& definition);

    [[nodiscard]] TerrainRegionalFields sample(float worldX, float worldZ) const;
    [[nodiscard]] float height(const TerrainRegionalFields& fields) const;
    [[nodiscard]] float heightAt(float worldX, float worldZ) const;

  private:
    struct RuntimeField {
        const TerrainNoiseFieldDefinition* definition{nullptr};
        std::uint32_t seed{0};
    };

    const TerrainGeneratorDefinition* definition_{nullptr};
    RuntimeField continentalness_;
    RuntimeField erosion_;
    RuntimeField peaks_;
    RuntimeField moisture_;
    RuntimeField temperature_;
    RuntimeField detail_;
    RuntimeField plains_;
    RuntimeField hills_;
    RuntimeField mountainBelt_;
    RuntimeField rockyOutcrops_;
    RuntimeField coastalShelf_;
    RuntimeField basin_;
    std::uint32_t warpXSeed_{0};
    std::uint32_t warpZSeed_{0};

    [[nodiscard]] float sampleField(const RuntimeField& field, float worldX, float worldZ) const;
    [[nodiscard]] std::pair<float, float> warpedPosition(float worldX, float worldZ) const;
    [[nodiscard]] static float valueNoise(float x, float z, std::uint32_t seed);
    [[nodiscard]] static float fractalNoise(float x,
                                            float z,
                                            std::uint32_t seed,
                                            std::uint32_t octaves,
                                            float persistence);
    [[nodiscard]] static std::uint32_t namedSeed(std::uint32_t worldSeed,
                                                 const std::string& stream);
};

} // namespace strategy
