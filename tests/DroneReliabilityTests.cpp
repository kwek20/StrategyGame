#include "gameplay/DefinitionRegistry.hpp"
#include "simulation/Command.hpp"
#include "simulation/GameSession.hpp"

#include <cmath>
#include <iostream>
#include <vector>

namespace {
strategy::Entity& create(strategy::GameSession& session,
                         const strategy::DefinitionRegistry& definitions,
                         const char* name, const char* archetype,
                         glm::vec3 position) {
    strategy::Entity& entity = session.world().createEntity(name, archetype, 1);
    definitions.initializeEntity(entity);
    entity.transform.position = position;
    return entity;
}
}

int strategyTestMain() {
    const strategy::DefinitionRegistry definitions;
    strategy::GameSession session{definitions, 0xD20A5EEDU};
    session.replaceWorld({}, 0xD20A5EEDU);
    bool valid = true;
    const auto check = [&](bool condition, const char* stage) {
        if (!condition) std::cerr << "Drone reliability failure: " << stage << '\n';
        valid = valid && condition;
    };
    std::uint64_t sequence = 1;

    // A powered charger outside the playable domain exists, but is not reachable.
    strategy::Entity& remoteHub = create(
        session, definitions, "Remote hub", "command_hub", {10000.0F, 0.0F, 10000.0F});
    const strategy::EntityId remoteHubId = remoteHub.id;
    strategy::Entity& first = create(
        session, definitions, "Drone one", "construction_drone", {0.0F, 6.0F, 0.0F});
    const strategy::EntityId firstId = first.id;
    first.battery.charge = 0.0F;
    valid = session.submit({1, sequence++, strategy::RechargeCommand{firstId}}) && valid;
    session.advanceTicks();
    const strategy::Entity* checked = session.world().findEntity(firstId);
    check(checked && checked->unitControl.order == strategy::UnitOrderKind::stranded &&
              checked->battery.recoveryReason ==
                  strategy::BatteryRecoveryReason::noReachableCharger,
          "inaccessible charger");

    session.world().destroyEntity(remoteHubId);
    strategy::Entity& pad = create(
        session, definitions, "Disconnected pad", "charging_pad", {4.0F, 0.0F, 0.0F});
    const strategy::EntityId padId = pad.id;
    session.advanceTicks();
    checked = session.world().findEntity(firstId);
    check(checked && checked->unitControl.order == strategy::UnitOrderKind::stranded &&
              checked->battery.recoveryReason == strategy::BatteryRecoveryReason::noCharger,
          "disconnected charger");

    strategy::Entity& hub = create(
        session, definitions, "Recovery hub", "command_hub", {0.0F, 0.0F, 0.0F});
    const strategy::EntityId hubId = hub.id;
    strategy::Entity& second = create(
        session, definitions, "Drone two", "construction_drone", {1.0F, 6.0F, 0.0F});
    const strategy::EntityId secondId = second.id;
    strategy::Entity& third = create(
        session, definitions, "Drone three", "construction_drone", {2.0F, 6.0F, 0.0F});
    const strategy::EntityId thirdId = third.id;
    session.world().findEntity(secondId)->battery.charge = 0.0F;
    session.world().findEntity(thirdId)->battery.charge = 0.0F;
    valid = session.submit({1, sequence++, strategy::RechargeCommand{secondId}}) && valid;
    valid = session.submit({1, sequence++, strategy::RechargeCommand{thirdId}}) && valid;
    session.advanceTicks();
    std::size_t assigned = 0, occupied = 0;
    std::vector<strategy::EntityId> assignedIds;
    for (const strategy::EntityId id : {firstId, secondId, thirdId}) {
        const strategy::Entity* drone = session.world().findEntity(id);
        if (drone && drone->battery.chargerTarget == hubId) {
            ++assigned;
            assignedIds.push_back(id);
        }
        if (drone && drone->battery.recoveryReason ==
                         strategy::BatteryRecoveryReason::chargersOccupied)
            ++occupied;
    }
    check(assigned == 2 && occupied == 1, "charger capacity");

    // Destroying the selected charger strands assigned drones without losing suspended work.
    session.world().destroyEntity(hubId);
    session.advanceTicks();
    checked = session.world().findEntity(firstId);
    check(checked && checked->unitControl.order == strategy::UnitOrderKind::stranded,
          "destroyed charger");

    // Recreate a charger, suspend construction, recharge, and verify deterministic resumption.
    strategy::Entity& replacement = create(
        session, definitions, "Replacement hub", "command_hub", {0.0F, 0.0F, 0.0F});
    const strategy::EntityId replacementId = replacement.id;
    for (const strategy::EntityId id : {firstId, secondId, thirdId})
        valid = session.submit({1, sequence++, strategy::StopUnitCommand{id}}) && valid;
    session.advanceTicks();
    strategy::Entity& projectA = create(
        session, definitions, "Project A", "basic_generator", {0.0F, 0.0F, 0.0F});
    projectA.construction.emplace();
    projectA.construction.recipeId = "construct.basic_generator";
    projectA.construction.powerRequired = 100.0F;
    const strategy::EntityId projectAId = projectA.id;
    const strategy::EntityId resumableId = assignedIds.empty() ? firstId : assignedIds.front();
    strategy::Entity* resumable = session.world().findEntity(resumableId);
    resumable->battery.charge = 98.0F;
    resumable->unitControl.order = strategy::UnitOrderKind::construct;
    resumable->unitControl.orderTarget = projectAId;
    valid = session.submit({1, sequence++, strategy::RechargeCommand{resumableId}}) && valid;
    session.advanceTicks();
    resumable = session.world().findEntity(resumableId);
    check(!resumable->battery.returningToCharge &&
              resumable->unitControl.order == strategy::UnitOrderKind::construct &&
              session.world().findEntity(projectAId)->construction.powerProgress > 0.0F,
          "resume suspended construction");

    // Two drones can advance independent projects during the same authoritative ticks.
    strategy::Entity& projectB = create(
        session, definitions, "Project B", "basic_generator", {1.0F, 0.0F, 0.0F});
    projectB.construction.emplace();
    projectB.construction.recipeId = "construct.basic_generator";
    projectB.construction.powerRequired = 100.0F;
    const strategy::EntityId projectBId = projectB.id;
    strategy::Entity* secondBuilder = session.world().findEntity(secondId);
    secondBuilder->battery.charge = secondBuilder->battery.capacity;
    valid = session.submit(
        {1, sequence++, strategy::ConstructCommand{secondId, projectBId}}) && valid;
    const float beforeA = session.world().findEntity(projectAId)->construction.powerProgress;
    session.advanceTicks(5);
    check(session.world().findEntity(projectAId)->construction.powerProgress > beforeA &&
              session.world().findEntity(projectBId)->construction.powerProgress >= 10.0F,
          "simultaneous projects");

    // Connect the pad last to ensure the disconnected case above was meaningful.
    valid = session.submit(
        {1, sequence++, strategy::ConnectPowerCommand{replacementId, padId}}) && valid;
    session.advanceTicks();

    if (!valid) std::cerr << "Drone and construction reliability validation failed\n";
    return valid ? 0 : 1;
}
