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
    entity.power.transitCapacity = 100.0F;
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
    const float input = entity.transient.powerBufferStartLastTick + entity.power.generation +
                        entity.transient.powerDischargedLastTick +
                        entity.transient.powerReceivedLastTick;
    const float output = entity.transient.powerConsumedLastTick +
                         entity.transient.powerChargedLastTick +
                         entity.transient.powerSentLastTick + entity.power.transitEnergy +
                         entity.transient.powerCurtailedLastTick;
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
        mix(static_cast<std::uint64_t>(std::llround(entity.power.transitEnergy * 1000.0F)));
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
    world.findEntity(generatorId)->power.transitCapacity = 320.0F;
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
            node.power.transitCapacity = 16.0F;
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
        merged->components.size() != 1 || world.findEntity(isolatedId)->power.supplied != 0.0F) {
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
    propagationSystem.simulate(
        propagationWorld, players, definitions, 0, propagationEvents);
    if (propagationWorld.findEntity(firstNode)->power.transitEnergy != 2.0F ||
        propagationWorld.findEntity(secondNode)->power.transitEnergy != 0.0F ||
        propagationWorld.findEntity(firstNode)->transient.powerParent != propagationGenerator ||
        propagationWorld.findEntity(firstNode)->transient.powerRoot != propagationGenerator ||
        propagationWorld.findEntity(propagationGenerator)->transient.powerSentLastTick != 2.0F ||
        propagationWorld.findEntity(firstNode)->transient.powerReceivedLastTick != 2.0F) {
        std::cerr << "Power crossed more than one connection during its first tick: first="
                  << propagationWorld.findEntity(firstNode)->power.transitEnergy << " second="
                  << propagationWorld.findEntity(secondNode)->power.transitEnergy << '\n';
        return 1;
    }
    propagationSystem.simulate(
        propagationWorld, players, definitions, 1, propagationEvents);
    if (propagationWorld.findEntity(firstNode)->power.transitEnergy != 3.0F ||
        propagationWorld.findEntity(secondNode)->power.transitEnergy != 1.0F ||
        !std::all_of(propagationWorld.entities().begin(), propagationWorld.entities().end(),
                     conservesPower)) {
        std::cerr << "Power did not advance exactly one connection on the second tick\n";
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
    if (branchPriorityWorld.findEntity(lowRelay)->power.transitEnergy != 5.0F ||
        branchPriorityWorld.findEntity(mediumRelay)->power.transitEnergy != 0.0F ||
        branchPriorityWorld.findEntity(lowRelay)->transient.powerBranchPriority !=
            PowerPriority::high) {
        std::cerr << "Downstream high priority did not propagate through its relay branch\n";
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
    // allocation, buffer leaks, and failures that only appear after many propagation ticks.
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
