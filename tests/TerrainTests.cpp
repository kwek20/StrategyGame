#include "terrain/Terrain.hpp"
#include "terrain/TerrainGeneration.hpp"
#include "world/MapArea.hpp"

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

int main() {
    const strategy::TerrainGenerationDefinitions generation =
        strategy::TerrainGenerationDefinitions::load();
    const strategy::TerrainGeneratorDefinition& generator = generation.activeGenerator();
    const strategy::TerrainFieldGenerator regionalFields{0x5EED1234U, generator};
    const strategy::Terrain terrain;
    const strategy::Terrain sameSeed{0x5EED1234U};
    const strategy::Terrain differentSeed{12345U};
    bool valid = generator.id.value == "continental_v1" && generator.version == 1 &&
                 generation.biomes().size() >= 5 && generation.surfaces().size() >= 5;
    const strategy::TerrainRegionalFields origin = regionalFields.sample(0.0F, 0.0F);
    const strategy::TerrainRegionalFields repeated = regionalFields.sample(0.0F, 0.0F);
    const auto validField = [](float value) { return value >= 0.0F && value <= 1.0F; };
    valid = valid && nearlyEqual(origin.continentalness, repeated.continentalness) &&
            nearlyEqual(origin.erosion, repeated.erosion) &&
            nearlyEqual(origin.peaks, repeated.peaks) && validField(origin.continentalness) &&
            validField(origin.erosion) && validField(origin.peaks) &&
            validField(origin.moisture) && validField(origin.temperature) &&
            validField(origin.detail);
    const strategy::MapArea smallMap{10};
    const strategy::MapArea mediumMap{15};
    const strategy::MapArea largeMap{20};
    valid = valid && nearlyEqual(smallMap.extent(), 240.0F) &&
            nearlyEqual(mediumMap.extent(), 360.0F) &&
            nearlyEqual(largeMap.extent(), 480.0F);
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
    // The generator uses stable configured elevation curves. A seed is no longer stretched to
    // force its observed minimum and maximum to zero and one.
    valid = valid && generatedMinimum >= generator.height.minimumHeight &&
            generatedMaximum <= generator.height.maximumHeight && generatedMinimum > 0.0F &&
            generatedMaximum < 1.0F;

    std::size_t buildableSamples = 0;
    for (int z = 0; z < strategy::Terrain::semanticCellCount; ++z) {
        for (int x = 0; x < strategy::Terrain::semanticCellCount; ++x) {
            const float worldX = -halfExtent +
                                 (static_cast<float>(x) + 0.5F) *
                                     strategy::Terrain::semanticCellSize;
            const float worldZ = -halfExtent +
                                 (static_cast<float>(z) + 0.5F) *
                                     strategy::Terrain::semanticCellSize;
            const strategy::TerrainSample& sample = terrain.sampleAt(worldX, worldZ);
            valid = valid && !sample.biome.empty() && !sample.surface.empty() &&
                    std::isfinite(sample.baseHeight) && std::isfinite(sample.slopeDegrees) &&
                    validField(sample.continentalness) && validField(sample.erosion) &&
                    validField(sample.peaks) && validField(sample.moisture) &&
                    validField(sample.temperature) &&
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
        }
    }
    valid = valid && buildableSamples > 0;

    for (const glm::vec2 point : {glm::vec2{0.0F, 0.0F},
                                  glm::vec2{-64.0F, 23.0F},
                                  glm::vec2{96.0F, -48.0F}}) {
        const strategy::TerrainSample& firstSample = terrain.sampleAt(point.x, point.y);
        const strategy::TerrainSample& repeatedSample = sameSeed.sampleAt(point.x, point.y);
        valid = valid && firstSample.biome == repeatedSample.biome &&
                firstSample.surface == repeatedSample.surface &&
                firstSample.traversal == repeatedSample.traversal &&
                firstSample.buildability == repeatedSample.buildability &&
                nearlyEqual(firstSample.moisture, repeatedSample.moisture);
    }

    for (int z : {0, 64, 256, 511, 512}) {
        for (int x : {0, 64, 256, 511, 512}) {
            const float worldX = static_cast<float>(x) * strategy::Terrain::spacing - halfExtent;
            const float worldZ = static_cast<float>(z) * strategy::Terrain::spacing - halfExtent;
            valid =
                valid &&
                nearlyEqual(terrain.heightAt(worldX, worldZ), terrain.vertexHeight(x, z), 0.002F);
        }
    }

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

    strategy::Terrain flattened{0x5EED1234U};
    const auto before = flattened.fitFootprint(12.0F, -7.0F, 5.0F, 90.0F);
    const strategy::TerrainFoundation foundation{{12.0F, before.height, -7.0F}, 5.0F, 7.0F};
    flattened.applyFoundation(foundation);
    const auto after = flattened.fitFootprint(12.0F, -7.0F, 3.0F, 1.0F);
    valid = valid && after.valid && nearlyEqual(flattened.heightAt(12.0F, -7.0F), before.height);
    valid = valid && nearlyEqual(flattened.heightAt(15.0F, -7.0F), before.height, 0.01F);

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
        std::cerr << "Terrain validation failed\n";
        return 1;
    }
    std::cout << "Terrain validation passed\n";
    return 0;
}
