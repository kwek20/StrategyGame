#include "gameplay/DefinitionRegistry.hpp"
#include "players/PlayerRegistry.hpp"
#include "simulation/PowerGridSystem.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

strategy::Entity& addPowerDevice(strategy::World& world,
                                 strategy::PlayerId owner,
                                 float generation,
                                 float demand) {
    strategy::Entity& entity = world.createEntity("Power device", "test_power_device", owner);
    entity.power.emplace();
    entity.power.generation = generation;
    entity.power.demand = demand;
    entity.power.transferLimit = 100.0F;
    entity.power.connectionRange = 100.0F;
    entity.power.maximumConnections = 8;
    entity.power.enabled = true;
    return entity;
}

void connect(strategy::Entity& first, strategy::Entity& second) {
    first.power.connections.push_back(second.id);
    second.power.connections.push_back(first.id);
}

bool conservesPower(const strategy::Entity& entity) {
    if (!entity.power) return true;
    const float input = entity.transient.powerGeneratedLastTick +
                        entity.transient.powerDischargedLastTick +
                        entity.transient.powerReceivedLastTick;
    const float output = entity.transient.powerConsumedLastTick +
                         entity.transient.powerChargedLastTick +
                         entity.transient.powerSentLastTick;
    return std::abs(input - output) < 0.001F;
}

std::uint64_t powerFingerprint(const strategy::World& world) {
    std::uint64_t result = 1469598103934665603ULL;
    const auto mix = [&result](std::uint64_t value) {
        result ^= value;
        result *= 1099511628211ULL;
    };
    for (const strategy::Entity& entity : world.entities()) {
        if (!entity.power) continue;
        mix(entity.id);
        mix(static_cast<std::uint64_t>(std::llround(entity.power.supplied * 1000.0F)));
        mix(static_cast<std::uint64_t>(std::llround(entity.power.stored * 1000.0F)));
        mix(entity.transient.powerRoot);
        mix(entity.transient.powerParent);
    }
    return result;
}

strategy::World makeStressGrid(std::size_t branchCount, std::size_t branchLength) {
    using namespace strategy;
    World world;
    const EntityId generatorId = addPowerDevice(world, 1, 160.0F, 0.0F).id;
    world.findEntity(generatorId)->power.transferLimit = 160.0F;
    world.findEntity(generatorId)->power.maximumConnections =
        static_cast<std::uint32_t>(branchCount);

    for (std::size_t branch = 0; branch < branchCount; ++branch) {
        EntityId previous = generatorId;
        for (std::size_t depth = 0; depth < branchLength; ++depth) {
            Entity& node = addPowerDevice(world, 1, 0.0F,
                                          depth + 1 == branchLength ? 2.0F : 0.0F);
            node.transform.position = {static_cast<float>(branch * 4), 0.0F,
                                       static_cast<float>((depth + 1) * 4)};
            node.power.transferLimit = 8.0F;
            node.power.maximumConnections = 3;
            node.power.priority = static_cast<PowerPriority>(branch % 3);
            connect(*world.findEntity(previous), node);
            previous = node.id;
        }
    }
    return world;
}

} // namespace

