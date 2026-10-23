#pragma once

#include "simulation/PlacementEvent.hpp"
#include "world/SpatialShape.hpp"

namespace strategy {
// A read-only plan: spending, clearing and terrain edits happen only after acceptance.
struct BuildingPlacementPlan {
    PlacementFailure failure{PlacementFailure::none};
    EntityId blocker{};
    SpatialShape shape;
    TerrainFootprint footprint;
    TerrainPlacementResult terrain;
    TerrainFoundation foundation;
    bool valid() const {
        return failure == PlacementFailure::none;
    }
};
} // namespace strategy
