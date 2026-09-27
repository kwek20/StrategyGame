#include "gameplay/DefinitionRegistry.hpp"
#include "simulation/Command.hpp"
#include "simulation/GameSession.hpp"

#include <cmath>
#include <iostream>

namespace {
strategy::Entity& create(strategy::GameSession& session,
                         const strategy::DefinitionRegistry& definitions,
                         const char* name,
                         const char* archetype) {
    strategy::Entity& entity = session.world().createEntity(name, archetype, 1);
    definitions.initializeEntity(entity);
    return entity;
}
}

int strategyTestMain() {
    const strategy::DefinitionRegistry definitions;
    bool valid = true;

    strategy::GameSession powerLoss{definitions, 101U};
    powerLoss.replaceWorld({}, 101U);
    strategy::Entity& hub = create(powerLoss, definitions, "Hub", "command_hub");
    const strategy::EntityId hubId = hub.id;
    strategy::Entity& processor =
        create(powerLoss, definitions, "Processor", "alloy_processor");
    const strategy::EntityId processorId = processor.id;
    strategy::Entity& drone =
        create(powerLoss, definitions, "Drone", "construction_drone");
    const strategy::EntityId droneId = drone.id;
    powerLoss.world().findEntity(hubId)->transform.position = {0, 0, 0};
    powerLoss.world().findEntity(processorId)->transform.position = {12, 0, 0};
    strategy::Entity* activeDrone = powerLoss.world().findEntity(droneId);
    activeDrone->transform.position = {0, 6, 0};
    activeDrone->gatherer.carriedResource = "scrap";
    activeDrone->gatherer.carriedAmount = 10.0F;
    activeDrone->unitControl.order = strategy::UnitOrderKind::returnResources;
    activeDrone->gatherer.preferredProcessor = processorId;
    activeDrone->gatherer.deliveryTarget = processorId;
    valid = powerLoss.submit(
                {1, 1, strategy::ConnectPowerCommand{hubId, processorId}}) && valid;
    powerLoss.advanceTicks();
    valid = powerLoss.submit(
                {1, 2, strategy::SetPowerEnabledCommand{processorId, false}}) && valid;
    powerLoss.advanceTicks();
    activeDrone = powerLoss.world().findEntity(droneId);
    valid = valid && activeDrone->gatherer.deliveryTarget == processorId &&
            activeDrone->gatherer.preferredProcessor == processorId &&
            std::abs(activeDrone->gatherer.carriedAmount - 10.0F) < 0.001F;

    strategy::GameSession fullTarget{definitions, 102U};
    fullTarget.replaceWorld({}, 102U);
    strategy::Entity& fallbackHub = create(fullTarget, definitions, "Hub", "command_hub");
    const strategy::EntityId fallbackHubId = fallbackHub.id;
    strategy::Entity& fullProcessor =
        create(fullTarget, definitions, "Full processor", "alloy_processor");
    const strategy::EntityId fullProcessorId = fullProcessor.id;
    fullProcessor.processor.bufferedInputs["blocked_input"] = 400.0F;
    strategy::Entity& fallbackDrone =
        create(fullTarget, definitions, "Drone", "construction_drone");
    const strategy::EntityId fallbackDroneId = fallbackDrone.id;
    fullTarget.world().findEntity(fallbackHubId)->transform.position = {0, 0, 0};
    fullTarget.world().findEntity(fullProcessorId)->transform.position = {8, 0, 0};
    strategy::Entity* activeFallbackDrone = fullTarget.world().findEntity(fallbackDroneId);
    activeFallbackDrone->transform.position = {0, 6, 0};
    activeFallbackDrone->gatherer.carriedResource = "scrap";
    activeFallbackDrone->gatherer.carriedAmount = 10.0F;
    activeFallbackDrone->gatherer.preferredProcessor = fullProcessorId;
    activeFallbackDrone->gatherer.deliveryTarget = fullProcessorId;
    activeFallbackDrone->unitControl.order = strategy::UnitOrderKind::returnResources;
    const float fallbackAlloyBefore = fullTarget.players().find(1)->resources["alloy"];
    fullTarget.advanceTicks();
    activeFallbackDrone = fullTarget.world().findEntity(fallbackDroneId);
    const strategy::Entity* activeFallbackHub = fullTarget.world().findEntity(fallbackHubId);
    valid = valid && activeFallbackDrone->gatherer.carriedAmount == 0.0F &&
            activeFallbackDrone->gatherer.preferredProcessor == 0 &&
            activeFallbackHub->processor.bufferedInputs.empty() &&
            std::abs(fullTarget.players().find(1)->resources.at("alloy") - fallbackAlloyBefore - 5.0F) < 0.001F;

    strategy::GameSession fieldRetarget{definitions, 103U};
    fieldRetarget.replaceWorld({}, 103U);
    strategy::Entity& fieldDrone = create(
        fieldRetarget, definitions, "Field drone", "construction_drone");
    const strategy::EntityId fieldDroneId = fieldDrone.id;
    strategy::Entity& firstNode = create(
        fieldRetarget, definitions, "First scrap", "scrap_node_small");
    const strategy::EntityId firstNodeId = firstNode.id;
    strategy::Entity& secondNode = create(
        fieldRetarget, definitions, "Second scrap", "scrap_node_medium");
    const strategy::EntityId secondNodeId = secondNode.id;
    fieldRetarget.world().findEntity(fieldDroneId)->transform.position = {0, 6, 0};
    fieldRetarget.world().findEntity(firstNodeId)->transform.position = {0, 0, 0};
    fieldRetarget.world().findEntity(secondNodeId)->transform.position = {4, 0, 0};
    fieldRetarget.world().findEntity(firstNodeId)->resource.remaining = 0.01F;
    fieldRetarget.world().findEntity(secondNodeId)->resource.remaining = 50.0F;
    valid = fieldRetarget.submit(
                {1, 1, strategy::GatherResourceCommand{fieldDroneId, firstNodeId, 0}}) && valid;
    fieldRetarget.advanceTicks();
    const strategy::Entity* retargeted = fieldRetarget.world().findEntity(fieldDroneId);
    valid = valid && retargeted->unitControl.order == strategy::UnitOrderKind::gather &&
            retargeted->unitControl.orderTarget == secondNodeId &&
            retargeted->gatherer.sourceTarget == secondNodeId;

    // A nearer same-resource node in another field wins, even when generation diagnostics
    // are present. Clearing those diagnostics through the load/replacement boundary must
    // leave the choice unchanged. A same-distance candidate also checks the stable-ID tie.
    const strategy::EntityId crossFieldId = create(
        fieldRetarget, definitions, "Other field scrap", "scrap_node_small").id;
    const strategy::EntityId tiedId = create(
        fieldRetarget, definitions, "Equidistant scrap", "scrap_node_small").id;
    const strategy::EntityId oilId = create(
        fieldRetarget, definitions, "Closer oil", "oil_node_small").id;
    const strategy::EntityId distantId = create(
        fieldRetarget, definitions, "Distant scrap", "scrap_node_small").id;
    auto* retargetDrone = fieldRetarget.world().findEntity(fieldDroneId);
    retargetDrone->transform.position = {9, 6, 0};
    retargetDrone->gatherer.carriedAmount = 0.0F;
    retargetDrone->gatherer.carriedResource.clear();
    retargetDrone->gatherer.sourceTarget = firstNodeId;
    retargetDrone->unitControl.order = strategy::UnitOrderKind::gather;
    retargetDrone->unitControl.orderTarget = firstNodeId;
    retargetDrone->unitControl.hasStrategicDestination = false;
    fieldRetarget.world().findEntity(firstNodeId)->transform.position = {9, 0, 0};
    fieldRetarget.world().findEntity(firstNodeId)->resource.remaining = 0.0F;
    fieldRetarget.world().findEntity(secondNodeId)->transform.position = {0, 0, 0};
    fieldRetarget.world().findEntity(crossFieldId)->transform.position = {15, 0, 0};
    fieldRetarget.world().findEntity(tiedId)->transform.position = {9, 0, 6};
    fieldRetarget.world().findEntity(oilId)->transform.position = {10, 0, 0};
    const auto* scrapField = definitions.resourceFieldForNode(
        strategy::ResourceArchetypeId{"scrap_node_small"});
    if (!scrapField) {
        std::cerr << "Missing scrap field definition for retarget regression\n";
        return 1;
    }
    fieldRetarget.world().findEntity(distantId)->transform.position = {
        9 + 2 * scrapField->generation.maximumFieldRadius + 1, 0, 0};
    const auto retargetSnapshot = fieldRetarget.world().entities();
    // Test-only fixture: production exposes this layout as read-only diagnostics.
    auto& diagnosticLayout = const_cast<strategy::ResourceLayout&>(fieldRetarget.resourceLayout());
    diagnosticLayout.fields = {{strategy::ResourceFieldId{"scrap_field"}, {0, 0}, 10, 2}};
    fieldRetarget.advanceTicks();
    const auto withGeometry = fieldRetarget.world().findEntity(fieldDroneId)->gatherer.sourceTarget;
    fieldRetarget.replaceWorld(retargetSnapshot, 103U);
    fieldRetarget.advanceTicks();
    retargeted = fieldRetarget.world().findEntity(fieldDroneId);
    const bool stableRetarget = withGeometry == crossFieldId &&
        retargeted->gatherer.sourceTarget == crossFieldId &&
        retargeted->unitControl.orderTarget == crossFieldId &&
        retargeted->unitControl.order == strategy::UnitOrderKind::gather;
    if (!stableRetarget)
        std::cerr << "Cross-field retarget changed at the world-replacement boundary\n";
    valid = stableRetarget && valid;

    // Exhaust the nearby Scrap candidates: neither closer Oil nor out-of-range Scrap wins.
    for (const auto id : {secondNodeId, crossFieldId, tiedId})
        fieldRetarget.world().findEntity(id)->resource.remaining = 0.0F;
    retargetDrone = fieldRetarget.world().findEntity(fieldDroneId);
    retargetDrone->unitControl.order = strategy::UnitOrderKind::gather;
    retargetDrone->unitControl.orderTarget = firstNodeId;
    retargetDrone->gatherer.sourceTarget = firstNodeId;
    fieldRetarget.advanceTicks();
    const bool boundedRetarget = fieldRetarget.world().findEntity(fieldDroneId)->unitControl.order ==
                                strategy::UnitOrderKind::idle;
    if (!boundedRetarget)
        std::cerr << "Depletion retarget crossed its resource-type or distance boundary\n";
    valid = boundedRetarget && valid;

    strategy::GameSession stableDelivery{definitions, 104U};
    stableDelivery.replaceWorld({}, 104U);
    strategy::Entity& deliveryHub = create(
        stableDelivery, definitions, "Delivery hub", "command_hub");
    const strategy::EntityId deliveryHubId = deliveryHub.id;
    strategy::Entity& deliveryDrone = create(
        stableDelivery, definitions, "Delivery drone", "construction_drone");
    const strategy::EntityId deliveryDroneId = deliveryDrone.id;
    stableDelivery.world().findEntity(deliveryHubId)->transform.position = {30, 0, 0};
    strategy::Entity* transporting = stableDelivery.world().findEntity(deliveryDroneId);
    transporting->transform.position = {0, 6, 0};
    transporting->gatherer.carriedResource = "scrap";
    transporting->gatherer.carriedAmount = 10.0F;
    transporting->unitControl.order = strategy::UnitOrderKind::returnResources;
    stableDelivery.advanceTicks();
    transporting = stableDelivery.world().findEntity(deliveryDroneId);
    const glm::vec3 committedDestination = transporting->unitControl.strategicDestination;
    const strategy::EntityId committedTarget = transporting->gatherer.deliveryTarget;
    stableDelivery.advanceTicks(10);
    transporting = stableDelivery.world().findEntity(deliveryDroneId);
    valid = valid && committedTarget == deliveryHubId &&
            transporting->gatherer.deliveryTarget == committedTarget &&
            glm::distance(transporting->unitControl.strategicDestination,
                          committedDestination) < 0.001F;

    // New deliveries convert on their delivery tick regardless of entity creation order.
    for (const bool droneFirst : {false, true}) {
        strategy::GameSession timing{definitions, 919U};
        timing.replaceWorld({}, 919U);
        const auto source = create(timing, definitions, "Source", "command_hub").id;
        strategy::EntityId receiver = 0, carrier = 0;
        if (droneFirst) carrier = create(timing, definitions, "Carrier", "construction_drone").id;
        receiver = create(timing, definitions, "Receiver", "alloy_processor").id;
        if (!droneFirst) carrier = create(timing, definitions, "Carrier", "construction_drone").id;
        timing.players().find(1)->resources["alloy"] = 0;
        auto* cargoDrone = timing.world().findEntity(carrier);
        cargoDrone->transform.position = {0, 6, 0};
        cargoDrone->gatherer.carriedResource = "scrap";
        cargoDrone->gatherer.carriedAmount = 10;
        cargoDrone->gatherer.deliveryTarget = receiver;
        cargoDrone->gatherer.preferredProcessor = receiver;
        cargoDrone->unitControl.order = strategy::UnitOrderKind::returnResources;
        valid = timing.submit({1, 1, strategy::ConnectPowerCommand{source, receiver}}) && valid;
        timing.advanceTicks();
        const auto* receiving = timing.world().findEntity(receiver);
        if (cargoDrone->gatherer.carriedAmount != 0 || !receiving->processor.bufferedInputs.empty() ||
            timing.players().find(1)->resources.at("alloy") != 10 || receiving->processor.lastConversionTick != 0) {
            std::cerr << "Delivery-tick conversion failed (droneFirst=" << droneFirst << ")\n";
            valid = false;
        }
        // Previously buffered cargo still waits through a shortage, then converts on recovery.
        timing.world().findEntity(receiver)->processor.bufferedInputs["scrap"] = 3;
        valid = timing.submit({1, 2, strategy::SetPowerOutputEnabledCommand{source, false}}) && valid;
        timing.advanceTicks();
        valid = valid && timing.world().findEntity(receiver)->processor.bufferedInputs.at("scrap") == 3 &&
                timing.players().find(1)->resources.at("alloy") == 10;
        valid = timing.submit({1, 3, strategy::SetPowerOutputEnabledCommand{source, true}}) && valid;
        timing.advanceTicks();
        valid = valid && timing.world().findEntity(receiver)->processor.bufferedInputs.empty() &&
                timing.players().find(1)->resources.at("alloy") == 13;
    }

    if (!valid) std::cerr << "Resource delivery routing test failed\n";
    return valid ? 0 : 1;
}
