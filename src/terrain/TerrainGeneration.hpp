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
struct TerrainBiomeTag;
struct TerrainSurfaceTag;
using TerrainGeneratorId = DefinitionId<TerrainGeneratorTag>;
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
    float minimumHeight{0.02F};
    float maximumHeight{0.95F};
    float warpScale{180.0F};
    float warpStrength{18.0F};
    std::uint32_t smoothingPasses{2};
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
    TerrainBarrierDefinition barriers;
    TerrainConnectivityDefinition connectivity;
    float waterLevel{0.18F};
    std::unordered_map<std::string, TerrainNoiseFieldDefinition> fields;
};

struct TerrainBiomeDefinition {
    TerrainBiomeId id;
    std::int32_t priority{0};
    bool enabled{true};
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
    [[nodiscard]] TerrainBiomeId fallbackBiome() const { return fallbackBiome_; }
    [[nodiscard]] const std::vector<TerrainBiomeDefinition>& biomes() const { return biomes_; }
    [[nodiscard]] const std::vector<TerrainSurfaceDefinition>& surfaces() const {
        return surfaces_;
    }

  private:
    TerrainGeneratorId activeGeneratorId_;
    TerrainBiomeId fallbackBiome_;
    std::unordered_map<std::string, TerrainGeneratorDefinition> generators_;
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
