#include "TerrainLevelingFixture.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "simulation/CombatRules.hpp"
#include "simulation/GameSession.hpp"
#include "simulation/UnitMovement.hpp"
#include "world/Collision.hpp"
#include "world/Navigation.hpp"
#include "world/World.hpp"

#include <cmath>
#include <stdexcept>
#include <iostream>

namespace {
void require(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
strategy::EntityId spawn(strategy::World& world, const strategy::DefinitionRegistry& definitions,
                         const char* type, glm::vec2 point) {
    const auto id = world.createEntity(type, type, 1).id;
    auto* entity = world.findEntity(id);
    definitions.initializeEntity(*entity);
    entity->transform.position = {point.x, 0, point.y};
    return id;
}
} // namespace

int strategyTestMain() {
    using namespace strategy;
    DefinitionRegistry definitions;
    auto flat = test::levelingFixture(0);
    World world;
    std::vector<EntityId> units;
    for (int i = 0; i < 6; ++i)
        units.push_back(spawn(world, definitions, "light_tank", {-20.F + i * 6, -20}));
    auto* turning = world.findEntity(units.front());
    const auto curve = turnLimitedMovement(*turning, {.70710678F, .70710678F}, 90, 1.F / 30);
    require(glm::length(curve) > .4F && turning->transform.rotationDegrees.y > 0,
            "A moderate RTS heading change drives through a curve instead of stopping at five degrees");
    turning->transform.rotationDegrees.y = 0;
    require(glm::length(turnLimitedMovement(*turning, {0, -1}, 90, 1.F / 30)) < .001F,
            "A half-turn still pivots before driving");
    Navigation navigation{flat, definitions, 10};
    for (const auto id : units) {
        auto* unit = world.findEntity(id);
        auto route = findUnitArrivalPath(navigation, world, flat, definitions, 10, *unit, {0, 0, 15});
        require(!route.empty(), "Each group member receives a reachable arrival position");
        unit->unitControl.order = UnitOrderKind::move;
        unit->unitControl.hasStrategicDestination = true;
        unit->unitControl.strategicDestination = route.back();
    }
    for (std::size_t a = 0; a < units.size(); ++a)
        for (std::size_t b = a + 1; b < units.size(); ++b)
            require(glm::length(world.findEntity(units[a])->unitControl.strategicDestination -
                                world.findEntity(units[b])->unitControl.strategicDestination) >= 9.3F,
                    "Arrival reservations leave passing lanes between parked tank footprints");
    const auto obstacle = spawn(world, definitions, "storage_silo", {0, 0});
    world.findEntity(obstacle)->transform.rotationDegrees.y = 37;
    require(!clearUnitRouteSegment(world, flat, definitions, *world.findEntity(units[0]), {-10, 0}, {10, 0}) &&
                clearUnitRouteSegment(world, flat, definitions, *world.findEntity(units[0]), {-10, -10}, {10, -10}),
            "Route look-ahead does not cut corners through rotated static obstacles");

    GameSession session{definitions, 7, "france", "brazil", "unassigned", "unassigned", 10,
                        1, 1, TerrainLayoutId{"open_plains"}, nullptr, TerrainMode::v2};
    session.world().replaceEntities({});
    const auto anchor = session.startingAnchors().front();
    units.clear();
    for (int i = 0; i < 6; ++i)
        units.push_back(spawn(session.world(), definitions, "light_tank",
            anchor + glm::vec2{-12.F + (i % 3) * 6.F, -12.F + (i / 3) * 6.F}));
    const glm::vec2 target = anchor + glm::vec2{15, 8};
    for (std::size_t i = 0; i < units.size(); ++i)
        require(session.submit({1, i + 1, MoveUnitCommand{units[i], {target.x, 0, target.y}}}),
                "Submit ordinary group movement envelopes");
    // Includes detours around other members; still bounded to thirty simulation seconds.
    session.advanceTicks(900);
    for (const auto id : units) {
        const auto* unit = session.world().findEntity(id);
        if (unit->unitControl.hasStrategicDestination)
            std::cerr << "Unsettled tank " << id << " position " << unit->transform.position.x << ',' << unit->transform.position.z
                      << " destination " << unit->unitControl.strategicDestination.x << ',' << unit->unitControl.strategicDestination.z
                      << " heading " << unit->transform.rotationDegrees.y << " waypoint " << unit->transient.navigationWaypoint
                      << '/' << unit->transient.navigationPath.size() << '\n';
    }
    for (const auto id : units) {
        const auto* unit = session.world().findEntity(id);
        require(!unit->unitControl.hasStrategicDestination && unit->unitControl.order == UnitOrderKind::idle &&
                    glm::length(glm::vec2{unit->transform.position.x, unit->transform.position.z} - target) < 24,
                "All tanks settle near the group destination instead of competing for one point");
        require(!overlapsObject(session.world(), definitions,
                    {unit->transform.position.x, unit->transform.position.z}, 2.19F, id),
                "Group arrival preserves physical collision clearance");
    }
    return 0;
}
