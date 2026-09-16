#pragma once

#include "terrain/TerrainGeneration.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>
#include <glm/geometric.hpp>
#include <vector>
#include <utility>

namespace strategy {

enum class FootprintShape { circle, rectangle };
enum class TerrainTraversalClass { open, difficult, impassable };
enum class TerrainBuildabilityClass { buildable, restricted, forbidden };

struct TerrainSample {
    float baseHeight{0.0F};
    float slopeDegrees{0.0F};
    float continentalness{0.0F};
    float erosion{0.0F};
    float peaks{0.0F};
    float moisture{0.0F};
    float temperature{0.0F};
    float waterDepth{0.0F};
    bool submerged{false};
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

class Terrain final {
  public:
    static constexpr int chunkCellCount = 64;
    static constexpr int chunksPerSide = 20;
    static constexpr int cellCount = chunksPerSide * chunkCellCount;
    static constexpr int vertexCount = cellCount + 1;
    static constexpr float spacing = 0.375F;
    static constexpr float heightScale = 16.0F;
    static constexpr float semanticCellSize = 2.0F;
    static constexpr int semanticCellCount =
        static_cast<int>(cellCount * spacing / semanticCellSize);

    explicit Terrain(std::uint32_t seed = 0x5EED1234U);
    Terrain(std::uint32_t seed, const TerrainGeneratorDefinition& generator);

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
    [[nodiscard]] FootprintFit fitFootprint(float worldX,
                                            float worldZ,
                                            float radius,
                                            float maximumSlopeDegrees) const;
    [[nodiscard]] FootprintFit fitFootprint(float worldX, float worldZ,
                                            const TerrainFootprint& footprint) const;
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
    std::vector<float> heights_;
    std::vector<float> baseHeights_;
    std::vector<TerrainSample> semanticSamples_;
    float waterLevel_{0.0F};
    std::vector<std::pair<int, int>> dirtyChunks_;
    std::vector<TerrainFoundation> appliedFoundations_;

    void generate(std::uint32_t seed, const TerrainGeneratorDefinition& generator);
    void generateSemantics(std::uint32_t seed,
                           const TerrainGeneratorDefinition& generator,
                           const TerrainGenerationDefinitions& definitions);
    [[nodiscard]] int semanticIndex(float worldX, float worldZ) const;
};

} // namespace strategy
