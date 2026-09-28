#pragma once

#include "gameplay/DefinitionRegistry.hpp"
#include "world/MapArea.hpp"
#include "simulation/DeterministicRandom.hpp"
#include "world/GenerationProgress.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <vector>

namespace strategy {

struct StartingRegion {
    glm::vec2 anchor{0.0F};
    glm::ivec2 chunk{0};
    float terrainQuality{0.0F};
    int landComponent{-1};
    std::uint32_t connectedLandCells{0};
    std::uint32_t selectionAttempt{0}; // Diagnostic only; not saved gameplay state.
};

inline std::vector<StartingRegion> selectStartingRegions(
    const Terrain& terrain, const DefinitionRegistry& definitions,
    std::uint32_t mapChunksPerSide, std::size_t playerCount,
    std::uint32_t selectionSeed = 0x5EED1234U,
    WorldGenerationProgress* progress = nullptr) {
    if (playerCount == 0) return {};
    const MatchRulesDefinition& rules = definitions.matchRules();
    const MapArea map{mapChunksPerSide};
    const float chunkWidth = static_cast<float>(Terrain::chunkCellCount) * Terrain::spacing;

    const int connectivitySide = std::max(
        1, static_cast<int>(std::ceil(map.extent() / Terrain::semanticCellSize)));
    const auto connectivityIndex = [connectivitySide](int x, int z) {
        return z * connectivitySide + x;
    };
    const auto connectivityPosition = [&](int x, int z) {
        return glm::vec2{-map.halfExtent() +
                             (static_cast<float>(x) + 0.5F) * Terrain::semanticCellSize,
                         -map.halfExtent() +
                             (static_cast<float>(z) + 0.5F) * Terrain::semanticCellSize};
    };
    std::vector<int> landComponents(
        static_cast<std::size_t>(connectivitySide * connectivitySide), -1);
    std::vector<std::uint32_t> componentSizes;
    constexpr std::array<glm::ivec2, 8> neighbors{
        glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1}, glm::ivec2{0, -1},
        glm::ivec2{1, 1}, glm::ivec2{1, -1}, glm::ivec2{-1, 1}, glm::ivec2{-1, -1}};
    for (int z = 0; z < connectivitySide; ++z)
        for (int x = 0; x < connectivitySide; ++x) {
            const int startIndex = connectivityIndex(x, z);
            const glm::vec2 startPosition = connectivityPosition(x, z);
            if (landComponents[startIndex] >= 0 ||
                terrain.movementCostAt(startPosition.x, startPosition.y,
                                       movementDomainBit(MovementDomain::land)) <= 0.0F)
                continue;
            const int component = static_cast<int>(componentSizes.size());
            componentSizes.push_back(0);
            std::deque<glm::ivec2> open;
            open.push_back({x, z});
            landComponents[startIndex] = component;
            while (!open.empty()) {
                const glm::ivec2 cell = open.front();
                open.pop_front();
                ++componentSizes[static_cast<std::size_t>(component)];
                for (const glm::ivec2 offset : neighbors) {
                    const glm::ivec2 next = cell + offset;
                    if (next.x < 0 || next.y < 0 || next.x >= connectivitySide ||
                        next.y >= connectivitySide)
                        continue;
                    const int nextIndex = connectivityIndex(next.x, next.y);
                    if (landComponents[nextIndex] >= 0) continue;
                    const glm::vec2 nextPosition = connectivityPosition(next.x, next.y);
                    if (terrain.movementCostAt(nextPosition.x, nextPosition.y,
                                               movementDomainBit(MovementDomain::land)) <= 0.0F)
                        continue;
                    if (offset.x != 0 && offset.y != 0) {
                        const glm::vec2 horizontal = connectivityPosition(next.x, cell.y);
                        const glm::vec2 vertical = connectivityPosition(cell.x, next.y);
                        if (terrain.movementCostAt(horizontal.x, horizontal.y,
                                                   movementDomainBit(MovementDomain::land)) <= 0.0F ||
                            terrain.movementCostAt(vertical.x, vertical.y,
                                                   movementDomainBit(MovementDomain::land)) <= 0.0F)
                            continue;
                    }
                    landComponents[nextIndex] = component;
                    open.push_back(next);
                }
            }
        }
    const std::uint32_t minimumConnectedLand = static_cast<std::uint32_t>(std::ceil(
        static_cast<float>(connectivitySide * connectivitySide) *
        terrain.minimumStartingLandFraction()));

