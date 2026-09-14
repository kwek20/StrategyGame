#include "gameplay/DefinitionRegistry.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/World.hpp"
#include "world/WorldGeneration.hpp"

#include <iostream>
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
    std::size_t grass = 0;
    std::size_t trees = 0;
    for (const strategy::Entity& entity : first.entities()) {
        const auto* type = definitions.archetype(entity.archetype);
        valid = valid && entity.kind == strategy::EntityKind::decoration && type &&
                type->tags.contains("clear-on-build");
        if (type && type->tags.contains("grass")) {
            ++grass;
            valid = valid && terrain.heightAt(entity.transform.position.x,
                                               entity.transform.position.z) /
                                     strategy::Terrain::heightScale >=
                                 0.38F;
        }
        if (type && type->tags.contains("tree")) {
            ++trees;
            valid = valid && entity.presentation == strategy::PresentationId{"grass_tall"};
        }
    }
    valid = valid && grass > 0 && trees > 0;

    const strategy::EntityId removed = first.entities().front().id;
    const glm::vec3 center = first.entities().front().transform.position;
    strategy::clearVegetationWithin(
        first, definitions,
        {strategy::FootprintShape::circle, {center.x, center.z}, 2.0F});
    valid = valid && first.findEntity(removed) == nullptr;

    if (!valid) std::cerr << "Vegetation generation or clearing test failed\n";
    return valid ? 0 : 1;
}
