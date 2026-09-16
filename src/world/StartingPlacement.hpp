#pragma once

#include "gameplay/DefinitionRegistry.hpp"
#include "world/MapArea.hpp"

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
};

inline std::vector<StartingRegion> selectStartingRegions(
    const Terrain& terrain, const DefinitionRegistry& definitions,
    std::uint32_t mapChunksPerSide, std::size_t playerCount) {
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

    std::vector<StartingRegion> candidates;
    // Excluding chunk 0 and chunk N-1 on both axes is a hard rule, independent of map size.
    for (std::uint32_t chunkZ = 1; chunkZ + 1 < map.chunksPerSide(); ++chunkZ) {
        for (std::uint32_t chunkX = 1; chunkX + 1 < map.chunksPerSide(); ++chunkX) {
            const glm::vec2 anchor{
                -map.halfExtent() + (static_cast<float>(chunkX) + 0.5F) * chunkWidth,
                -map.halfExtent() + (static_cast<float>(chunkZ) + 0.5F) * chunkWidth};
            const TerrainPlacementResult placement = terrain.evaluatePlacement(
                anchor.x, anchor.y, headquartersFootprint,
                {terrainPlacementBit(TerrainPlacementDomain::land), false});
            if (!placement.valid()) continue;
            const int anchorX = std::clamp(
                static_cast<int>((anchor.x + map.halfExtent()) / Terrain::semanticCellSize),
                0, connectivitySide - 1);
            const int anchorZ = std::clamp(
                static_cast<int>((anchor.y + map.halfExtent()) / Terrain::semanticCellSize),
                0, connectivitySide - 1);
            const int landComponent = landComponents[connectivityIndex(anchorX, anchorZ)];
            if (landComponent < 0 ||
                componentSizes[static_cast<std::size_t>(landComponent)] < minimumConnectedLand)
                continue;

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
            if (usableSamples < 24) continue;
            quality += static_cast<float>(usableSamples) * 0.25F;

            std::uint32_t resourceSites = 0;
            for (int direction = 0; direction < 16; ++direction) {
                const float angle = static_cast<float>(direction) * 0.39269908F;
                for (const float distance : {18.0F, 26.0F, 34.0F}) {
                    const glm::vec2 point = anchor +
                        glm::vec2{std::cos(angle), std::sin(angle)} * distance;
                    const TerrainSample& sample = terrain.sampleAt(point.x, point.y);
                    if (map.contains(point, chunkWidth) && !sample.submerged &&
                        sample.buildability != TerrainBuildabilityClass::forbidden &&
                        sample.slopeDegrees <= 18.0F)
                        ++resourceSites;
                }
            }
            if (resourceSites < 8) continue;
            quality += static_cast<float>(resourceSites) * 0.1F;
            candidates.push_back({anchor,
                                  {static_cast<int>(chunkX), static_cast<int>(chunkZ)},
                                  quality, landComponent,
                                  componentSizes[static_cast<std::size_t>(landComponent)]});
        }
    }
    if (candidates.size() < playerCount)
        throw std::runtime_error("Terrain does not contain enough valid interior starting regions");

    const auto betterRegion = [](const StartingRegion& left, const StartingRegion& right) {
        if (left.terrainQuality != right.terrainQuality)
            return left.terrainQuality > right.terrainQuality;
        if (left.chunk.y != right.chunk.y) return left.chunk.y < right.chunk.y;
        return left.chunk.x < right.chunk.x;
    };
    if (playerCount == 1) {
        const auto choice = std::max_element(candidates.begin(), candidates.end(),
            [&](const auto& left, const auto& right) { return betterRegion(right, left); });
        return {*choice};
    }

    std::vector<StartingRegion> selected;
    // Separation is lexicographically primary: terrain quality only breaks equal-distance ties.
    float bestDistance = -1.0F, bestQuality = -1.0F;
    std::pair<std::size_t, std::size_t> bestPair{0, 1};
    for (std::size_t left = 0; left < candidates.size(); ++left)
        for (std::size_t right = left + 1; right < candidates.size(); ++right) {
            if (candidates[left].landComponent != candidates[right].landComponent) continue;
            const glm::vec2 delta = candidates[left].anchor - candidates[right].anchor;
            const float distance = glm::dot(delta, delta);
            const float quality = candidates[left].terrainQuality + candidates[right].terrainQuality;
            if (distance > bestDistance || (distance == bestDistance && quality > bestQuality)) {
                bestDistance = distance;
                bestQuality = quality;
                bestPair = {left, right};
            }
        }
    if (bestDistance < 0.0F)
        throw std::runtime_error(
            "Terrain has no mutually reachable pair of valid starting regions");
    selected.push_back(candidates[bestPair.first]);
    selected.push_back(candidates[bestPair.second]);
    while (selected.size() < playerCount) {
        const StartingRegion* choice = nullptr;
        float choiceDistance = -1.0F;
        for (const StartingRegion& candidate : candidates) {
            if (std::any_of(selected.begin(), selected.end(), [&](const auto& region) {
                    return region.chunk == candidate.chunk;
                }) || candidate.landComponent != selected.front().landComponent) continue;
            float nearest = std::numeric_limits<float>::max();
            for (const StartingRegion& existing : selected) {
                const glm::vec2 delta = candidate.anchor - existing.anchor;
                nearest = std::min(nearest, glm::dot(delta, delta));
            }
            if (!choice || nearest > choiceDistance ||
                (nearest == choiceDistance && betterRegion(candidate, *choice))) {
                choice = &candidate;
                choiceDistance = nearest;
            }
        }
        if (!choice) throw std::runtime_error("Could not separate all player starting regions");
        selected.push_back(*choice);
    }
    return selected;
}

inline glm::vec3 startingEntityPosition(const StartingEntityDefinition& entity,
                                        glm::vec2 anchor) {
    return {anchor.x + entity.position.x, entity.position.y, anchor.y + entity.position.z};
}

} // namespace strategy
