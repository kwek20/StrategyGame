#include "gameplay/GameplayCatalogue.hpp"
#include "ui/EntityHudModel.hpp"
#include "ui/EntityHudLayout.hpp"
#include "ui/UiController.hpp"
#include "ui/GameHudLayout.hpp"
#include "ui/UiLayout.hpp"
#include "world/World.hpp"

#include <iostream>

int main() {
    const strategy::GameplayCatalogue definitions;
    strategy::World world;
    strategy::Entity& first = world.createEntity("Drone", "construction_drone", 1);
    definitions.initializeEntity(first);
    const strategy::EntityId firstId = first.id;
    strategy::Entity& second = world.createEntity("Drone", "construction_drone", 1);
    definitions.initializeEntity(second);
    const strategy::EntityId secondId = second.id;

    const strategy::EntityHudModel single = strategy::EntityHudModelBuilder::build(
        world, firstId, {}, definitions);
    bool valid = single.totalEntities == 1 && single.cards.empty() &&
                 single.bars.size() == 2 && single.stats.size() == 5;

    const strategy::EntityHudModel multiple = strategy::EntityHudModelBuilder::build(
        world, firstId, {firstId, secondId}, definitions);
    valid = valid && multiple.totalEntities == 2 && multiple.cards.size() == 2 &&
            multiple.cards[0].bars.size() == 2 && multiple.cards[1].bars.size() == 2 &&
            multiple.selectionGroups.size() == 1 && multiple.selectionGroups[0].count == 2;

    strategy::Entity& building = world.createEntity("Hub", "command_hub", 1);
    definitions.initializeEntity(building);
    const strategy::EntityId buildingId = building.id;
    const strategy::EntityHudModel buildingHud = strategy::EntityHudModelBuilder::build(
        world, buildingId, {}, definitions);
    valid = valid && buildingHud.totalEntities == 1 && !buildingHud.bars.empty() &&
            buildingHud.stats.size() >= 3;

    strategy::EntityHudModel interactive = buildingHud;
    interactive.actions.push_back(
        {"command_hub.train_construction_drone", "unit_drone", "Train drone", {}, {}, {}, true});
    interactive.actions.push_back(
        {"locked.action", "status_asset_failed", "Locked", {}, {}, {}, false});
    interactive.queue.push_back({"unit_drone", "Construction drone", 0.5F, true});
    strategy::UiDocument layout = strategy::EntityHudLayout::actions(interactive, 1280, 720);
    const strategy::UiElement* action = layout.hitTest({40.0F, 660.0F});
    valid = valid && action &&
            strategy::EntityHudLayout::actionId(action->id) ==
                std::optional<std::string>{"command_hub.train_construction_drone"};
    const strategy::UiElement* disabled = layout.hitTest({150.0F, 660.0F});
    valid = valid && disabled && !disabled->enabled &&
            !layout.activate({150.0F, 660.0F});
    const strategy::UiElement* queue = layout.hitTest({40.0F, 550.0F});
    valid = valid && queue && strategy::EntityHudLayout::queueIndex(queue->id) == 0;
    layout.pointerMoved({40.0F, 660.0F});
    valid = valid && layout.hoveredElement() &&
            layout.hoveredElement()->id == action->id;

    strategy::Entity& mixedBuilding = world.createEntity("Hub", "command_hub", 1);
    definitions.initializeEntity(mixedBuilding);
    const strategy::EntityHudModel mixed = strategy::EntityHudModelBuilder::build(
        world, firstId, {firstId, mixedBuilding.id}, definitions);
    strategy::UiDocument selectionLayout =
        strategy::EntityHudLayout::selection(mixed, 1280, 720);
    const strategy::UiElement* selectionRow = selectionLayout.hitTest({40.0F, 620.0F});
    valid = valid && mixed.selectionGroups.size() == 2 && selectionRow &&
            strategy::EntityHudLayout::selectionArchetype(selectionRow->id).has_value();

    strategy::UiController controller;
    controller.pointerMoved({40.0F, 660.0F});
    controller.apply(layout);
    valid = valid && controller.hoveredId() == action->id;
    controller.advance(0.5F);
    valid = valid && controller.visibleTooltip(layout).has_value();
    controller.apply(layout);
    valid = valid && layout.find("entity.tooltip") &&
            layout.find("entity.tooltip")->text.starts_with("Train drone");
    valid = valid && controller.press(layout, {40.0F, 660.0F}) == action->id &&
            controller.pressedId() == action->id;
    valid = valid && !controller.press(layout, {150.0F, 660.0F});
    valid = valid && controller.moveFocus(layout, 1) &&
            controller.activateFocused(layout).has_value();

    strategy::UiDocument resources = strategy::GameHudLayout::resources(3, 2, true, 1280, 720);
    valid = valid && resources.find("resources.panel") && resources.find("resources.power") &&
            resources.find("power.panel") &&
            resources.hitTest({345.0F, 25.0F})->id == "resources.power";

    const strategy::UiLayout wideCanvas(1920, 1080, 1.0F);
    const strategy::UiRect anchored = wideCanvas.rect(
        strategy::UiAnchor::bottomRight, 20.0F, 20.0F, 200.0F, 100.0F);
    valid = valid && anchored.right <= 1920.0F && anchored.bottom <= 1080.0F &&
            anchored.left > 1500.0F && anchored.top > 800.0F;
    const auto columns = wideCanvas.row(anchored, 3, 8.0F, 32.0F);
    valid = valid && columns.size() == 3 && columns[0].right < columns[1].left &&
            columns[2].right <= anchored.right;

    strategy::UiDocument responsiveHud =
        strategy::EntityHudLayout::actions(interactive, 1920, 1080, 1.25F);
    const strategy::UiElement* responsivePanel = responsiveHud.find("entity.panel");
    valid = valid && responsivePanel && responsivePanel->bounds.left >= 0.0F &&
            responsivePanel->bounds.bottom <= 1080.0F;

    if (!valid) std::cerr << "Entity HUD component presentation failed\n";
    return valid ? 0 : 1;
}
