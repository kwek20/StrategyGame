#include "gameplay/DefinitionRegistry.hpp"
#include "players/PlayerRegistry.hpp"
#include "simulation/PowerGridSystem.hpp"
#include "world/PowerPlacement.hpp"
#include "world/World.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
strategy::EntityId device(strategy::World& world, float x, strategy::PlayerId owner = 1) {
    auto& entity = world.createEntity("Power endpoint", "test_power", owner);
    entity.transform.position = {x, 0, 0};
    entity.power.emplace();
    entity.power.connectionRange = 24;
    entity.power.maximumConnections = 2;
    return entity.id;
}
} // namespace

int strategyTestMain() {
    using namespace strategy;
    World world;
    const auto first = device(world, 0);
    const auto second = device(world, 20);
    const auto enemy = device(world, 5, 2);
    Entity ghost;
    ghost.id = std::numeric_limits<EntityId>::max();
    ghost.authority.owner = 1;
    ghost.power.emplace();
    ghost.power.connectionRange = 12;
    ghost.power.maximumConnections = 2;
    ghost.construction.emplace();
    ghost.construction.state = BuildingLifecycleState::planned;
    ghost.transform.position = {10, 8, 0};
    require(powerPlacementRange(ghost, *world.findEntity(first), 1) == 12,
            "The smaller range defines the boundary; planned source is eligible");
    require(powerPlacementRange(ghost, *world.findEntity(enemy), 1) == 0,
            "Enemy devices cannot provide placement connections");
    require(findPowerPlacementTarget(world, 1, ghost)->id == first,
            "Equal-distance preview uses the authoritative stable-ID tie break");

    PowerPlacementAssist assist;
    const auto inside = assist.position(world, 1, ghost, true);
    require(assist.anchor == first && inside == ghost.transform.position,
            "Ctrl acquires an in-range endpoint without snapping inside its radius");
    ghost.transform.position = {30, 8, 30};
    const auto clamped = assist.position(world, 1, ghost, true);
    require(clamped.y == 8 && std::hypot(clamped.x, clamped.z) < 12 &&
                std::hypot(clamped.x, clamped.z) > 11.99F &&
                std::abs(clamped.x - clamped.z) < 0.0001F,
            "Dragging beyond the boundary slides radially inside it without changing height");
    ghost.transform.position = clamped;
    require(findPowerPlacementTarget(world, 1, ghost) != nullptr,
            "Clamped position satisfies the actual range predicate");
    require(assist.position(world, 1, ghost, true) == clamped,
            "Repeated preview/click evaluation does not drift");

    world.findEntity(first)->power.enabled = false;
    const auto released = assist.position(world, 1, ghost, true);
    require(assist.anchor == 0 && assist.lockLost && released == ghost.transform.position,
            "Disabled endpoint releases the lock visibly instead of switching silently");
    ghost.transform.position = {20, 0, 0};
    (void)assist.position(world, 1, ghost, true);
    require(assist.anchor == 0, "Lost lock waits for Ctrl release even beside another endpoint");
    (void)assist.position(world, 1, ghost, false);
    (void)assist.position(world, 1, ghost, true);
    require(assist.anchor == second && !assist.lockLost,
            "Releasing and pressing Ctrl acquires another endpoint");
    world.findEntity(second)->power.connections = {123, 456};
    (void)assist.position(world, 1, ghost, true);
    require(assist.lockLost && !findPowerPlacementTarget(world, 1, ghost),
            "Full connection slots invalidate both assistance and selection");
    world.findEntity(second)->power.connections.clear();
    world.findEntity(second)->construction.emplace();
    world.findEntity(second)->construction.state = BuildingLifecycleState::planned;
    require(findPowerPlacementTarget(world, 1, ghost)->id == second,
            "Planned endpoints reserve connections for queued pole chains");
    world.findEntity(second)->construction.reset();

    (void)assist.position(world, 1, ghost, false);
    ghost.transform.position = {100, 0, 0};
    require(assist.position(world, 1, ghost, true) == ghost.transform.position && !assist.anchor,
            "Holding Ctrl outside all ranges leaves placement free");
    require(findPowerPlacementTarget(world, 1, ghost, false)->id == second,
            "Out-of-range feedback can identify the nearest eligible endpoint");
    ghost.transform.position = {25, 0, 0};
    (void)assist.position(world, 1, ghost, true);
    require(assist.anchor == second, "Entering range with Ctrl held acquires the first valid link");

    const auto builtId = device(world, 25);
    auto* built = world.findEntity(builtId);
    built->power.connectionRange = ghost.power.connectionRange;
    const auto predicted = findPowerPlacementTarget(world, 1, *built)->id;
    PowerGridSystem grid;
    require(grid.connectNearestForPlacement(world, 1, builtId) == predicted &&
                world.findEntity(predicted)->power.connections.back() == builtId,
            "Authoritative construction connects reciprocally to the previewed endpoint");

    World mixedRanges;
    const auto shortRange = device(mixedRanges, 0);
    const auto longRange = device(mixedRanges, 8);
    mixedRanges.findEntity(shortRange)->power.connectionRange = 2;
    ghost.transform.position = {3, 0, 0};
    const auto targets = findPowerPlacementTargets(mixedRanges, 1, ghost);
    require(targets.nearest->id == shortRange && targets.inRange->id == longRange,
            "A closer device with insufficient range must not hide a valid farther connection");
    World chain;
    const auto hub = device(chain, 0);
    chain.findEntity(hub)->power.generation = 10;
    chain.findEntity(hub)->power.transferLimit = 50;
    PowerGridSystem chainGrid;
    EntityId previous = hub;
    for (int index = 1; index <= 3; ++index) {
        const auto pole = device(chain, index * 22.F);
        auto* entity = chain.findEntity(pole);
        entity->construction.emplace();
        entity->construction.state = BuildingLifecycleState::planned;
        entity->power.transferLimit = 50;
        require(chainGrid.connectNearestForPlacement(chain, 1, pole) == previous,
                "Each queued pole reserves a reciprocal link to the preceding blueprint");
        previous = pole;
    }
    const DefinitionRegistry definitions;
    const PlayerRegistry players;
    std::vector<PowerEvent> events;
    chainGrid.simulate(chain, players, definitions, 0, events);
    require(chainGrid.snapshot(1)->nodes.size() == 1 && chainGrid.snapshot(1)->edges.empty(),
            "Blueprint links cannot transmit electricity before construction completes");
    for (auto& entity : chain.entities())
        if (entity.construction)
            entity.construction.state = BuildingLifecycleState::operational;
    chainGrid.markDirty(1);
    chainGrid.simulate(chain, players, definitions, 1, events);
    require(chainGrid.snapshot(1)->nodes.size() == 4 && chainGrid.snapshot(1)->edges.size() == 3 &&
                chainGrid.snapshot(1)->components.size() == 1,
            "Finishing queued poles activates their reserved network without reconnecting");
    require(chain.destroyEntity(3) &&
                chain.findEntity(2)->power.connections == std::vector<EntityId>{hub} &&
                chain.findEntity(4)->power.connections.empty(),
            "Cancelling a middle blueprint frees reciprocal connection slots");
    Entity pole;
    pole.archetype = EntityArchetypeId{"electricity_pole"};
    definitions.initializeEntity(pole);
    require(pole.vision &&
                effectiveSightRange(pole.vision.sightRange, 50, pole.vision.heightBonus) == 2,
            "Pole vision stays four units across even on elevated terrain");
    for (const auto& id : definitions.matchRules().buildPalette) {
        Entity building;
        building.archetype = EntityArchetypeId{id};
        definitions.initializeEntity(building);
        require(building.vision && building.vision.sightRange > 0,
                "Every buildable building has local vision");
    }
    return 0;
}
