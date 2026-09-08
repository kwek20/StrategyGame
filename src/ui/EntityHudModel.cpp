#include "ui/EntityHudModel.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

namespace strategy {
namespace {

std::string decimal(float value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << value;
    return output.str();
}

void presentHealth(const Entity& entity, const DefinitionRegistry&, EntityHudModel& model) {
    if (entity.health)
        model.bars.push_back(
            {Text::get("entity_hud.health_label"), entity.health.current,
             entity.health.maximum, HudBarKind::health});
}

void presentBattery(const Entity& entity, const DefinitionRegistry&, EntityHudModel& model) {
    if (entity.battery) {
        model.bars.push_back(
            {Text::get("entity_hud.power_label"), entity.battery.charge,
             entity.battery.capacity, HudBarKind::power});
        if (entity.unitControl && entity.unitControl.order == UnitOrderKind::stranded)
            model.footer = Text::get("entity_hud.no_charger");
    }
}

void presentGatherer(const Entity& entity, const DefinitionRegistry&, EntityHudModel& model) {
    if (!entity.gatherer) return;
    model.stats.push_back({Text::get("entity_hud.cargo"),
                           std::to_string(static_cast<int>(entity.gatherer.carriedAmount)) +
                               " / " +
                               std::to_string(static_cast<int>(entity.gatherer.carryCapacity))});
    model.stats.push_back(
        {Text::get("entity_hud.gather"), decimal(entity.gatherer.gatherPerSecond) + "/s"});
}

void presentMovement(const Entity& entity, const DefinitionRegistry&, EntityHudModel& model) {
    if (entity.unitControl)
        model.stats.push_back(
            {Text::get("entity_hud.speed"), decimal(entity.unitControl.movementSpeed)});
}

void presentVision(const Entity& entity, const DefinitionRegistry&, EntityHudModel& model) {
    if (entity.vision)
        model.stats.push_back({Text::get("entity_hud.vision"), decimal(entity.vision.sightRange)});
}

void presentConstruction(const Entity& entity, const DefinitionRegistry&, EntityHudModel& model) {
    if (entity.construction && !isOperational(entity))
        model.bars.push_back({Text::get("entity_hud.construction"),
                              entity.construction.powerProgress,
                              entity.construction.powerRequired, HudBarKind::power});
}

void presentPowerDevice(const Entity& entity, const DefinitionRegistry& definitions,
                        EntityHudModel& model) {
    const EntityArchetype* archetype = definitions.archetype(entity.archetype);
    if (!archetype || !archetype->powerDevice) return;
    const PowerDeviceDefinition* device = definitions.powerDevice(*archetype->powerDevice);
    if (!device) return;
    if (device->production > 0.0F)
        model.stats.push_back({Text::get("entity_hud.generation"), decimal(device->production) + " kW"});
    if (device->consumption > 0.0F)
        model.stats.push_back({Text::get("entity_hud.consumption"), decimal(device->consumption) + " kW"});
}

void presentProduction(const Entity& entity, const DefinitionRegistry& definitions,
                       EntityHudModel& model) {
    if (!entity.production) return;
    model.stats.push_back({Text::get("entity_hud.queue"),
                           std::to_string(entity.production.queue.size())});
    for (const ProductionOrder& order : entity.production.queue) {
        std::string name = order.productId;
        if (!order.upgradeId.empty()) {
            if (const UpgradeDefinition* upgrade = definitions.upgrade(order.upgradeId))
                name = Text::get(upgrade->nameKey);
        } else if (const EntityArchetype* product = definitions.archetype(order.productId))
            name = Text::get(product->nameKey);
        const float progress = order.durationTicks > 0
            ? 1.0F - static_cast<float>(order.remainingTicks) /
                         static_cast<float>(order.durationTicks) : 1.0F;
        model.queue.push_back({order.iconId, std::move(name), progress, true});
    }
}

void presentUpgrades(const Entity& entity, const DefinitionRegistry&, EntityHudModel& model) {
    if (entity.upgrades)
        model.stats.push_back({Text::get("entity_hud.upgrades"),
                               std::to_string(entity.upgrades.levels.size())});
}

using ComponentPresenter = void (*)(const Entity&, const DefinitionRegistry&, EntityHudModel&);
constexpr std::array<ComponentPresenter, 9> componentPresenters{
    presentHealth, presentBattery, presentGatherer, presentMovement, presentVision,
    presentConstruction, presentPowerDevice, presentProduction, presentUpgrades};

HudEntityCardModel card(const Entity& entity, const DefinitionRegistry& definitions) {
    HudEntityCardModel result;
    result.entity = entity.id;
    result.icon = definitions.presentationIcon(entity.presentation);
    if (entity.health)
        result.bars.push_back({Text::get("entity_hud.health_label"), entity.health.current,
                               entity.health.maximum, HudBarKind::health});
    if (entity.battery)
        result.bars.push_back({Text::get("entity_hud.power_label"), entity.battery.charge,
                               entity.battery.capacity, HudBarKind::power});
    return result;
}

} // namespace

EntityHudModel EntityHudModelBuilder::build(const World& world,
                                            EntityId selected,
                                            const std::vector<EntityId>& selection,
                                            const DefinitionRegistry& definitions) {
    std::vector<const Entity*> entities;
    if (selection.empty()) {
        if (const Entity* entity = world.findEntity(selected)) entities.push_back(entity);
    } else {
        for (EntityId id : selection)
            if (const Entity* entity = world.findEntity(id)) entities.push_back(entity);
    }

    EntityHudModel result;
    result.totalEntities = entities.size();
    if (entities.empty()) return result;
    result.title = entities.front()->name;
    result.portraitIcon = definitions.presentationIcon(entities.front()->presentation);
    if (entities.size() == 1) {
        for (ComponentPresenter presenter : componentPresenters)
            presenter(*entities.front(), definitions, result);
    } else {
        result.cards.reserve(entities.size());
        for (const Entity* entity : entities) {
            result.cards.push_back(card(*entity, definitions));
            const auto found = std::find_if(result.selectionGroups.begin(),
                result.selectionGroups.end(), [&](const HudSelectionGroupModel& group) {
                    return group.archetype == entity->archetype.value;
                });
            if (found != result.selectionGroups.end()) {
                ++found->count;
            } else {
                result.selectionGroups.push_back({entity->archetype.value, entity->name,
                    definitions.presentationIcon(entity->presentation), 1});
            }
        }
    }
    return result;
}

} // namespace strategy
