#pragma once

#include "terrain/TerrainGeneration.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>
#include <glm/geometric.hpp>
#include <span>
#include <vector>
#include <utility>
#include <string_view>

namespace strategy {

class WorldGenerationProgress;
class TerrainGenerationPipeline;
class TerrainWaterGenerator;

enum class FootprintShape { circle, rectangle };
enum class TerrainTraversalClass { open, difficult, impassable };
enum class TerrainBuildabilityClass { buildable, restricted, forbidden };
enum class WaterKind : std::uint8_t { none = 0, ocean = 1, river = 2, lake = 3 };
enum class TerrainTag : std::uint32_t {
    land = 1U << 0U,
    water = 1U << 1U,
    shallowWater = 1U << 2U,
    deepWater = 1U << 3U,
    shoreline = 1U << 4U,
    submerged = 1U << 5U,
    dry = 1U << 6U,
    vegetated = 1U << 7U,
    rocky = 1U << 8U,
    buildable = 1U << 9U,
    noBuild = 1U << 10U,
    mountainBarrier = 1U << 11U,
    mountainPass = 1U << 12U,
    universalBarrier = 1U << 13U,
    river = 1U << 14U,
    lake = 1U << 15U,
    wetland = 1U << 16U,
    beach = 1U << 17U,
    rockyCoast = 1U << 18U,
    riverBank = 1U << 19U,
    floodplain = 1U << 20U,
    sediment = 1U << 21U,
    estuary = 1U << 22U,
    confluence = 1U << 23U
};
using TerrainTagMask = std::uint32_t;
constexpr TerrainTagMask terrainTagBit(TerrainTag tag) {
    return static_cast<TerrainTagMask>(tag);
}
[[nodiscard]] TerrainTagMask terrainTagFromName(std::string_view name);
enum class TerrainPlacementDomain : std::uint8_t { land = 0, shallowWater = 1, deepWater = 2 };
using TerrainPlacementDomainMask = std::uint8_t;
constexpr TerrainPlacementDomainMask terrainPlacementBit(TerrainPlacementDomain domain) {
    return static_cast<TerrainPlacementDomainMask>(1U << static_cast<unsigned>(domain));
}
struct TerrainPlacementProfile {
    TerrainPlacementDomainMask domains{terrainPlacementBit(TerrainPlacementDomain::land)};
    bool requiresShore{false};
};
enum class TerrainPlacementFailure { none, excessiveSlope, forbiddenTerrain, shoreRequired };

struct TerrainSample {
    float baseHeight{0.0F};
    float slopeDegrees{0.0F};
    float continentalness{0.0F};
    float erosion{0.0F};
    float peaks{0.0F};
    float moisture{0.0F};
    float temperature{0.0F};
    float plains{0.0F};
    float hills{0.0F};
    float mountainBelt{0.0F};
    float rockyOutcrops{0.0F};
    float coastalShelf{0.0F};
    float basin{0.0F};
    float waterDepth{0.0F};
    float waterSurfaceHeight{0.0F};
    float waterCoverage{0.0F};
    WaterKind waterKind{WaterKind::none};
    float drainage{0.0F};
    float riverDistance{0.0F};
    float riverHalfWidth{0.0F};
    glm::vec2 riverFlowDirection{0.0F};
    std::uint32_t streamOrder{0};
    bool floodplain{false};
    bool riverBank{false};
    bool sediment{false};
    bool estuary{false};
    bool confluence{false};
    bool submerged{false};
    bool river{false};
    bool lake{false};
    bool wetland{false};
    TerrainTagMask tags{0};
    TerrainBiomeId biome;
    TerrainSurfaceId surface;
    glm::vec3 surfaceColor{1.0F};
    glm::vec4 materialWeights{1.0F, 0.0F, 0.0F, 0.0F};
    TerrainTraversalClass traversal{TerrainTraversalClass::open};
    TerrainBuildabilityClass buildability{TerrainBuildabilityClass::buildable};
    std::array<float, 3> movementCosts{1.0F, 0.0F, 1.0F};
};

struct TerrainFootprint {
    FootprintShape shape{FootprintShape::circle};
    float radius{1.0F};
    glm::vec2 halfExtents{1.0F};
    float rotationDegrees{0.0F};
    float edgeFalloff{2.0F};
    float maximumTiltDegrees{5.0F};
};

struct TerrainFoundation {
    std::uint64_t sourceEntity{0};
    FootprintShape shape{FootprintShape::circle};
    glm::vec3 center{0.0F};
    float innerRadius{0.0F};
    float outerRadius{0.0F};
    float edgeFalloff{2.0F};
    // Optional oriented rectangular footprint. Zero extents preserve the legacy circular mask.
    glm::vec2 halfExtents{0.0F};
    float rotationDegrees{0.0F};
    glm::vec2 gradient{0.0F};
    float influence{1.0F};
    float sourceSlopeDegrees{0.0F};
    bool requiresSlab{false};

