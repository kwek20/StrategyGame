#pragma once

#include "terrain/Terrain.hpp"
#include "world/Entity.hpp"

namespace strategy {
enum class PlacementFailure {
    none,
    invalidBuilder,
    invalidRecipe,
    requirements,
    outsideMap,
    collision,
    terrain,
    resources
};
// Transient command feedback, never saved or included in authoritative state checksums.
struct PlacementEvent {
    PlayerId player{};
    std::uint64_t sequence{};
    EntityId building{};
    PlacementFailure failure{PlacementFailure::none};
    TerrainPlacementFailure terrainFailure{TerrainPlacementFailure::none};
    bool accepted() const {
        return failure == PlacementFailure::none;
    }
};
} // namespace strategy