    TerrainFootprint headquartersFootprint;
    bool foundHeadquarters = false;
    const auto inspectStarts = [&](const auto& starts) {
        for (const StartingEntityDefinition& start : starts) {
            const EntityArchetype* type = definitions.archetype(start.archetype);
            if (!type || !type->tags.contains("headquarters")) continue;
            const TerrainFootprint candidate = type->footprint.value_or(TerrainFootprint{
                FootprintShape::circle, type->collisionRadius,
                {type->collisionRadius, type->collisionRadius}});
            if (!foundHeadquarters || glm::length(candidate.halfExtents) >
                                          glm::length(headquartersFootprint.halfExtents))
                headquartersFootprint = candidate;
            foundHeadquarters = true;
        }
    };
    inspectStarts(rules.playerOne);
    inspectStarts(rules.playerTwo);
    if (!foundHeadquarters)
        throw std::runtime_error("Starting-region selection requires a headquarters definition");

    if (map.chunksPerSide() < 10 || playerCount > 4)
        throw std::runtime_error("Corner starts require at least 10 chunks per side and at most four players");
    constexpr std::array<glm::ivec2, 4> corners{{{0, 0}, {1, 1}, {1, 0}, {0, 1}}};
    std::vector<StartingRegion> selected;
    for (std::size_t player = 0; player < playerCount; ++player) {
        bool placed = false;
        for (std::uint32_t attempt = 0; attempt < 256 && !placed; ++attempt) {
            DeterministicRandom random(selectionSeed + attempt, "starting.positions");
            const int offsetX = 2 + random.next() % 4;
            const int offsetZ = 2 + random.next() % 4;
            const int chunkX = corners[player].x ? map.chunksPerSide() - 1 - offsetX : offsetX;
            const int chunkZ = corners[player].y ? map.chunksPerSide() - 1 - offsetZ : offsetZ;
            const auto record = [&](const char* result) {
                if (progress) progress->diagnostic("start player=" + std::to_string(player + 1) +
                    " seed=" + std::to_string(selectionSeed + attempt) + " attempt=" + std::to_string(attempt + 1) +
                    " chunk=" + std::to_string(chunkX) + "," + std::to_string(chunkZ) + " " + result);
            };
            const glm::vec2 anchor{
                -map.halfExtent() + (static_cast<float>(chunkX) + 0.5F) * chunkWidth,
                -map.halfExtent() + (static_cast<float>(chunkZ) + 0.5F) * chunkWidth};
            const TerrainPlacementResult placement = terrain.evaluatePlacement(
                anchor.x, anchor.y, headquartersFootprint,
                {terrainPlacementBit(TerrainPlacementDomain::land), false});
            if (!placement.valid()) { record("rejected: headquarters terrain"); continue; }
            const int anchorX = std::clamp(
                static_cast<int>((anchor.x + map.halfExtent()) / Terrain::semanticCellSize),
                0, connectivitySide - 1);
            const int anchorZ = std::clamp(
                static_cast<int>((anchor.y + map.halfExtent()) / Terrain::semanticCellSize),
                0, connectivitySide - 1);
            const int landComponent = landComponents[connectivityIndex(anchorX, anchorZ)];
            if (landComponent < 0 ||
                componentSizes[static_cast<std::size_t>(landComponent)] < minimumConnectedLand)
                { record("rejected: insufficient connected land"); continue; }

            float quality = std::max(0.0F, 10.0F - placement.fit.slopeDegrees);
            std::uint32_t usableSamples = 0;
            for (int dz = -3; dz <= 3; ++dz)
                for (int dx = -3; dx <= 3; ++dx) {
                    const glm::vec2 point = anchor +
                        glm::vec2{static_cast<float>(dx), static_cast<float>(dz)} *
                            (chunkWidth * 0.5F);
                    if (map.contains(point, chunkWidth) &&
                        terrain.movementCostAt(
                            point.x, point.y,
                            movementDomainBit(MovementDomain::land)) > 0.0F)
                        ++usableSamples;
                }
            if (usableSamples < 24) { record("rejected: insufficient surrounding land"); continue; }
            quality += static_cast<float>(usableSamples) * 0.25F;

            if (std::any_of(selected.begin(), selected.end(), [&](const StartingRegion& previous) {
                    return previous.landComponent != landComponent ||
                        glm::distance(previous.anchor, anchor) <
                            map.extent() * rules.minimumOpponentSeparationNormalized;
                })) { record("rejected: opponent separation or connectivity"); continue; }
            record("accepted");
            if (progress) progress->diagnostic("accepted position=" + std::to_string(anchor.x) + "," +
                std::to_string(anchor.y) + " landComponent=" + std::to_string(landComponent));
            selected.push_back({anchor, {chunkX, chunkZ}, quality, landComponent,
                                componentSizes[static_cast<std::size_t>(landComponent)], attempt});
            placed = true;
        }
        if (!placed)
            throw std::runtime_error("Unable to place player " + std::to_string(player + 1) +
                                     " in its assigned corner after 256 attempts (selection seed " +
                                     std::to_string(selectionSeed) + ")");
    }
    return selected;
}

inline glm::vec3 startingEntityPosition(const StartingEntityDefinition& entity,
                                        glm::vec2 anchor) {
    return {anchor.x + entity.position.x, entity.position.y, anchor.y + entity.position.z};
}

} // namespace strategy