    TerrainFoundation() = default;
    TerrainFoundation(glm::vec3 centerValue, float inner, float outer)
        : center(centerValue), innerRadius(inner), outerRadius(outer), edgeFalloff(outer - inner) {}
    TerrainFoundation(std::uint64_t source, glm::vec3 centerValue, const TerrainFootprint& footprint,
                      glm::vec2 planeGradient)
        : sourceEntity(source), shape(footprint.shape), center(centerValue), innerRadius(0.0F),
          outerRadius(footprint.shape == FootprintShape::circle ? footprint.radius
                                                               : glm::length(footprint.halfExtents)),
          edgeFalloff(footprint.edgeFalloff),
          halfExtents(footprint.shape == FootprintShape::rectangle ? footprint.halfExtents
                                                                   : glm::vec2{0.0F}),
          rotationDegrees(footprint.rotationDegrees), gradient(planeGradient) {}
};

struct FootprintFit {
    float height{0.0F};
    float slopeDegrees{0.0F};
    glm::vec2 gradient{0.0F};
    bool valid{false};
};

struct TerrainPlacementResult {
    FootprintFit fit;
    TerrainPlacementFailure failure{TerrainPlacementFailure::none};
    [[nodiscard]] bool valid() const { return failure == TerrainPlacementFailure::none; }
};

struct RiverPathPoint {
    glm::vec2 position{0.0F};
    float surfaceHeight{0.0F};
    float normalizedDrainage{0.0F};
    std::uint32_t streamOrder{1};
};

struct RiverPath {
    std::uint32_t id{0};
    std::uint32_t streamOrder{1};
    bool endsAtOcean{false};
    std::vector<RiverPathPoint> points;
};

class Terrain final {
  public:
    // Creates an empty renderer-side shell. It must be replaced by generated terrain
    // before any sampling operation is used.
    struct DeferredInitialization final {};
    static constexpr int chunkCellCount = 64;
    static constexpr int chunksPerSide = 20;
    static constexpr int cellCount = chunksPerSide * chunkCellCount;
    static constexpr int vertexCount = cellCount + 1;
    static constexpr float spacing = 0.375F;
    static constexpr float heightScale = 30.0F;
    static constexpr float semanticCellSize = 2.0F;
    static constexpr int semanticCellCount =
        static_cast<int>(cellCount * spacing / semanticCellSize);

    explicit Terrain(std::uint32_t seed = 0x5EED1234U,
                     WorldGenerationProgress* progress = nullptr);
    explicit Terrain(DeferredInitialization) noexcept {}
    Terrain(std::uint32_t seed, const TerrainGeneratorDefinition& generator,
            WorldGenerationProgress* progress = nullptr);
    Terrain(std::uint32_t seed, const TerrainGeneratorDefinition& generator,
            bool waterEnabled, WorldGenerationProgress* progress = nullptr);
    Terrain(std::uint32_t seed, TerrainLayoutId layout,
            WorldGenerationProgress* progress = nullptr);

