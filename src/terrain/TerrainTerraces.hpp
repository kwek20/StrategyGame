#pragma once
#include "terrain/TerrainV2Plan.hpp"

#include <span>
#include <vector>

namespace strategy {
class WorldGenerationProgress;
enum class TerrainEdgeKind { flat, ramp, cliff };
struct TerrainCellSource {
    float mean{};
    float minimum{};
    float maximum{};
    glm::vec2 gradient{};
    TerrainCellRole role{TerrainCellRole::ordinary};
};
struct TerrainResolvedCell {
    TerrainCellSource source;
    float elevation{};
    TerrainEdgeKind east{TerrainEdgeKind::flat};
    TerrainEdgeKind south{TerrainEdgeKind::flat};
};
struct TerrainResolutionDiagnostics {
    std::uint32_t sweeps{};
    std::uint32_t changedCells{};
    std::uint32_t isolatedBefore{};
    std::uint32_t isolatedAfter{};
    std::uint32_t rampEdges{};
    std::uint32_t cliffEdges{};
    float meanSourceError{};
    float maximumSourceError{};
};
// Coarse elevations and canonical edge ownership. Independent of Terrain, GL and entities.
class TerrainTerraces final {
  public:
    static TerrainTerraces resolve(std::span<const TerrainCellSource> source,
                                   int side,
                                   float extent,
                                   const TerrainCellSettings& settings,
                                   WorldGenerationProgress* progress = nullptr);
    int side() const {
        return side_;
    }
    float extent() const {
        return extent_;
    }
    const std::vector<TerrainResolvedCell>& cells() const {
        return cells_;
    }
    const TerrainResolutionDiagnostics& diagnostics() const {
        return diagnostics_;
    }
    float surfaceAt(glm::vec2 position) const;
    bool crossesCliff(glm::vec2 start, glm::vec2 end) const;
    float maximumCliffDrop(glm::vec2 start, glm::vec2 end) const;

  private:
    int side_{};
    float extent_{};
    TerrainCellSettings settings_;
    std::vector<TerrainResolvedCell> cells_;
    TerrainResolutionDiagnostics diagnostics_;
    std::size_t index(int x, int z) const;
    double localScore(int x, int z, float candidate) const;
    std::uint32_t isolatedCount() const;
    void classifyEdges();
    float cliffDrop(glm::vec2 start, glm::vec2 end, bool firstOnly) const;
};
} // namespace strategy
