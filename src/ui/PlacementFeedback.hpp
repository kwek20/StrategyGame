#pragma once

#include "simulation/PlacementEvent.hpp"

namespace strategy {
inline const char* placementFailureKey(TerrainPlacementFailure reason) {
    switch (reason) {
    case TerrainPlacementFailure::excessiveSlope:
        return "placement.slope";
    case TerrainPlacementFailure::shoreRequired:
        return "placement.shore";
    case TerrainPlacementFailure::insufficientSupport:
        return "placement.support";
    case TerrainPlacementFailure::excavationBudget:
        return "placement.excavation_budget";
    case TerrainPlacementFailure::foundationTooDeep:
        return "placement.depth";
    default:
        return "placement.terrain";
    }
}
inline const char* placementFailureKey(const PlacementEvent& event) {
    switch (event.failure) {
    case PlacementFailure::terrain:
        return placementFailureKey(event.terrainFailure);
    case PlacementFailure::collision:
        return "placement.collision";
    case PlacementFailure::outsideMap:
        return "placement.outside_map";
    case PlacementFailure::resources:
        return "placement.resources";
    case PlacementFailure::requirements:
        return "placement.requirements";
    case PlacementFailure::invalidBuilder:
        return "placement.builder";
    default:
        return "placement.rejected";
    }
}
} // namespace strategy
