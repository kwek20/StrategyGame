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

int main() {
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
    fullTarget.advanceTicks();
    activeFallbackDrone = fullTarget.world().findEntity(fallbackDroneId);
    const strategy::Entity* activeFallbackHub = fullTarget.world().findEntity(fallbackHubId);
    valid = valid && activeFallbackDrone->gatherer.carriedAmount == 0.0F &&
            activeFallbackDrone->gatherer.preferredProcessor == 0 &&
            activeFallbackHub->processor.bufferedInputs.contains("scrap") &&
            std::abs(activeFallbackHub->processor.bufferedInputs.at("scrap") - 10.0F) < 0.001F;

    if (!valid) std::cerr << "Resource delivery routing test failed\n";
    return valid ? 0 : 1;
}