int strategyTestMain() {
    using namespace strategy;

    DefinitionRegistry definitions;
    PlayerRegistry players;
    World world;
    PowerGridSystem system;
    std::vector<PowerEvent> events;

    // Equal-hop paths from one generator prefer total squared segment length over parent ID.
    // With equal costs, compare the whole root-to-target ID sequence, not just the last parent.
    for (const bool equalCost : {false, true}) {
        World routes;
        const auto generator = addPowerDevice(routes, 1, 10.0F, 0.0F).id;
        const auto firstBranch = addPowerDevice(routes, 1, 0.0F, 0.0F).id;
        const auto secondBranch = addPowerDevice(routes, 1, 0.0F, 0.0F).id;
        EntityId expectedParent = secondBranch;
        EntityId target = 0;
        if (equalCost) {
            const auto secondParent = addPowerDevice(routes, 1, 0.0F, 0.0F).id;
            const auto firstParent = addPowerDevice(routes, 1, 0.0F, 0.0F).id;
            target = addPowerDevice(routes, 1, 0.0F, 4.0F).id;
            routes.findEntity(firstBranch)->transform.position = {1, 0, 1};
            routes.findEntity(secondBranch)->transform.position = {1, 0, -1};
            routes.findEntity(firstParent)->transform.position = {2, 0, 1};
            routes.findEntity(secondParent)->transform.position = {2, 0, -1};
            routes.findEntity(target)->transform.position = {3, 0, 0};
            connect(*routes.findEntity(firstBranch), *routes.findEntity(firstParent));
            connect(*routes.findEntity(secondBranch), *routes.findEntity(secondParent));
            connect(*routes.findEntity(firstParent), *routes.findEntity(target));
            connect(*routes.findEntity(secondParent), *routes.findEntity(target));
            expectedParent = firstParent; // [1,2,5,6] precedes [1,3,4,6], despite 5 > 4.
        } else {
            target = addPowerDevice(routes, 1, 0.0F, 4.0F).id;
            routes.findEntity(firstBranch)->transform.position = {0, 0, 10};
            routes.findEntity(secondBranch)->transform.position = {5, 0, 0};
            routes.findEntity(target)->transform.position = {10, 0, 0};
            connect(*routes.findEntity(firstBranch), *routes.findEntity(target));
            connect(*routes.findEntity(secondBranch), *routes.findEntity(target));
        }
        connect(*routes.findEntity(generator), *routes.findEntity(firstBranch));
        connect(*routes.findEntity(generator), *routes.findEntity(secondBranch));
        World reordered;
        reordered.replaceEntities(routes.entities());
        for (auto& entity : reordered.entities())
            std::reverse(entity.power.connections.begin(), entity.power.connections.end());
        PowerGridSystem routeSystem, reorderedSystem;
        std::vector<PowerEvent> routeEvents, reorderedEvents;
        for (std::uint64_t tick = 0; tick < 8; ++tick) {
            routeSystem.simulate(routes, players, definitions, tick, routeEvents);
            reorderedSystem.simulate(reordered, players, definitions, tick, reorderedEvents);
            if (routes.findEntity(target)->transient.powerParent != expectedParent ||
                powerFingerprint(routes) != powerFingerprint(reordered) ||
                !std::all_of(routes.entities().begin(), routes.entities().end(), conservesPower)) {
                std::cerr << "Equal-hop power route cost/ID ordering failed (equalCost="
                          << equalCost << ")\n";
                return 1;
            }
        }
    }

    const EntityId generatorId = addPowerDevice(world, 1, 10.0F, 0.0F).id;
    const EntityId consumerId = addPowerDevice(world, 1, 0.0F, 4.0F).id;
    const EntityId isolatedId = addPowerDevice(world, 1, 0.0F, 1.0F).id;
    connect(*world.findEntity(generatorId), *world.findEntity(consumerId));

    system.simulate(world, players, definitions, 0, events);
    const PowerGridSnapshot* first = system.snapshot(1);
    if (!first || first->nodes.size() != 3 || first->edges.size() != 1 ||
        first->components.size() != 2 || world.findEntity(consumerId)->power.supplied != 4.0F) {
        std::cerr << "Initial power topology snapshot is incorrect\n";
        return 1;
    }
    const std::uint64_t firstRevision = first->revision;

    system.simulate(world, players, definitions, 1, events);
    if (!system.snapshot(1) || system.snapshot(1)->revision != firstRevision) {
        std::cerr << "Clean power topology was rebuilt unnecessarily\n";
        return 1;
    }

    connect(*world.findEntity(consumerId), *world.findEntity(isolatedId));
    system.markDirty(1);
    system.simulate(world, players, definitions, 2, events);
    const PowerGridSnapshot* merged = system.snapshot(1);
    if (!merged || merged->revision <= firstRevision || merged->edges.size() != 2 ||
        merged->components.size() != 1 || world.findEntity(isolatedId)->power.supplied != 1.0F) {
        std::cerr << "Dirty power topology did not rebuild after connection\n";
        return 1;
    }
    const std::uint64_t mergedRevision = merged->revision;

    world.findEntity(consumerId)->power.enabled = false;
    system.markDirty(1);
    system.simulate(world, players, definitions, 3, events);
    const PowerGridSnapshot* split = system.snapshot(1);
    if (!split || split->revision <= mergedRevision || split->nodes.size() != 2 ||
        !split->edges.empty() || split->components.size() != 2 ||
        world.findEntity(isolatedId)->power.supplied != 0.0F) {
        std::cerr << "Disabled relay did not split the explicit topology\n";
        return 1;
    }

    World lifecycleWorld;
    PowerGridSystem lifecycleSystem;
    const EntityId sourceId = addPowerDevice(lifecycleWorld, 1, 10.0F, 0.0F).id;
    const EntityId targetId = addPowerDevice(lifecycleWorld, 1, 0.0F, 5.0F).id;
    const EntityId enemyId = addPowerDevice(lifecycleWorld, 2, 0.0F, 5.0F).id;
    const EntityId noSlotsId = addPowerDevice(lifecycleWorld, 1, 0.0F, 5.0F).id;
    lifecycleWorld.findEntity(noSlotsId)->power.maximumConnections = 0;

    if (lifecycleSystem.connect(lifecycleWorld, 1, sourceId, enemyId) !=
            PowerFailureReason::enemyTarget ||
        lifecycleSystem.connect(lifecycleWorld, 1, sourceId, noSlotsId) !=
            PowerFailureReason::connectionLimit) {
        std::cerr << "Power connection failures were not reported explicitly\n";
        return 1;
    }
    lifecycleWorld.findEntity(targetId)->power.enabled = false;
    if (lifecycleSystem.connect(lifecycleWorld, 1, sourceId, targetId) !=
        PowerFailureReason::disabled) {
        std::cerr << "Disabled power endpoint was accepted\n";
        return 1;
    }
    lifecycleWorld.findEntity(targetId)->power.enabled = true;
    if (lifecycleSystem.connect(lifecycleWorld, 1, sourceId, targetId) !=
            PowerFailureReason::none ||
        lifecycleSystem.connect(lifecycleWorld, 1, sourceId, targetId) !=
            PowerFailureReason::alreadyConnected) {
        std::cerr << "Valid or duplicate power connection lifecycle failed\n";
        return 1;
    }
    if (!lifecycleWorld.destroyEntity(sourceId) ||
        !lifecycleWorld.findEntity(targetId)->power.connections.empty()) {
        std::cerr << "Destroyed power device left a reciprocal connection behind\n";
        return 1;
    }

    World placementWorld;
    PowerGridSystem placementSystem;
    Entity& placementSource = addPowerDevice(placementWorld, 1, 0.0F, 0.0F);
    const EntityId placementSourceId = placementSource.id;
    placementSource.construction.emplace();
    placementSource.construction.state = BuildingLifecycleState::planned;
    placementSource.transform.position = {0.0F, 0.0F, 0.0F};
    Entity& farther = addPowerDevice(placementWorld, 1, 10.0F, 0.0F);
    const EntityId fartherId = farther.id;
    farther.transform.position = {12.0F, 0.0F, 0.0F};
    Entity& nearer = addPowerDevice(placementWorld, 1, 10.0F, 0.0F);
    const EntityId nearerId = nearer.id;
    nearer.transform.position = {6.0F, 0.0F, 0.0F};
    Entity& enemy = addPowerDevice(placementWorld, 2, 10.0F, 0.0F);
    const EntityId placementEnemyId = enemy.id;
    enemy.transform.position = {2.0F, 0.0F, 0.0F};
    if (placementSystem.connectNearestForPlacement(placementWorld, 1, placementSourceId) != nearerId ||
        placementWorld.findEntity(placementSourceId)->power.connections !=
            std::vector<EntityId>{nearerId} ||
        placementWorld.findEntity(nearerId)->power.connections !=
            std::vector<EntityId>{placementSourceId} ||
        !placementWorld.findEntity(fartherId)->power.connections.empty() ||
        !placementWorld.findEntity(placementEnemyId)->power.connections.empty()) {
        std::cerr << "Placed power building did not connect to its nearest eligible device\n";
        return 1;
    }

    World propagationWorld;
    PowerGridSystem propagationSystem;
    std::vector<PowerEvent> propagationEvents;
    const EntityId propagationGenerator =
        addPowerDevice(propagationWorld, 1, 10.0F, 0.0F).id;
    const EntityId firstNode = addPowerDevice(propagationWorld, 1, 0.0F, 0.0F).id;
    const EntityId secondNode = addPowerDevice(propagationWorld, 1, 0.0F, 0.0F).id;
    propagationWorld.findEntity(propagationGenerator)->power.transferLimit = 2.0F;
    propagationWorld.findEntity(firstNode)->power.transferLimit = 1.0F;
    connect(*propagationWorld.findEntity(propagationGenerator),
            *propagationWorld.findEntity(firstNode));
    connect(*propagationWorld.findEntity(firstNode),
            *propagationWorld.findEntity(secondNode));
    propagationWorld.findEntity(secondNode)->power.demand = 5.0F;
    propagationSystem.simulate(propagationWorld, players, definitions, 0, propagationEvents);
    if (propagationWorld.findEntity(secondNode)->power.supplied != 1.0F ||
        propagationWorld.findEntity(propagationGenerator)->transient.powerSentLastTick != 1.0F ||
        !std::all_of(propagationWorld.entities().begin(), propagationWorld.entities().end(), conservesPower)) {
        std::cerr << "Whole-grid allocation did not supply a two-hop consumer immediately\n";
        return 1;
    }
    propagationWorld.findEntity(firstNode)->power.transferLimit = 0.0F;
    propagationSystem.simulate(propagationWorld, players, definitions, 1, propagationEvents);
    if (propagationWorld.findEntity(secondNode)->power.supplied != 0.0F ||
        propagationWorld.findEntity(firstNode)->transient.powerSentLastTick != 0.0F) {
        std::cerr << "Zero output capacity forwarded energy or retained a transit buffer\n";
        return 1;
    }

    World disabledStorageWorld;
    PowerGridSystem disabledStorageSystem;
    std::vector<PowerEvent> disabledStorageEvents;
    const EntityId storageGenerator = addPowerDevice(disabledStorageWorld, 1, 5.0F, 0.0F).id;
    const EntityId disabledStorage = addPowerDevice(disabledStorageWorld, 1, 0.0F, 0.0F).id;
    const EntityId storageConsumer = addPowerDevice(disabledStorageWorld, 1, 0.0F, 5.0F).id;
    Entity* disabledStorageEntity = disabledStorageWorld.findEntity(disabledStorage);
    disabledStorageEntity->power.storageCapacity = 10.0F;
    disabledStorageEntity->power.stored = 10.0F;
    disabledStorageEntity->power.dischargeEnabled = false;
    disabledStorageEntity->power.transferLimit = 5.0F;
    disabledStorageWorld.findEntity(storageGenerator)->power.transferLimit = 5.0F;
    connect(*disabledStorageWorld.findEntity(storageGenerator), *disabledStorageEntity);
    connect(*disabledStorageEntity, *disabledStorageWorld.findEntity(storageConsumer));
    disabledStorageSystem.simulate(
        disabledStorageWorld, players, definitions, 0, disabledStorageEvents);
    disabledStorageSystem.simulate(
        disabledStorageWorld, players, definitions, 1, disabledStorageEvents);
    if (disabledStorageEntity->transient.powerRoot != storageGenerator ||
        disabledStorageEntity->transient.powerParent != storageGenerator ||
        disabledStorageWorld.findEntity(storageConsumer)->power.supplied != 5.0F ||
        disabledStorageEntity->power.stored != 10.0F ||
        !std::all_of(disabledStorageWorld.entities().begin(),
                     disabledStorageWorld.entities().end(), conservesPower)) {
        std::cerr << "Discharge-disabled storage incorrectly became a source root\n";
        return 1;
    }

    World priorityWorld;
    PowerGridSystem prioritySystem;
    std::vector<PowerEvent> priorityEvents;
    const EntityId priorityGenerator = addPowerDevice(priorityWorld, 1, 5.0F, 0.0F).id;
    const EntityId lowConsumer = addPowerDevice(priorityWorld, 1, 0.0F, 5.0F).id;
    const EntityId mediumConsumer = addPowerDevice(priorityWorld, 1, 0.0F, 5.0F).id;
    const EntityId highConsumer = addPowerDevice(priorityWorld, 1, 0.0F, 5.0F).id;
    priorityWorld.findEntity(priorityGenerator)->power.transferLimit = 5.0F;
    priorityWorld.findEntity(lowConsumer)->power.priority = PowerPriority::low;
    priorityWorld.findEntity(mediumConsumer)->power.priority = PowerPriority::medium;
    priorityWorld.findEntity(highConsumer)->power.priority = PowerPriority::high;
    connect(*priorityWorld.findEntity(priorityGenerator), *priorityWorld.findEntity(lowConsumer));
    connect(*priorityWorld.findEntity(priorityGenerator),
            *priorityWorld.findEntity(mediumConsumer));
    connect(*priorityWorld.findEntity(priorityGenerator), *priorityWorld.findEntity(highConsumer));
    prioritySystem.simulate(priorityWorld, players, definitions, 0, priorityEvents);
    if (priorityWorld.findEntity(highConsumer)->power.supplied != 5.0F ||
        priorityWorld.findEntity(mediumConsumer)->power.supplied != 0.0F ||
        priorityWorld.findEntity(lowConsumer)->power.supplied != 0.0F) {
        std::cerr << "High-priority branch did not receive constrained power first\n";
        return 1;
    }

    for (const float availablePower : {6.0F, 11.0F, 15.0F}) {
        priorityWorld.findEntity(priorityGenerator)->power.generation = availablePower;
        priorityWorld.findEntity(priorityGenerator)->power.transferLimit = availablePower;
        prioritySystem.simulate(priorityWorld, players, definitions, 1, priorityEvents);
        if (priorityWorld.findEntity(highConsumer)->power.supplied != 5 ||
            priorityWorld.findEntity(mediumConsumer)->power.supplied != std::min(5.0F, availablePower - 5) ||
            priorityWorld.findEntity(lowConsumer)->power.supplied != std::max(0.0F, availablePower - 10)) {
            std::cerr << "Medium/low priority order or fully supplied grid failed\n";
            return 1;
        }
    }

    World branchPriorityWorld;
    PowerGridSystem branchPrioritySystem;
    std::vector<PowerEvent> branchPriorityEvents;
    const EntityId branchGenerator = addPowerDevice(branchPriorityWorld, 1, 5.0F, 0.0F).id;
    const EntityId lowRelay = addPowerDevice(branchPriorityWorld, 1, 0.0F, 0.0F).id;
    const EntityId mediumRelay = addPowerDevice(branchPriorityWorld, 1, 0.0F, 0.0F).id;
    const EntityId highLeaf = addPowerDevice(branchPriorityWorld, 1, 0.0F, 5.0F).id;
    const EntityId mediumLeaf = addPowerDevice(branchPriorityWorld, 1, 0.0F, 5.0F).id;
    branchPriorityWorld.findEntity(branchGenerator)->power.transferLimit = 5.0F;
    branchPriorityWorld.findEntity(lowRelay)->power.priority = PowerPriority::low;
    branchPriorityWorld.findEntity(mediumRelay)->power.priority = PowerPriority::medium;
    branchPriorityWorld.findEntity(highLeaf)->power.priority = PowerPriority::high;
    branchPriorityWorld.findEntity(mediumLeaf)->power.priority = PowerPriority::medium;
    connect(*branchPriorityWorld.findEntity(branchGenerator),
            *branchPriorityWorld.findEntity(lowRelay));
    connect(*branchPriorityWorld.findEntity(branchGenerator),
            *branchPriorityWorld.findEntity(mediumRelay));
    connect(*branchPriorityWorld.findEntity(lowRelay),
            *branchPriorityWorld.findEntity(highLeaf));
    connect(*branchPriorityWorld.findEntity(mediumRelay),
            *branchPriorityWorld.findEntity(mediumLeaf));
    branchPrioritySystem.simulate(
        branchPriorityWorld, players, definitions, 0, branchPriorityEvents);
    if (branchPriorityWorld.findEntity(highLeaf)->power.supplied != 5.0F ||
        branchPriorityWorld.findEntity(mediumLeaf)->power.supplied != 0.0F ||
        branchPriorityWorld.findEntity(lowRelay)->transient.powerBranchPriority !=
            PowerPriority::high) {
        std::cerr << "Downstream high priority did not propagate through its relay branch\n";
        return 1;
    }

    // Local use precedes forwarding, but consumption and output are independent switches.
    World chain;
    PowerGridSystem chainSystem;
    const auto chainSource = addPowerDevice(chain, 1, 6, 0).id;
    const auto chainLow = addPowerDevice(chain, 1, 0, 4).id;
    const auto chainHigh = addPowerDevice(chain, 1, 0, 5).id;
    chain.findEntity(chainLow)->power.priority = PowerPriority::low;
    chain.findEntity(chainHigh)->power.priority = PowerPriority::high;
    connect(*chain.findEntity(chainSource), *chain.findEntity(chainLow));
    connect(*chain.findEntity(chainLow), *chain.findEntity(chainHigh));
    chainSystem.simulate(chain, players, definitions, 0, events);
    if (chain.findEntity(chainLow)->power.supplied != 4 || chain.findEntity(chainHigh)->power.supplied != 2) {
        std::cerr << "Downstream priority bypassed upstream consumption\n";
        return 1;
    }
    chain.findEntity(chainLow)->power.consumptionEnabled = false;
    chainSystem.simulate(chain, players, definitions, 1, events);
    if (chain.findEntity(chainLow)->power.supplied != 0 || chain.findEntity(chainHigh)->power.supplied != 5) {
        std::cerr << "Consumption-disabled device did not forward power\n";
        return 1;
    }
    chain.findEntity(chainLow)->power.consumptionEnabled = true;
    chain.findEntity(chainLow)->power.outputEnabled = false;
    chainSystem.simulate(chain, players, definitions, 2, events);
    if (chain.findEntity(chainLow)->power.supplied != 4 || chain.findEntity(chainHigh)->power.supplied != 0 ||
        !std::all_of(chain.entities().begin(), chain.entities().end(), conservesPower)) {
        std::cerr << "Output-disabled device lost intake or forwarded power\n";
        return 1;
    }

    // Same-priority branches share equally and return unused shares to requesting branches.
    World sharing;
    PowerGridSystem sharingSystem;
    const auto sharingSource = addPowerDevice(sharing, 1, 6, 0).id;
    const auto sharingA = addPowerDevice(sharing, 1, 0, 5).id;
    const auto sharingB = addPowerDevice(sharing, 1, 0, 5).id;
    connect(*sharing.findEntity(sharingSource), *sharing.findEntity(sharingA));
    connect(*sharing.findEntity(sharingSource), *sharing.findEntity(sharingB));
    sharingSystem.simulate(sharing, players, definitions, 0, events);
    if (sharing.findEntity(sharingA)->power.supplied != 3 || sharing.findEntity(sharingB)->power.supplied != 3) {
        std::cerr << "Equal-priority branches did not split output equally\n";
        return 1;
    }
    sharing.findEntity(sharingA)->power.demand = 1;
    sharingSystem.simulate(sharing, players, definitions, 1, events);
    if (sharing.findEntity(sharingA)->power.supplied != 1 || sharing.findEntity(sharingB)->power.supplied != 5) {
        std::cerr << "Unused branch share was not redistributed\n";
        return 1;
    }

    // Storage remains empty even when generation is spare but a transfer bottleneck starves a load.
    World storage;
    PowerGridSystem storageSystem;
    const auto hub = storage.createEntity("Storage hub", "command_hub", 1).id;
    definitions.initializeEntity(*storage.findEntity(hub));
    storage.findEntity(hub)->power.stored = 0;
    const auto storageLoad = addPowerDevice(storage, 1, 0, 11).id;
    connect(*storage.findEntity(hub), *storage.findEntity(storageLoad));
    storageSystem.simulate(storage, players, definitions, 0, events);
    if (storage.findEntity(hub)->power.stored != 0 || storage.findEntity(storageLoad)->power.supplied != 10) {
        std::cerr << "Storage charged ahead of an unsatisfied consumer\n";
        return 1;
    }
    storage.findEntity(storageLoad)->power.demand = 6;
    storage.findEntity(hub)->power.transferLimit = 5;
    storageSystem.simulate(storage, players, definitions, 1, events);
    if (storage.findEntity(hub)->power.stored != 0 || storage.findEntity(storageLoad)->power.supplied != 5) {
        std::cerr << "Storage charged while a transfer bottleneck starved a consumer\n";
        return 1;
    }
    storage.findEntity(hub)->power.transferLimit = 50;
    storageSystem.simulate(storage, players, definitions, 2, events);
    if (storage.findEntity(hub)->power.stored != 2 || storage.findEntity(storageLoad)->power.supplied != 6 ||
        storage.findEntity(hub)->transient.powerDischargedLastTick != 0 ||
        !std::all_of(storage.entities().begin(), storage.entities().end(), conservesPower)) {
        std::cerr << "Surplus storage charge rate or conservation failed\n";
        return 1;
    }
    storage.findEntity(hub)->power.stored = 199;
    storageSystem.simulate(storage, players, definitions, 3, events);
    if (storage.findEntity(hub)->power.stored != 200) {
        std::cerr << "Storage exceeded capacity\n";
        return 1;
    }

    World sourceOnly;
    PowerGridSystem sourceOnlySystem;
    const auto strongSource = addPowerDevice(sourceOnly, 1, 10, 0).id;
    const auto weakSource = addPowerDevice(sourceOnly, 1, 1, 0).id;
    const auto behindSource = addPowerDevice(sourceOnly, 1, 0, 5).id;
    const auto isolatedRelay = addPowerDevice(sourceOnly, 1, 0, 0).id;
    connect(*sourceOnly.findEntity(strongSource), *sourceOnly.findEntity(weakSource));
    connect(*sourceOnly.findEntity(weakSource), *sourceOnly.findEntity(behindSource));
    sourceOnlySystem.simulate(sourceOnly, players, definitions, 0, events);
    if (sourceOnly.findEntity(weakSource)->transient.powerReceivedLastTick != 0 ||
        sourceOnly.findEntity(behindSource)->power.supplied != 1 ||
        sourceOnly.findEntity(isolatedRelay)->power.state != PowerOperationalState::offline) {
        std::cerr << "Generator accepted incoming power or an isolated relay appeared powered\n";
        return 1;
    }

    World multiSourceWorld;
    PowerGridSystem multiSourceSystem;
    std::vector<PowerEvent> multiSourceEvents;
    const EntityId distantGenerator = addPowerDevice(multiSourceWorld, 1, 1.0F, 0.0F).id;
    const EntityId nearGenerator = addPowerDevice(multiSourceWorld, 1, 5.0F, 0.0F).id;
    const EntityId sharedConsumer = addPowerDevice(multiSourceWorld, 1, 0.0F, 5.0F).id;
    multiSourceWorld.findEntity(distantGenerator)->transform.position.x = 0.0F;
    multiSourceWorld.findEntity(nearGenerator)->transform.position.x = 10.0F;
    multiSourceWorld.findEntity(sharedConsumer)->transform.position.x = 9.0F;
    connect(*multiSourceWorld.findEntity(distantGenerator),
            *multiSourceWorld.findEntity(sharedConsumer));
    connect(*multiSourceWorld.findEntity(nearGenerator),
            *multiSourceWorld.findEntity(sharedConsumer));
    multiSourceSystem.simulate(multiSourceWorld, players, definitions, 0, multiSourceEvents);
    if (multiSourceWorld.findEntity(sharedConsumer)->power.supplied != 5.0F) {
        std::cerr << "Equal-hop node did not choose the nearer generator\n";
        return 1;
    }

    World fallbackWorld;
    PowerGridSystem fallbackSystem;
    std::vector<PowerEvent> fallbackEvents;
    const EntityId weakNearGenerator = addPowerDevice(fallbackWorld, 1, 1.0F, 0.0F).id;
    const EntityId strongFarGenerator = addPowerDevice(fallbackWorld, 1, 5.0F, 0.0F).id;
    const EntityId fallbackConsumer = addPowerDevice(fallbackWorld, 1, 0.0F, 5.0F).id;
    fallbackWorld.findEntity(weakNearGenerator)->transform.position.x = 0.0F;
    fallbackWorld.findEntity(strongFarGenerator)->transform.position.x = 10.0F;
    fallbackWorld.findEntity(fallbackConsumer)->transform.position.x = 1.0F;
    connect(*fallbackWorld.findEntity(weakNearGenerator),
            *fallbackWorld.findEntity(fallbackConsumer));
    connect(*fallbackWorld.findEntity(strongFarGenerator),
            *fallbackWorld.findEntity(fallbackConsumer));
    fallbackSystem.simulate(fallbackWorld, players, definitions, 0, fallbackEvents);
    if (fallbackWorld.findEntity(fallbackConsumer)->power.supplied != 5.0F ||
        fallbackWorld.findEntity(fallbackConsumer)->transient.powerRoot != strongFarGenerator ||
        fallbackWorld.findEntity(fallbackConsumer)->transient.powerPrimaryRoot != weakNearGenerator ||
        !fallbackWorld.findEntity(fallbackConsumer)->transient.powerFallbackActive) {
        std::cerr << "Surplus fallback generator did not take over a deficient primary territory\n";
        return 1;
    }

    // A repeatable large-grid probe catches accidental quadratic rebuilds, non-deterministic
    // allocation, conservation leaks, and failures that only appear after many simulation ticks.
    constexpr std::size_t stressBranches = 32;
    constexpr std::size_t stressDepth = 8;
    constexpr std::uint32_t stressTicks = 600;
    World stressWorldA = makeStressGrid(stressBranches, stressDepth);
    World stressWorldB = makeStressGrid(stressBranches, stressDepth);
    PowerGridSystem stressSystemA;
    PowerGridSystem stressSystemB;
    std::vector<PowerEvent> stressEventsA;
    std::vector<PowerEvent> stressEventsB;
    const auto stressStart = std::chrono::steady_clock::now();
    for (std::uint32_t tick = 0; tick < stressTicks; ++tick) {
        stressSystemA.simulate(stressWorldA, players, definitions, tick, stressEventsA);
        stressSystemB.simulate(stressWorldB, players, definitions, tick, stressEventsB);
        if (powerFingerprint(stressWorldA) != powerFingerprint(stressWorldB)) {
            std::cerr << "Large power grids diverged at deterministic tick " << tick << '\n';
            return 1;
        }
        for (const Entity& entity : stressWorldA.entities()) {
            if (!conservesPower(entity)) {
                std::cerr << "Large-grid power conservation failed for entity " << entity.id
                          << " at tick " << tick << '\n';
                return 1;
            }
        }

        // Split and later merge a busy branch. Both grids receive the same mutation so the probe
        // also validates revisioned topology rebuilding under sustained allocation.
        if (tick == 200 || tick == 350) {
            Entity& generatorA = stressWorldA.entities().front();
            Entity& generatorB = stressWorldB.entities().front();
            const EntityId branchA = stressWorldA.entities().at(1).id;
            const EntityId branchB = stressWorldB.entities().at(1).id;
            if (tick == 200) {
                if (stressSystemA.disconnect(stressWorldA, 1, generatorA.id, branchA) !=
                        PowerFailureReason::none ||
                    stressSystemB.disconnect(stressWorldB, 1, generatorB.id, branchB) !=
                        PowerFailureReason::none) {
                    std::cerr << "Large-grid split command failed\n";
                    return 1;
                }
            } else if (stressSystemA.connect(stressWorldA, 1, generatorA.id, branchA) !=
                               PowerFailureReason::none ||
                       stressSystemB.connect(stressWorldB, 1, generatorB.id, branchB) !=
                               PowerFailureReason::none) {
                std::cerr << "Large-grid merge command failed\n";
                return 1;
            }
        }
    }
    const double stressMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - stressStart).count();
    std::cout << "Power stress profile: " << stressWorldA.entities().size() << " devices, "
              << stressTicks << " ticks, two deterministic worlds, " << stressMilliseconds
              << " ms\n";
    // This is deliberately a regression tripwire rather than a tight benchmark. Normal timing
    // variance should not fail CI, while a multi-second-per-tick regression should.
    if (stressMilliseconds > 30000.0) {
        std::cerr << "Large-grid power profile exceeded the 30 second regression budget\n";
        return 1;
    }

    return 0;
}
