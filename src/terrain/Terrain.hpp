#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/geometric.hpp>
#include <vector>
#include <utility>

namespace strategy {

enum class FootprintShape { circle, rectangle };

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
    static constexpr int cellCount = 512;
    static constexpr int vertexCount = cellCount + 1;
    static constexpr int chunkCellCount = 64;
    static constexpr int chunksPerSide = cellCount / chunkCellCount;
    static constexpr float spacing = 0.375F;
    static constexpr float heightScale = 16.0F;

    explicit Terrain(std::uint32_t seed = 0x5EED1234U);

    [[nodiscard]] float normalizedHeight(int x, int z) const;
    [[nodiscard]] float vertexHeight(int x, int z) const;
    [[nodiscard]] float heightAt(float worldX, float worldZ) const;
    [[nodiscard]] glm::vec3 normalAt(int x, int z) const;
    [[nodiscard]] glm::vec3 colorAt(float normalizedHeight) const;
    [[nodiscard]] float worldExtent() const;
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
    std::vector<std::pair<int, int>> dirtyChunks_;
    std::vector<TerrainFoundation> appliedFoundations_;

    void generate(std::uint32_t seed);
    [[nodiscard]] static float valueNoise(float x, float z, std::uint32_t seed);
    [[nodiscard]] static float
    fractalNoise(float x, float z, std::uint32_t seed, int octaves, float persistence);
};

} // namespace strategy