    [[nodiscard]] float normalizedHeight(int x, int z) const;
    [[nodiscard]] float vertexHeight(int x, int z) const;
    [[nodiscard]] float heightAt(float worldX, float worldZ) const;
    [[nodiscard]] const TerrainSample& sampleAt(float worldX, float worldZ) const;
    [[nodiscard]] TerrainBiomeId biomeAt(float worldX, float worldZ) const;
    [[nodiscard]] TerrainTraversalClass traversalAt(float worldX, float worldZ) const;
    [[nodiscard]] float movementCostAt(float worldX, float worldZ,
                                       MovementDomainMask domains) const;
    [[nodiscard]] bool isBuildableAt(float worldX, float worldZ) const;
    [[nodiscard]] glm::vec3 normalAt(int x, int z) const;
    [[nodiscard]] glm::vec3 colorAt(float worldX, float worldZ) const;
    [[nodiscard]] glm::vec4 materialWeightsAt(float worldX, float worldZ) const;
    [[nodiscard]] float worldExtent() const;
    [[nodiscard]] float waterLevel() const { return waterLevel_; }
    [[nodiscard]] float waterSurfaceAt(float worldX, float worldZ) const;
    [[nodiscard]] float waterCoverageAt(float worldX, float worldZ) const;
    [[nodiscard]] float generatedWaterSurfaceAt(float worldX, float worldZ) const;
    [[nodiscard]] float generatedWaterCoverageAt(float worldX, float worldZ) const;
    [[nodiscard]] WaterKind waterKindAt(float worldX, float worldZ) const;
    [[nodiscard]] float riverDistanceAt(float worldX, float worldZ) const;
    [[nodiscard]] glm::vec2 riverFlowAt(float worldX, float worldZ) const;
    [[nodiscard]] const std::vector<RiverPath>& riverPaths() const { return riverPaths_; }
    [[nodiscard]] float minimumStartingLandFraction() const {
        return minimumStartingLandFraction_;
    }
    [[nodiscard]] std::uint32_t maximumGenerationAttempts() const {
        return maximumGenerationAttempts_;
    }
    [[nodiscard]] FootprintFit fitFootprint(float worldX,
                                            float worldZ,
                                            float radius,
                                            float maximumSlopeDegrees) const;
    [[nodiscard]] FootprintFit fitFootprint(float worldX, float worldZ,
                                            const TerrainFootprint& footprint) const;
    [[nodiscard]] TerrainPlacementResult evaluatePlacement(
        float worldX, float worldZ, const TerrainFootprint& footprint,
        TerrainPlacementProfile profile) const;
    [[nodiscard]] TerrainFoundation evaluateFoundation(std::uint64_t sourceEntity,
                                                       float worldX, float worldZ,
                                                       const TerrainFootprint& footprint) const;
    [[nodiscard]] static float signedDistanceToFootprint(const TerrainFoundation& foundation,
                                                         glm::vec2 worldPosition);
    void applyFoundation(const TerrainFoundation& foundation);
    void rebuildFoundations(const std::vector<TerrainFoundation>& foundations);
    [[nodiscard]] const std::vector<std::pair<int, int>>& dirtyChunks() const { return dirtyChunks_; }
    void clearDirtyChunks() { dirtyChunks_.clear(); }

  private:
    friend class TerrainGenerationPipeline;
    friend class TerrainWaterGenerator;
    std::vector<float> heights_;
    std::vector<float> baseHeights_;
    std::vector<float> waterSurfaceHeights_;
    std::vector<float> waterCoverage_;
    // Snapshot produced by hydrology before the presentation smoothing pass. Kept for
    // diagnostics so generation defects can be distinguished from smoothing defects.
    std::vector<float> generatedWaterSurfaceHeights_;
    std::vector<float> generatedWaterCoverage_;
    std::vector<float> drainage_;
    std::vector<WaterKind> waterKinds_;
    std::vector<std::uint8_t> wetlandFlags_;
    std::vector<RiverPath> riverPaths_;
    std::vector<float> riverDistances_;
    std::vector<float> riverHalfWidths_;
    std::vector<glm::vec2> riverFlowDirections_;
    std::vector<std::uint32_t> riverStreamOrders_;
    std::vector<std::uint8_t> floodplainFlags_;
    std::vector<std::uint8_t> riverBankFlags_;
    std::vector<std::uint8_t> sedimentFlags_;
    std::vector<std::uint8_t> estuaryFlags_;
    std::vector<std::uint8_t> confluenceFlags_;
    std::vector<TerrainSample> semanticSamples_;
    float waterLevel_{0.0F};
    float minimumStartingLandFraction_{0.12F};
    std::uint32_t maximumGenerationAttempts_{8};
    std::vector<std::pair<int, int>> dirtyChunks_;
    std::vector<TerrainFoundation> appliedFoundations_;

    void generateHeightfield(std::uint32_t seed,
                             const TerrainGeneratorDefinition& generator,
                             WorldGenerationProgress* progress);
    void applyHydrology(std::uint32_t seed, const TerrainGeneratorDefinition& generator,
                        WorldGenerationProgress* progress);
    void generateSemantics(std::uint32_t seed,
                           const TerrainGeneratorDefinition& generator,
                           const TerrainGenerationDefinitions& definitions,
                           std::span<const TerrainBiomeId> enabledBiomes,
                           WorldGenerationProgress* progress);
    [[nodiscard]] int semanticIndex(float worldX, float worldZ) const;
};

} // namespace strategy
