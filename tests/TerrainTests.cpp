#include "terrain/Terrain.hpp"
#include "terrain/TerrainGeneration.hpp"
#include "terrain/TerrainLayoutGenerator.hpp"
#include "world/MapArea.hpp"
#include "world/GenerationProgress.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>
#include <iostream>

namespace {

bool nearlyEqual(float left, float right, float epsilon = 0.001F) {
    return std::abs(left - right) <= epsilon;
}

} // namespace

int strategyTestMain() {
    const strategy::TerrainGenerationDefinitions generation =
        strategy::TerrainGenerationDefinitions::load();
    const strategy::TerrainGeneratorDefinition& generator = generation.activeGenerator();
    const strategy::TerrainFieldGenerator regionalFields{0x5EED1234U, generator};
    const strategy::Terrain terrain{0x5EED1234U, generator, true};
    const strategy::Terrain dryTerrain{0x5EED1234U, generator, false};
    strategy::WorldGenerationProgress terrainProgress;
    const strategy::Terrain sameSeed{0x5EED1234U, generator, true, &terrainProgress};
    const strategy::Terrain differentSeed{12345U, generator, true};
    bool valid = generation.activeLayout().id.value == "continental" &&
                 !generation.activeLayout().hydrologyEnabled &&
                 generator.id.value == "continental_v1" && generator.version == 2 &&
                 nearlyEqual(generator.waterLevel, 0.22F) &&
                 generation.biomes().size() >= 5 && generation.surfaces().size() >= 5;
    const float dryHalfExtent = dryTerrain.worldExtent() * 0.5F;
    for (float z = -dryHalfExtent; z <= dryHalfExtent; z += 12.0F)
        for (float x = -dryHalfExtent; x <= dryHalfExtent; x += 12.0F) {
            const strategy::TerrainSample& sample = dryTerrain.sampleAt(x, z);
            valid = valid && sample.waterKind == strategy::WaterKind::none &&
                    sample.waterCoverage == 0.0F && !sample.submerged && !sample.river &&
                    !sample.lake && !sample.wetland;
        }
    std::vector<float> layoutProbe(25, 0.4F);
    strategy::TerrainLayoutDefinition centralHill =
        generation.layout(strategy::TerrainLayoutId{"central_hill"});
    strategy::TerrainLayoutGenerator::apply(layoutProbe, 5, 123U, centralHill,
                                            generator.waterLevel);
    valid = valid && layoutProbe[12] > 0.5F && nearlyEqual(layoutProbe.front(), 0.4F);
    const auto completedTerrainProgress = terrainProgress.snapshot();
    valid = valid && completedTerrainProgress.phase == strategy::WorldGenerationPhase::water &&
            nearlyEqual(completedTerrainProgress.phaseProgress, 1.0F);
    strategy::WorldGenerationProgress cancelledProgress;
    cancelledProgress.requestCancellation();
    bool cancellationObserved = false;
    try {
        const strategy::Terrain cancelledTerrain{123U, &cancelledProgress};
        (void)cancelledTerrain;
    } catch (const strategy::WorldGenerationCancelled&) {
        cancellationObserved = true;
    }
    valid = valid && cancellationObserved;
    const strategy::TerrainRegionalFields origin = regionalFields.sample(0.0F, 0.0F);
    const strategy::TerrainRegionalFields repeated = regionalFields.sample(0.0F, 0.0F);
    const auto validField = [](float value) { return value >= 0.0F && value <= 1.0F; };
    valid = valid && nearlyEqual(origin.continentalness, repeated.continentalness) &&
            nearlyEqual(origin.erosion, repeated.erosion) &&
            nearlyEqual(origin.peaks, repeated.peaks) && validField(origin.continentalness) &&
            validField(origin.erosion) && validField(origin.peaks) &&
            validField(origin.moisture) && validField(origin.temperature) &&
            validField(origin.detail) && validField(origin.plains) &&
            validField(origin.hills) && validField(origin.mountainBelt) &&
            validField(origin.rockyOutcrops) && validField(origin.coastalShelf) &&
            validField(origin.basin);
    const strategy::MapArea smallMap{10};
    const strategy::MapArea mediumMap{15};
    const strategy::MapArea largeMap{20};
    valid = valid && nearlyEqual(smallMap.extent(), 240.0F) &&
            nearlyEqual(mediumMap.extent(), 360.0F) &&
            nearlyEqual(largeMap.extent(), 480.0F);
    valid = valid && smallMap.firstTerrainChunk() == 5 &&
            smallMap.terrainChunkEnd() == 15 &&
            smallMap.intersectingTerrainChunksPerSide() == 10 &&
            mediumMap.firstTerrainChunk() == 2 && mediumMap.terrainChunkEnd() == 18 &&
            mediumMap.intersectingTerrainChunksPerSide() == 16 &&
            largeMap.firstTerrainChunk() == 0 && largeMap.terrainChunkEnd() == 20;
    const glm::vec2 mapPoint{-72.0F, 54.0F};
    valid = valid && glm::length(
        mediumMap.worldFromNormalized(mediumMap.normalized(mapPoint)) - mapPoint) < 0.001F;
    valid = valid && mediumMap.gridCell({0.0F, 0.0F}, 128) == glm::ivec2{64, 64} &&
            mediumMap.contains({179.0F, -179.0F}) &&
            !mediumMap.contains({181.0F, 0.0F});
    const float halfExtent = terrain.worldExtent() * 0.5F;

    float generatedMinimum = 1.0F;
    float generatedMaximum = 0.0F;
    for (int z = 0; z < strategy::Terrain::vertexCount; ++z) {
        for (int x = 0; x < strategy::Terrain::vertexCount; ++x) {
            const float normalized = terrain.normalizedHeight(x, z);
            generatedMinimum = std::min(generatedMinimum, normalized);
            generatedMaximum = std::max(generatedMaximum, normalized);
            valid = valid && normalized >= 0.0F && normalized <= 1.0F;
            valid = valid && nearlyEqual(glm::length(terrain.normalAt(x, z)), 1.0F, 0.002F);
        }
    }
    // The representation is bounded, but authored min/max clipping no longer compresses the
    // natural range. The default seed must exercise lowlands and genuine mountain elevation.
    valid = valid && generatedMinimum >= 0.0F && generatedMaximum <= 1.0F &&
            generatedMinimum < generator.waterLevel && generatedMaximum > 0.72F &&
            generatedMaximum - generatedMinimum > 0.55F;

    std::size_t buildableSamples = 0;
    std::size_t submergedSamples = 0;
    std::size_t shorelineSamples = 0;
    std::size_t mountainBarrierSamples = 0;
    std::size_t mountainPassSamples = 0;
    std::size_t riverSamples = 0;
    std::size_t lakeSamples = 0;
    std::size_t wetlandSamples = 0;
    std::size_t beachSamples = 0;
    std::size_t rockyCoastSamples = 0;
    std::size_t isolatedRiverSamples = 0;
    std::size_t floodplainSamples = 0;
    std::size_t invalidRiverFieldSamples = 0;
    std::size_t riverBankSamples = 0;
    std::size_t confluenceSamples = 0;
    std::size_t estuarySamples = 0;
    std::size_t sedimentSamples = 0;
    bool foundElevatedLake = false;
    std::size_t waterInvariantFailures = 0;
    std::size_t waterSmoothingDifferences = 0;
    for (int z = 0; z < strategy::Terrain::semanticCellCount; ++z) {
        for (int x = 0; x < strategy::Terrain::semanticCellCount; ++x) {
            const float worldX = -halfExtent +
                                 (static_cast<float>(x) + 0.5F) *
                                     strategy::Terrain::semanticCellSize;
            const float worldZ = -halfExtent +
                                 (static_cast<float>(z) + 0.5F) *
                                     strategy::Terrain::semanticCellSize;
            const strategy::TerrainSample& sample = terrain.sampleAt(worldX, worldZ);
            const float generatedCoverage = terrain.generatedWaterCoverageAt(worldX, worldZ);
            const float generatedSurface = terrain.generatedWaterSurfaceAt(worldX, worldZ);
            valid = valid && validField(generatedCoverage) &&
                    std::isfinite(generatedSurface);
            if (!nearlyEqual(generatedCoverage, sample.waterCoverage, 0.0001F) ||
                !nearlyEqual(generatedSurface, sample.waterSurfaceHeight, 0.0001F))
                ++waterSmoothingDifferences;
            const bool waterInvariant = validField(sample.waterCoverage) &&
                nearlyEqual(sample.waterDepth,
                            sample.waterCoverage > 0.0F
                                ? std::max(0.0F, sample.waterSurfaceHeight - sample.baseHeight)
                                : 0.0F) &&
                sample.submerged ==
                    (sample.waterCoverage > 0.01F && sample.waterDepth > 0.0F);
            if (!waterInvariant) ++waterInvariantFailures;
            valid = valid && !sample.biome.empty() && !sample.surface.empty() &&
                    std::isfinite(sample.baseHeight) && std::isfinite(sample.slopeDegrees) &&
                    validField(sample.continentalness) && validField(sample.erosion) &&
                    validField(sample.peaks) && validField(sample.moisture) &&
                    validField(sample.temperature) && validField(sample.plains) &&
                    validField(sample.hills) && validField(sample.mountainBelt) &&
                    validField(sample.rockyOutcrops) && validField(sample.coastalShelf) &&
                    validField(sample.basin) &&
                    waterInvariant &&
                    terrain.biomeAt(worldX, worldZ) == sample.biome &&
                    terrain.traversalAt(worldX, worldZ) == sample.traversal &&
                    terrain.isBuildableAt(worldX, worldZ) ==
                        (sample.buildability ==
                         strategy::TerrainBuildabilityClass::buildable);
            const glm::vec3 blendedColor = terrain.colorAt(worldX, worldZ);
            const glm::vec4 blendedWeights = terrain.materialWeightsAt(worldX, worldZ);
            valid = valid && glm::all(glm::greaterThanEqual(blendedColor, glm::vec3{0.0F})) &&
                    glm::all(glm::lessThanEqual(blendedColor, glm::vec3{1.0F})) &&
                    nearlyEqual(blendedWeights.x + blendedWeights.y + blendedWeights.z +
                                    blendedWeights.w,
                                1.0F,
                                0.002F);
            if (sample.buildability == strategy::TerrainBuildabilityClass::buildable)
                ++buildableSamples;
            if (sample.submerged) {
                ++submergedSamples;
                valid = valid && (sample.biome.value == "deep_water" ||
                                  sample.biome.value == "shallow_water") &&
                        (sample.tags & strategy::terrainTagBit(strategy::TerrainTag::water)) != 0 &&
                        (sample.tags & strategy::terrainTagBit(strategy::TerrainTag::submerged)) != 0 &&
                        (sample.tags & strategy::terrainTagBit(strategy::TerrainTag::land)) == 0;
            } else {
                valid = valid &&
                        (sample.tags & strategy::terrainTagBit(strategy::TerrainTag::land)) != 0 &&
                        (sample.tags & strategy::terrainTagBit(strategy::TerrainTag::water)) == 0;
            }
            if ((sample.tags & strategy::terrainTagBit(strategy::TerrainTag::shoreline)) != 0)
                ++shorelineSamples;
            if (sample.river) {
                ++riverSamples;
                const bool validRiverField = sample.riverHalfWidth > 0.0F &&
                    sample.riverDistance <= 0.01F && sample.streamOrder >= 1 &&
                    glm::length(sample.riverFlowDirection) > 0.9F &&
                    sample.waterSurfaceHeight > sample.baseHeight;
                if (!validRiverField) ++invalidRiverFieldSamples;
                valid = valid && validRiverField;
            }
            if (sample.floodplain) ++floodplainSamples;
            if (sample.riverBank) {
                ++riverBankSamples;
                valid = valid && sample.materialWeights.y > sample.materialWeights.x &&
                        (sample.tags & strategy::terrainTagBit(
                             strategy::TerrainTag::riverBank)) != 0;
            }
            if (sample.confluence) ++confluenceSamples;
            if (sample.estuary) ++estuarySamples;
            if (sample.sediment) ++sedimentSamples;
            if (sample.lake) {
                ++lakeSamples;
                foundElevatedLake = foundElevatedLake ||
                                    sample.waterSurfaceHeight > terrain.waterLevel() + 0.05F;
            }
            if (sample.wetland) ++wetlandSamples;
            if ((sample.tags & strategy::terrainTagBit(strategy::TerrainTag::beach)) != 0)
                ++beachSamples;
            if ((sample.tags & strategy::terrainTagBit(strategy::TerrainTag::rockyCoast)) != 0)
                ++rockyCoastSamples;
            if ((sample.tags & strategy::terrainTagBit(strategy::TerrainTag::mountainBarrier)) != 0) {
                ++mountainBarrierSamples;
                valid = valid && !sample.submerged &&
                        sample.traversal == strategy::TerrainTraversalClass::impassable &&
                        sample.movementCosts[static_cast<std::size_t>(
                            strategy::MovementDomain::land)] == 0.0F &&
                        sample.movementCosts[static_cast<std::size_t>(
                            strategy::MovementDomain::air)] > 0.0F;
            }
            if ((sample.tags & strategy::terrainTagBit(strategy::TerrainTag::mountainPass)) != 0) {
                ++mountainPassSamples;
                valid = valid && !sample.submerged &&
                        sample.traversal == strategy::TerrainTraversalClass::difficult &&
                        sample.movementCosts[static_cast<std::size_t>(
                            strategy::MovementDomain::land)] > 1.0F;
            }
        }
    }
    valid = valid && buildableSamples > 0 && submergedSamples > 0 && shorelineSamples > 0 &&
            mountainBarrierSamples > 0 && mountainPassSamples > 0 && riverSamples > 0 &&
            lakeSamples > 0 && wetlandSamples > 0 && beachSamples > 0 &&
            rockyCoastSamples > 0 && foundElevatedLake && floodplainSamples > 0 &&
            riverBankSamples > 0 && confluenceSamples > 0 && estuarySamples > 0 &&
            sedimentSamples > 0;
    valid = valid && waterSmoothingDifferences > 0;
    const bool validAfterSemantics = valid;
    for (int z = 0; z < strategy::Terrain::semanticCellCount; ++z) {
        for (int x = 0; x < strategy::Terrain::semanticCellCount; ++x) {
            const float worldX = -halfExtent + (static_cast<float>(x) + 0.5F) *
                                                   strategy::Terrain::semanticCellSize;
            const float worldZ = -halfExtent + (static_cast<float>(z) + 0.5F) *
                                                   strategy::Terrain::semanticCellSize;
            if (!terrain.sampleAt(worldX, worldZ).river) continue;
            bool connected = false;
            for (int dz = -1; dz <= 1 && !connected; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx == 0 && dz == 0) || x + dx < 0 || z + dz < 0 ||
                        x + dx >= strategy::Terrain::semanticCellCount ||
                        z + dz >= strategy::Terrain::semanticCellCount)
                        continue;
                    const float neighborX = worldX + dx * strategy::Terrain::semanticCellSize;
                    const float neighborZ = worldZ + dz * strategy::Terrain::semanticCellSize;
                    const auto& neighbor = terrain.sampleAt(neighborX, neighborZ);
                    connected = neighbor.river || neighbor.lake ||
                                (neighbor.submerged && !neighbor.river);
                    if (connected) break;
                }
            if (!connected) ++isolatedRiverSamples;
        }
    }
    valid = valid && isolatedRiverSamples == 0;

    std::uint32_t maximumStreamOrder = 0;
    std::uint32_t expectedPathId = 1;
    std::size_t invalidRiverPathSegments = 0;
    std::size_t oceanOutletPaths = 0;
    for (const strategy::RiverPath& path : terrain.riverPaths()) {
        valid = valid && path.id == expectedPathId++ && path.points.size() >= 2 &&
                path.streamOrder >= 1;
        maximumStreamOrder = std::max(maximumStreamOrder, path.streamOrder);
        if (path.endsAtOcean) ++oceanOutletPaths;
        for (std::size_t index = 1; index < path.points.size(); ++index) {
            const auto& previous = path.points[index - 1];
            const auto& current = path.points[index];
            const bool validSegment =
                current.surfaceHeight <= previous.surfaceHeight + 0.001F &&
                current.streamOrder >= previous.streamOrder &&
                glm::distance(previous.position, current.position) <=
                    generator.hydrology.riverPathSampleSpacing * 1.15F;
            if (!validSegment) ++invalidRiverPathSegments;
            valid = valid && validSegment;
        }
    }
    valid = valid && !terrain.riverPaths().empty() && maximumStreamOrder >= 2 &&
            oceanOutletPaths > 0;
    const bool validAfterRiverPaths = valid;

    bool checkedWaterPlacement = false;
    for (int z = 0; z < strategy::Terrain::semanticCellCount && !checkedWaterPlacement; ++z)
        for (int x = 0; x < strategy::Terrain::semanticCellCount; ++x) {
            const float worldX = -halfExtent + (static_cast<float>(x) + 0.5F) *
                                                   strategy::Terrain::semanticCellSize;
            const float worldZ = -halfExtent + (static_cast<float>(z) + 0.5F) *
                                                   strategy::Terrain::semanticCellSize;
            const auto& sample = terrain.sampleAt(worldX, worldZ);
            if (!sample.submerged) continue;
            strategy::TerrainFootprint footprint;
            footprint.shape = strategy::FootprintShape::circle;
            footprint.radius = 0.35F;
            footprint.halfExtents = {0.35F, 0.35F};
            footprint.maximumTiltDegrees = 90.0F;
            const auto land = terrain.evaluatePlacement(
                worldX, worldZ, footprint,
                {strategy::terrainPlacementBit(strategy::TerrainPlacementDomain::land), false});
            const strategy::TerrainPlacementDomain waterDomain =
                sample.biome.value == "deep_water"
                    ? strategy::TerrainPlacementDomain::deepWater
                    : strategy::TerrainPlacementDomain::shallowWater;
            const auto water = terrain.evaluatePlacement(
                worldX, worldZ, footprint,
                {strategy::terrainPlacementBit(waterDomain), false});
            const auto shore = terrain.evaluatePlacement(
                worldX, worldZ, footprint,
                {static_cast<strategy::TerrainPlacementDomainMask>(
                     strategy::terrainPlacementBit(strategy::TerrainPlacementDomain::land) |
                     strategy::terrainPlacementBit(strategy::TerrainPlacementDomain::shallowWater) |
                     strategy::terrainPlacementBit(strategy::TerrainPlacementDomain::deepWater)),
                 true});
            valid = valid && !land.valid() && water.valid() && !shore.valid() &&
                    shore.failure == strategy::TerrainPlacementFailure::shoreRequired;
            checkedWaterPlacement = true;
            break;
        }
    valid = valid && checkedWaterPlacement;
    const bool validAfterWaterPlacement = valid;

    for (const glm::vec2 point : {glm::vec2{0.0F, 0.0F},
                                  glm::vec2{-64.0F, 23.0F},
                                  glm::vec2{96.0F, -48.0F}}) {
        const strategy::TerrainSample& firstSample = terrain.sampleAt(point.x, point.y);
        const strategy::TerrainSample& repeatedSample = sameSeed.sampleAt(point.x, point.y);
        valid = valid && firstSample.biome == repeatedSample.biome &&
                firstSample.surface == repeatedSample.surface &&
                firstSample.traversal == repeatedSample.traversal &&
                firstSample.buildability == repeatedSample.buildability &&
                nearlyEqual(firstSample.moisture, repeatedSample.moisture) &&
                nearlyEqual(firstSample.waterSurfaceHeight,
                            repeatedSample.waterSurfaceHeight) &&
                nearlyEqual(firstSample.waterCoverage, repeatedSample.waterCoverage) &&
                firstSample.waterKind == repeatedSample.waterKind &&
                nearlyEqual(firstSample.drainage, repeatedSample.drainage) &&
                firstSample.river == repeatedSample.river &&
                firstSample.lake == repeatedSample.lake &&
                firstSample.wetland == repeatedSample.wetland;
    }

    for (int z : {0, 64, 256, 511, 512}) {
        for (int x : {0, 64, 256, 511, 512}) {
            const float worldX = static_cast<float>(x) * strategy::Terrain::spacing - halfExtent;
            const float worldZ = static_cast<float>(z) * strategy::Terrain::spacing - halfExtent;
            valid =
                valid &&
                nearlyEqual(terrain.heightAt(worldX, worldZ), terrain.vertexHeight(x, z), 0.002F) &&
                terrain.waterSurfaceAt(worldX, worldZ) + 0.002F >=
                    terrain.heightAt(worldX, worldZ);
        }
    }
    const bool validAfterDeterminism = valid;

    valid = valid && terrain.heightAt(-halfExtent - 1.0F, 0.0F) == 0.0F;
    valid = valid && terrain.heightAt(halfExtent + 1.0F, 0.0F) == 0.0F;
    valid = valid && strategy::Terrain::cellCount % strategy::Terrain::chunkCellCount == 0;
    bool foundSeedDifference = false;
    for (int coordinate : {17, 64, 127, 201, 239}) {
        valid = valid && nearlyEqual(terrain.normalizedHeight(coordinate, coordinate),
                                     sameSeed.normalizedHeight(coordinate, coordinate));
        foundSeedDifference = foundSeedDifference ||
                              !nearlyEqual(terrain.normalizedHeight(coordinate, coordinate),
                                           differentSeed.normalizedHeight(coordinate, coordinate),
                                           0.00001F);
    }
    valid = valid && foundSeedDifference;
    float maximumNeighborStep = 0.0F;
    for (int z = 0; z < strategy::Terrain::cellCount; ++z) {
        for (int x = 0; x < strategy::Terrain::cellCount; ++x) {
            maximumNeighborStep =
                std::max(maximumNeighborStep,
                         std::abs(terrain.vertexHeight(x + 1, z) - terrain.vertexHeight(x, z)));
            maximumNeighborStep =
                std::max(maximumNeighborStep,
                         std::abs(terrain.vertexHeight(x, z + 1) - terrain.vertexHeight(x, z)));
        }
    }
    valid = valid && maximumNeighborStep < 1.0F;

    glm::vec2 dryPoint{0.0F};
    bool foundDryPoint = false;
    for (int z = 0; z < strategy::Terrain::semanticCellCount && !foundDryPoint; ++z)
        for (int x = 0; x < strategy::Terrain::semanticCellCount; ++x) {
            const float worldX = -halfExtent + (static_cast<float>(x) + 0.5F) *
                                                   strategy::Terrain::semanticCellSize;
            const float worldZ = -halfExtent + (static_cast<float>(z) + 0.5F) *
                                                   strategy::Terrain::semanticCellSize;
            if (terrain.waterCoverageAt(worldX, worldZ) <= 0.001F &&
                terrain.waterCoverageAt(worldX - 7.0F, worldZ) <= 0.001F &&
                terrain.waterCoverageAt(worldX + 7.0F, worldZ) <= 0.001F &&
                terrain.waterCoverageAt(worldX, worldZ - 7.0F) <= 0.001F &&
                terrain.waterCoverageAt(worldX, worldZ + 7.0F) <= 0.001F &&
                terrain.isBuildableAt(worldX, worldZ)) {
                dryPoint = {worldX, worldZ};
                foundDryPoint = true;
                break;
            }
        }
    strategy::Terrain flattened{0x5EED1234U};
    const auto before = flattened.fitFootprint(dryPoint.x, dryPoint.y, 5.0F, 90.0F);
    const strategy::TerrainFoundation foundation{
        {dryPoint.x, before.height, dryPoint.y}, 5.0F, 7.0F};
    flattened.applyFoundation(foundation);
    const auto after = flattened.fitFootprint(dryPoint.x, dryPoint.y, 3.0F, 1.0F);
    valid = valid && foundDryPoint && after.valid &&
            nearlyEqual(flattened.heightAt(dryPoint.x, dryPoint.y), before.height);
    valid = valid && nearlyEqual(flattened.heightAt(dryPoint.x + 3.0F, dryPoint.y),
                                 before.height, 0.01F);

    // Foundation deformation must not manufacture water on dry land. Hydrology remains
    // authoritative while the derived terrain foundation is added and removed.
    strategy::Terrain foundationWaterTest{0x5EED1234U};
    const float originalDryHeight = foundationWaterTest.heightAt(dryPoint.x, dryPoint.y);
    const float originalDrySurface = foundationWaterTest.waterSurfaceAt(dryPoint.x, dryPoint.y);
    const strategy::WaterKind originalDryKind =
        foundationWaterTest.waterKindAt(dryPoint.x, dryPoint.y);
    strategy::TerrainFoundation raisedDryFoundation{
        {dryPoint.x, originalDryHeight + 1.0F, dryPoint.y}, 1.0F, 2.0F};
    foundationWaterTest.applyFoundation(raisedDryFoundation);
    valid = valid && foundDryPoint &&
            foundationWaterTest.waterCoverageAt(dryPoint.x, dryPoint.y) <= 0.001F &&
            foundationWaterTest.waterKindAt(dryPoint.x, dryPoint.y) == originalDryKind &&
            nearlyEqual(foundationWaterTest.waterSurfaceAt(dryPoint.x, dryPoint.y),
                        originalDrySurface);
    foundationWaterTest.rebuildFoundations({});
    valid = valid && nearlyEqual(foundationWaterTest.heightAt(dryPoint.x, dryPoint.y),
                                 originalDryHeight);
    const auto& foundationProfile = foundationWaterTest.foundationDiagnostics();
    valid = valid && foundationProfile.rebuildCount == 1 &&
            foundationProfile.lastResetVertices > 0 &&
            foundationProfile.lastResetVertices <
                static_cast<std::uint64_t>(strategy::Terrain::vertexCount) *
                    strategy::Terrain::vertexCount / 20U &&
            foundationProfile.lastMilliseconds >= 0.0;

    strategy::Terrain boundaryTerrain{0x5EED1234U};
    const float chunkBoundary = -halfExtent + strategy::Terrain::chunkCellCount *
                                                  strategy::Terrain::spacing;
    strategy::TerrainFootprint boundaryFootprint;
    boundaryFootprint.shape = strategy::FootprintShape::rectangle;
    boundaryFootprint.halfExtents = {2.0F, 2.0F};
    boundaryFootprint.edgeFalloff = 1.0F;
    boundaryFootprint.maximumTiltDegrees = 90.0F;
    boundaryTerrain.applyFoundation(boundaryTerrain.evaluateFoundation(
        1, chunkBoundary, chunkBoundary, boundaryFootprint));
    const auto hasDirtyChunk = [&](int x, int z) {
        return std::find(boundaryTerrain.dirtyChunks().begin(),
                         boundaryTerrain.dirtyChunks().end(), std::pair{x, z}) !=
               boundaryTerrain.dirtyChunks().end();
    };
    valid = valid && hasDirtyChunk(0, 0) && hasDirtyChunk(1, 0) &&
            hasDirtyChunk(0, 1) && hasDirtyChunk(1, 1);

    if (!valid) {
        std::cerr << "Terrain validation failed: buildable=" << buildableSamples
                  << " submerged=" << submergedSamples << " waterLevel="
                  << terrain.waterLevel() << " barriers=" << mountainBarrierSamples
                  << " passes=" << mountainPassSamples
                  << " generatedMinimum=" << generatedMinimum
                  << " generatedMaximum=" << generatedMaximum
                  << " rivers=" << riverSamples << " lakes=" << lakeSamples
                  << " wetlands=" << wetlandSamples << " beaches=" << beachSamples
                  << " rockyCoasts=" << rockyCoastSamples
                  << " isolatedRivers=" << isolatedRiverSamples
                  << " elevatedLake=" << foundElevatedLake
                  << " waterInvariantFailures=" << waterInvariantFailures
                  << " floodplains=" << floodplainSamples
                  << " invalidRiverFields=" << invalidRiverFieldSamples
                  << " invalidPathSegments=" << invalidRiverPathSegments
                  << " maxStreamOrder=" << maximumStreamOrder
                  << " banks=" << riverBankSamples
                  << " confluences=" << confluenceSamples
                  << " estuaries=" << estuarySamples
                  << " sediment=" << sedimentSamples
                  << " oceanPaths=" << oceanOutletPaths << '\n';
        std::cerr << "Checkpoints semantics=" << validAfterSemantics
                  << " paths=" << validAfterRiverPaths
                  << " placement=" << validAfterWaterPlacement
                  << " deterministic=" << validAfterDeterminism
                  << " maxNeighborStep=" << maximumNeighborStep
                  << " foundDry=" << foundDryPoint
                  << " foundationValid=" << after.valid << '\n';
        return 1;
    }
    std::cout << "Terrain validation passed\n";
    return 0;
}
