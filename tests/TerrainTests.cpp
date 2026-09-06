#include "terrain/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <iostream>

namespace {

bool nearlyEqual(float left, float right, float epsilon = 0.001F) {
    return std::abs(left - right) <= epsilon;
}

} // namespace

int main() {
    const strategy::Terrain terrain;
    const strategy::Terrain sameSeed{0x5EED1234U};
    const strategy::Terrain differentSeed{12345U};
    bool valid = true;
    const float halfExtent = terrain.worldExtent() * 0.5F;

    for (int z = 0; z < strategy::Terrain::vertexCount; ++z) {
        for (int x = 0; x < strategy::Terrain::vertexCount; ++x) {
            const float normalized = terrain.normalizedHeight(x, z);
            valid = valid && normalized >= 0.0F && normalized <= 1.0F;
            valid = valid && nearlyEqual(glm::length(terrain.normalAt(x, z)), 1.0F, 0.002F);
        }
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

    if (!valid) {
        std::cerr << "Terrain validation failed\n";
        return 1;
    }
    std::cout << "Terrain validation passed\n";
    return 0;
}
