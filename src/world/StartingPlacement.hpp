#pragma once

#include "gameplay/DefinitionRegistry.hpp"
#include "players/Player.hpp"
#include "world/MapArea.hpp"

#include <algorithm>
#include <glm/vec3.hpp>

namespace strategy {

// Starting-entity positions in rules.json are formation offsets. This resolves them around
// opposing map-relative anchors so every supported map size preserves the intended separation.
inline glm::vec3 startingEntityPosition(const StartingEntityDefinition& entity,
                                        PlayerId player,
                                        const MatchRulesDefinition& rules,
                                        std::uint32_t mapChunksPerSide) {
    const MapArea map{mapChunksPerSide};
    const float chunkWidth = static_cast<float>(Terrain::chunkCellCount) * Terrain::spacing;
    const float maximumInset = std::max(0.0F, map.halfExtent() - rules.terrainEdgeMargin);
    const float inset = std::clamp(rules.startingEdgeInsetChunks * chunkWidth,
                                   rules.terrainEdgeMargin, maximumInset);
    const float lateral = std::clamp(rules.startingLateralNormalized, 0.0F, 1.0F);
    const glm::vec2 anchor = map.worldFromNormalized(
        {player == 1 ? inset / map.extent() : 1.0F - inset / map.extent(), lateral});
    return {anchor.x + entity.position.x, entity.position.y, anchor.y + entity.position.z};
}

} // namespace strategy
