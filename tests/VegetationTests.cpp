#include "gameplay/DefinitionRegistry.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/World.hpp"
#include "world/WorldGeneration.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <vector>

namespace {
std::vector<glm::vec3> positions(const strategy::World& world) {
    std::vector<glm::vec3> result;
    for (const strategy::Entity& entity : world.entities())
        result.push_back(entity.transform.position);
    return result;
}
}

int main() {
    const strategy::DefinitionRegistry definitions;
    const strategy::Terrain terrain{0x13572468U};
    strategy::World first;
    strategy::World second;
    strategy::populateVegetation(first, terrain, definitions, 0x13572468U, 15);
    strategy::populateVegetation(second, terrain, definitions, 0x13572468U, 15);

    bool valid = !first.entities().empty() && positions(first) == positions(second);
    std::map<std::string, std::size_t> grassVariants;
    for (const strategy::Entity& entity : first.entities()) {
        const auto* type = definitions.archetype(entity.archetype);
        valid = valid && entity.kind == strategy::EntityKind::decoration && type &&
                type->tags.contains("clear-on-build");
        if (type && type->tags.contains("grass")) {
            ++grassVariants[entity.archetype.value];
            const auto& vegetation = definitions.matchRules().vegetation;
            const auto rule = std::find_if(vegetation.begin(), vegetation.end(), [&](const auto& item) {
                return item.archetype == entity.archetype.value;
            });
            const float normalizedHeight = terrain.heightAt(entity.transform.position.x,
                                                             entity.transform.position.z) /
                                           strategy::Terrain::heightScale;
            valid = valid && rule != vegetation.end() &&
                    normalizedHeight >= rule->minimumHeight &&
                    normalizedHeight <= rule->maximumHeight;
        }
    }
    valid = valid && grassVariants["grass_dry"] > 0 &&
            grassVariants["grass_aged"] > 0 &&
            grassVariants["grass_fresh"] > 0;

    const strategy::EntityId removed = first.entities().front().id;
    const glm::vec3 center = first.entities().front().transform.position;
    strategy::clearVegetationWithin(
        first, definitions,
        {strategy::FootprintShape::circle, {center.x, center.z}, 2.0F});
    valid = valid && first.findEntity(removed) == nullptr;

    if (!valid) std::cerr << "Vegetation generation or clearing test failed\n";
    return valid ? 0 : 1;
}
