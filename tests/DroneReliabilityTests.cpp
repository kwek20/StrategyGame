#include "gameplay/DefinitionRegistry.hpp"
#include "simulation/Command.hpp"
#include "simulation/GameSession.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
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
        session.world().findEntity(id)->battery.charge =
            session.world().findEntity(id)->battery.capacity;
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
    secondBuilder = session.world().findEntity(secondId);
    secondBuilder->gatherer.sourceTarget = 999;
    secondBuilder->gatherer.deliveryTarget = replacementId;
    secondBuilder->gatherer.repeatGathering = true;
    valid = session.submit(
        {1, sequence++, strategy::ConstructCommand{secondId, projectBId}}) && valid;
    session.advanceTicks();
    secondBuilder = session.world().findEntity(secondId);
    check(secondBuilder && !secondBuilder->gatherer.repeatGathering &&
              secondBuilder->gatherer.sourceTarget == 0 &&
              secondBuilder->gatherer.deliveryTarget == 0,
          "construction clears gather loop");
    const float beforeA = session.world().findEntity(projectAId)->construction.powerProgress;
    session.advanceTicks(5);
    check(session.world().findEntity(projectAId)->construction.powerProgress > beforeA &&
              session.world().findEntity(projectBId)->construction.powerProgress >= 10.0F,
          "simultaneous projects");

    // Connect the pad last to ensure the disconnected case above was meaningful.
    valid = session.submit(
        {1, sequence++, strategy::ConnectPowerCommand{replacementId, padId}}) && valid;
    session.advanceTicks();

    // Exhaustion is self-triggering even without a movement/construction command.
    strategy::GameSession idleRecovery{definitions, 0x1D1ECAFEU};
    idleRecovery.replaceWorld({}, 0x1D1ECAFEU);
    strategy::Entity& idleHub = create(
        idleRecovery, definitions, "Idle recovery hub", "command_hub", {0.0F, 0.0F, 0.0F});
    const strategy::EntityId idleHubId = idleHub.id;
    strategy::Entity& idleEmpty = create(
        idleRecovery, definitions, "Idle empty drone", "construction_drone", {8.0F, 6.0F, 0.0F});
    const strategy::EntityId idleEmptyId = idleEmpty.id;
    idleEmpty.battery.charge = 0.0F;
    idleRecovery.advanceTicks();
    const strategy::Entity* recoveringIdle = idleRecovery.world().findEntity(idleEmptyId);
    check(recoveringIdle && recoveringIdle->battery.returningToCharge &&
              recoveringIdle->battery.chargerTarget == idleHubId,
          "idle exhaustion starts charger return");

    // A positive remainder below the next work-step cost must suspend the task, including
    // when no powered charger is available, then resume it once charging succeeds.
    idleRecovery.world().destroyEntity(idleEmptyId);
    std::uint64_t recoverySequence = 1;
    for (const auto order : {strategy::UnitOrderKind::construct, strategy::UnitOrderKind::repair}) {
        const auto workerId = create(idleRecovery, definitions, "Fractional worker",
                                     "construction_drone", {0, 6, 0}).id;
        const auto targetId = create(idleRecovery, definitions, "Fractional work target",
                                     "basic_generator", {0, 0, 0}).id;
        auto* target = idleRecovery.world().findEntity(targetId);
        if (order == strategy::UnitOrderKind::construct) {
            target->construction.emplace();
            target->construction.recipeId = "construct.basic_generator";
            target->construction.powerRequired = 1000.0F;
        } else {
            target->health.current = target->health.maximum - 10.0F;
        }
        const float initialProgress = order == strategy::UnitOrderKind::construct
                                          ? target->construction.powerProgress : target->health.current;
        idleRecovery.world().findEntity(workerId)->battery.charge = 0.5F;
        if (order == strategy::UnitOrderKind::construct)
            check(idleRecovery.submit({1, recoverySequence++, strategy::ConstructCommand{workerId, targetId}}),
                  "submit fractional construction");
        else
            check(idleRecovery.submit({1, recoverySequence++, strategy::RepairCommand{workerId, targetId}}),
                  "submit fractional repair");
        idleRecovery.advanceTicks();
        const auto* worker = idleRecovery.world().findEntity(workerId);
        target = idleRecovery.world().findEntity(targetId);
        const float stalledProgress = order == strategy::UnitOrderKind::construct
                                          ? target->construction.powerProgress : target->health.current;
        check(worker->battery.returningToCharge && worker->battery.hasSuspendedOrder &&
                  worker->battery.suspendedOrder == order && worker->battery.suspendedTarget == targetId &&
                  worker->battery.charge == 0.5F && stalledProgress == initialProgress,
              order == strategy::UnitOrderKind::construct ? "fractional construction suspends"
                                                         : "fractional repair suspends");
        check(idleRecovery.submit({1, recoverySequence++, strategy::SetPowerEnabledCommand{idleHubId, false}}),
              "disable fractional worker charger");
        idleRecovery.advanceTicks();
        worker = idleRecovery.world().findEntity(workerId);
        check(worker->unitControl.order == strategy::UnitOrderKind::stranded &&
                  worker->battery.suspendedOrder == order && worker->battery.suspendedTarget == targetId,
              "fractional work survives unavailable charger");
        check(idleRecovery.submit({1, recoverySequence++, strategy::SetPowerEnabledCommand{idleHubId, true}}),
              "restore fractional worker charger");
        idleRecovery.advanceTicks(60);
        worker = idleRecovery.world().findEntity(workerId);
        target = idleRecovery.world().findEntity(targetId);
        const float resumedProgress = order == strategy::UnitOrderKind::construct
                                          ? target->construction.powerProgress : target->health.current;
        check(!worker->battery.returningToCharge && !worker->battery.hasSuspendedOrder &&
                  worker->unitControl.order == order && worker->unitControl.orderTarget == targetId &&
                  resumedProgress > initialProgress,
              order == strategy::UnitOrderKind::construct ? "fractional construction resumes"
                                                         : "fractional repair resumes");
        idleRecovery.world().destroyEntity(workerId);
        idleRecovery.world().destroyEntity(targetId);
    }

    // Exercise data-driven rates without changing the production catalogue.
    std::ifstream unitsFile("assets/gameplay/units.json");
    std::string customUnits((std::istreambuf_iterator<char>(unitsFile)), {});
    const auto repairBegin = customUnits.find("\"repair\": {");
    const auto repairEnd = customUnits.find('}', repairBegin);
    customUnits.replace(repairBegin, repairEnd - repairBegin + 1,
                        R"("repair": {"healthPerTick":7,"batteryPerTick":2.5})");
    const auto fixture = std::filesystem::temp_directory_path() / "strategy_custom_repair_units.json";
    { std::ofstream output(fixture); output << customUnits; }
    const strategy::DefinitionRegistry customDefinitions{fixture};
    std::filesystem::remove(fixture);
    strategy::GameSession customRepair{customDefinitions, 123U};
    customRepair.replaceWorld({}, 123U);
    const auto repairerId = create(customRepair, customDefinitions, "Repairer", "construction_drone", {0,6,0}).id;
    const auto damagedId = create(customRepair, customDefinitions, "Damaged", "basic_generator", {0,0,0}).id;
    auto* repairer = customRepair.world().findEntity(repairerId);
    auto* damaged = customRepair.world().findEntity(damagedId);
    damaged->health.current = damaged->health.maximum - 10;
    repairer->battery.charge = 10;
    check(customRepair.submit({1, 1, strategy::RepairCommand{repairerId, damagedId}}), "custom repair command");
    customRepair.advanceTicks();
    check(damaged->health.current == damaged->health.maximum - 3 && repairer->battery.charge == 7.5F,
          "custom repair healing and battery cost");
    customRepair.advanceTicks();
    check(damaged->health.current == damaged->health.maximum && repairer->battery.charge == 5,
          "final partial repair caps healing and charges full step");
    customRepair.advanceTicks();
    check(repairer->battery.charge == 5, "full target costs no battery");
    damaged->health.current -= 10;
    repairer->battery.charge = 2;
    customRepair.advanceTicks();
    check(repairer->battery.returningToCharge && repairer->battery.suspendedOrder == strategy::UnitOrderKind::repair &&
          damaged->health.current == damaged->health.maximum - 10 && repairer->battery.charge == 2,
          "custom unaffordable repair suspends without spending");
    // A flying battery-powered entity without a repair definition cannot start or retain repair.
    repairer->archetype = strategy::EntityArchetypeId{"worker"};
    repairer->battery.returningToCharge = false;
    repairer->unitControl.order = strategy::UnitOrderKind::idle;
    check(customRepair.submit({1, 2, strategy::RepairCommand{repairerId, damagedId}}), "non-repair unit command queued");
    customRepair.advanceTicks();
    check(repairer->unitControl.order == strategy::UnitOrderKind::idle, "definition required for repair command");
    repairer->unitControl.order = strategy::UnitOrderKind::repair;
    repairer->unitControl.orderTarget = damagedId;
    customRepair.advanceTicks();
    check(repairer->unitControl.order == strategy::UnitOrderKind::idle, "definition required for restored repair order");

    if (!valid) std::cerr << "Drone and construction reliability validation failed\n";
    return valid ? 0 : 1;
}
