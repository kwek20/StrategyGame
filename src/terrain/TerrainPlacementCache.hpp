#pragma once

#include "terrain/Terrain.hpp"

namespace strategy {
// Cache only geometric analysis. Occupancy, fog, ownership and costs remain live checks.
class TerrainPlacementCache final {
  public:
    const TerrainPlacementResult& evaluate(const Terrain& terrain,
                                           glm::vec2 position,
                                           const TerrainFootprint& footprint,
                                           TerrainPlacementProfile profile) {
        if (!result_ || revision_ != terrain.placementRevision() || position_ != position ||
            footprint_ != footprint || profile_ != profile) {
            result_ = terrain.evaluatePlacement(position.x, position.y, footprint, profile);
            revision_ = terrain.placementRevision();
            position_ = position;
            footprint_ = footprint;
            profile_ = profile;
            ++evaluations_;
        }
        return *result_;
    }
    void clear() {
        result_.reset();
    }
    std::uint64_t evaluations() const {
        return evaluations_;
    }

  private:
    std::optional<TerrainPlacementResult> result_;
    std::uint64_t revision_{}, evaluations_{};
    glm::vec2 position_{};
    TerrainFootprint footprint_;
    TerrainPlacementProfile profile_;
};
} // namespace strategy
