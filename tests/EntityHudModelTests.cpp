#include "gameplay/GameplayCatalogue.hpp"
#include "ui/EntityHudModel.hpp"
#include "ui/ResourceFieldSelection.hpp"
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
    valid = valid && multiple.bars.size() == 1 && multiple.bars[0].kind == strategy::HudBarKind::health &&
            multiple.bars[0].current == single.bars[0].current * 2 &&
            multiple.bars[0].maximum == single.bars[0].maximum * 2 &&
            multiple.stats.size() == 1 && multiple.stats[0].value == "2" &&
            multiple.title == "2 ENTITIES SELECTED" && !multiple.footer.empty();

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

    {
        strategy::World fields;
        auto& a = fields.createEntity("Scrap beams", "v2_scrap_beams", 0);
        definitions.initializeEntity(a);
        a.resource.fieldId = 11;
        a.resource.remaining = 25;
        const auto aId = a.id;
        auto& b = fields.createEntity("Scrap frame", "v2_scrap_frame", 0);
        definitions.initializeEntity(b);
        b.resource.fieldId = 11;
        b.resource.remaining = 50;
        const auto bId = b.id;
        auto& other = fields.createEntity("Other field", "v2_scrap_beams", 0);
        definitions.initializeEntity(other);
        other.resource.fieldId = 12;
        auto& oil = fields.createEntity("Other family", "v2_oil_seep", 0);
        definitions.initializeEntity(oil);
        oil.resource.fieldId = 11;

        const auto field = strategy::ResourceFieldSelection::fromClick(fields, aId, false);
        valid &= field.members(fields) == std::vector<strategy::EntityId>{aId, bId};
        const auto fieldHud = strategy::EntityHudModelBuilder::build(
            fields, aId, field.members(fields), definitions, true);
        valid &= fieldHud.totalEntities == 2 && fieldHud.cards.empty() &&
                 fieldHud.selectionGroups.empty() && fieldHud.stats.size() == 2 &&
                 fieldHud.stats[0].value.starts_with("75.0 ") && fieldHud.stats[1].value == "2";
        const auto node = strategy::ResourceFieldSelection::fromClick(fields, aId, true);
        const auto nodeHud = strategy::EntityHudModelBuilder::build(fields, aId, {}, definitions);
        valid &= !node && nodeHud.totalEntities == 1 && nodeHud.stats[0].value.starts_with("25.0 ");

        fields.findEntity(aId)->resource.remaining = 0;
        valid &= field.members(fields) == std::vector<strategy::EntityId>{bId};
        const auto lastNodeField = strategy::EntityHudModelBuilder::build(
            fields, bId, field.members(fields), definitions, true);
        valid &= lastNodeField.totalEntities == 1 && lastNodeField.stats.size() == 2 &&
                 lastNodeField.stats[0].value.starts_with("50.0 ") &&
                 lastNodeField.title == fieldHud.title;
        fields.destroyEntity(aId);
        valid &= field.members(fields) == std::vector<strategy::EntityId>{bId};
        fields.findEntity(bId)->resource.remaining = 0;
        valid &= field.members(fields).empty();
        valid &= !strategy::ResourceFieldSelection::fromClick(fields, bId, false);
    }

    if (!valid) std::cerr << "Entity HUD model presentation failed\n";
    return valid ? 0 : 1;
}
