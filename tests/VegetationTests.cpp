#include "gameplay/DefinitionRegistry.hpp"
#include "terrain/Terrain.hpp"
#include "world/Vegetation.hpp"
#include "world/World.hpp"
#include "world/WorldGeneration.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <vector>

namespace {
std::vector<glm::vec3> positions(const strategy::VegetationField& vegetation) {
    std::vector<glm::vec3> result;
    for (const auto& chunk : vegetation.chunks())
        for (const auto& instance : chunk.instances)
            result.push_back(instance.transform.position);
    return result;
}
}

int main() {
    const strategy::DefinitionRegistry definitions;
    const strategy::Terrain terrain{0x13572468U};
    strategy::World world;
    strategy::VegetationField first =
        strategy::generateVegetation(world, terrain, definitions, 0x13572468U, 15);
    strategy::VegetationField second =
        strategy::generateVegetation(world, terrain, definitions, 0x13572468U, 15);

    bool valid = first.instanceCount() > 0 && positions(first) == positions(second) &&
                 world.entities().empty();
    std::map<std::string, std::size_t> grassVariants;
    for (const auto& chunk : first.chunks()) for (const auto& entity : chunk.instances) {
        const auto* type = definitions.archetype(entity.archetype);
        valid = valid && type && type->kind == strategy::EntityKind::decoration &&
                type->tags.contains("clear-on-build");
        ++grassVariants[entity.archetype.value];
        const auto& vegetation = definitions.matchRules().vegetation;
        const auto rule = std::find_if(vegetation.begin(), vegetation.end(), [&](const auto& item) {
            return item.archetype == entity.archetype.value;
        });
        const strategy::TerrainSample& terrainSample = terrain.sampleAt(
            entity.transform.position.x, entity.transform.position.z);
        valid = valid && rule != vegetation.end() &&
                !terrainSample.submerged &&
                (terrainSample.tags & strategy::terrainTagBit(strategy::TerrainTag::land)) != 0 &&
                (terrainSample.tags & rule->requiredTerrainTags) == rule->requiredTerrainTags &&
                (terrainSample.tags & rule->forbiddenTerrainTags) == 0 &&
                std::find(rule->allowedBiomes.begin(), rule->allowedBiomes.end(),
                          terrainSample.biome) != rule->allowedBiomes.end() &&
                std::find(rule->allowedSurfaces.begin(), rule->allowedSurfaces.end(),
                          terrainSample.surface) != rule->allowedSurfaces.end() &&
                terrainSample.slopeDegrees <= rule->maximumSlopeDegrees;
    }
    valid = valid && grassVariants["grass_dry"] > 0 &&
            grassVariants["grass_aged"] > 0 &&
            grassVariants["grass_fresh"] > 0 &&
            grassVariants["field_weed"] > 0 &&
            grassVariants["wildflower_clump"] > 0 &&
            grassVariants["wildflower_empodium"] > 0 &&
            grassVariants["wildflower_dandelion"] > 0 &&
            grassVariants["grass_meadow"] > 0 &&
            grassVariants["grass_bermuda"] > 0 &&
            grassVariants["pebble_cluster"] > 0 &&
            grassVariants["pebble_stone"] > 0 &&
            grassVariants["pebble_rock"] > 0;

    const glm::vec3 center = positions(first).front();
    const std::size_t oldCount = first.instanceCount();
    const std::size_t removed = first.clearWithin(
        {strategy::FootprintShape::circle, {center.x, center.z}, 2.0F});
    valid = valid && removed > 0 && first.instanceCount() + removed == oldCount &&
            world.entities().empty();

    if (!valid) {
        std::cerr << "Vegetation generation or clearing test failed\n";
        for (const auto& [id, count] : grassVariants)
            std::cerr << "  " << id << ": " << count << '\n';
    }
    return valid ? 0 : 1;
}
