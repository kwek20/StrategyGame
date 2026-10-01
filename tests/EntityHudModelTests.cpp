#include "gameplay/GameplayCatalogue.hpp"
#include "ui/EntityHudModel.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <iostream>

int strategyTestMain() {
    const strategy::GameplayCatalogue definitions;
    strategy::World world;
    auto& first = world.createEntity("Drone", "construction_drone", 1);
    definitions.initializeEntity(first);
    const auto firstId = first.id;
    auto& second = world.createEntity("Drone", "construction_drone", 1);
    definitions.initializeEntity(second);
    const auto secondId = second.id;

    const auto single = strategy::EntityHudModelBuilder::build(world, firstId, {}, definitions);
    bool valid = single.totalEntities == 1 && single.cards.empty() && single.bars.size() == 2 &&
                 single.stats.size() == 6;
    const auto multiple = strategy::EntityHudModelBuilder::build(
        world, firstId, {firstId, secondId}, definitions);
    valid = valid && multiple.totalEntities == 2 && multiple.cards.size() == 2 &&
            multiple.selectionGroups.size() == 1 && multiple.selectionGroups[0].count == 2 &&
            multiple.cards[0].bars.size() == 2 && multiple.cards[1].bars.size() == 2;

    auto& building = world.createEntity("Hub", "command_hub", 1);
    definitions.initializeEntity(building);
    const auto buildingHud = strategy::EntityHudModelBuilder::build(
        world, building.id, {}, definitions);
    valid = valid && buildingHud.totalEntities == 1 && !buildingHud.bars.empty() &&
            buildingHud.stats.size() >= 3 && !buildingHud.processorInputs.empty() &&
            !buildingHud.processorState.empty();

    auto& resource = world.createEntity("Scrap field", "scrap_node_small", 0);
    definitions.initializeEntity(resource);
    resource.resource.remaining = 137.5F;
    const auto resourceHud = strategy::EntityHudModelBuilder::build(
        world, resource.id, {}, definitions);
    const auto remaining = std::find_if(resourceHud.stats.begin(), resourceHud.stats.end(),
        [](const auto& stat) { return stat.label == "RESOURCE LEFT"; });
    valid = valid && remaining != resourceHud.stats.end() &&
            remaining->value.find("137.5") != std::string::npos;

    auto& mixedBuilding = world.createEntity("Hub", "command_hub", 1);
    definitions.initializeEntity(mixedBuilding);
    const auto mixed = strategy::EntityHudModelBuilder::build(
        world, firstId, {firstId, mixedBuilding.id}, definitions);
    valid = valid && mixed.selectionGroups.size() == 2;

    if (!valid) std::cerr << "Entity HUD model presentation failed\n";
    return valid ? 0 : 1;
}
