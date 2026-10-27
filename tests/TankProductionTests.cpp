#include "TerrainLevelingFixture.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "persistence/SaveGame.hpp"
#include "simulation/CommandCodec.hpp"
#include "simulation/GameSession.hpp"
#include "simulation/UnitDeployment.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <glm/trigonometric.hpp>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
strategy::EntityId add(strategy::World& world,
                       const strategy::DefinitionRegistry& definitions,
                       const char* archetype,
                       glm::vec2 position) {
    const auto id = world.createEntity(archetype, archetype, 1).id;
    auto* entity = world.findEntity(id);
    definitions.initializeEntity(*entity);
    entity->transform.position = {position.x, 0, position.y};
    return id;
}
} // namespace
int strategyTestMain() {
    using namespace strategy;
    const DefinitionRegistry definitions;
    require(!definitions.building(BuildingArchetypeId{"drone_factory"}) &&
                definitions.building(BuildingArchetypeId{"tank_factory"}),
            "Tank factory replaces the unfinished drone factory");
    const auto* recipe = definitions.recipe(RecipeId{"tank_factory.train_light_tank"});
    require(recipe && recipe->product.id == "light_tank" && recipe->cost.at("alloy") == 125 &&
                recipe->cost.at("fuel") == 25 && recipe->durationTicks == 600,
            "Factory exposes one paid twenty-second light-tank recipe");
    auto flat = test::levelingFixture(0);
    World rallyWorld;
    const auto rallyProducer = add(rallyWorld, definitions, "tank_factory", {0, 0});
    for (const float heading : {0.F, 37.F, 90.F}) {
        auto* building = rallyWorld.findEntity(rallyProducer);
        building->transform.rotationDegrees.y = heading;
        for (const auto flag :
             {glm::vec2{40, 0}, glm::vec2{-40, 0}, glm::vec2{0, 40}, glm::vec2{30, 30}}) {
            building->production.rallyPoint = flag;
            const auto shape = spatialShape(definitions, *building);
            const auto boundary = closestBoundaryPoint(shape, flag);
            const auto spawn =
                findUnitDeployment(rallyWorld, flat, definitions, 10, *building, "light_tank");
            require(spawn && !overlapsObject(rallyWorld, definitions, *spawn, 2.2F),
                    "Rally-facing deployment clears rotated building footprints");
            const auto outward = glm::normalize(flag - boundary);
            const auto offset = *spawn - boundary;
            require(
                glm::dot(glm::normalize(offset), outward) > .999F &&
                    std::abs(glm::length(offset) -
                             (2.2F + definitions.archetype(building->archetype)->spawnClearance +
                              .01F)) < .001F,
                "Deployment begins at the closest boundary toward the rally, with unit clearance");
        }
    }
    rallyWorld.findEntity(rallyProducer)->transform.rotationDegrees.y = 0;
    rallyWorld.findEntity(rallyProducer)->production.rallyPoint = glm::vec2{40, 0};
    const auto preferredSpawn = findUnitDeployment(
        rallyWorld, flat, definitions, 10, *rallyWorld.findEntity(rallyProducer), "light_tank");
    add(rallyWorld, definitions, "storage_silo", *preferredSpawn);
    const auto fallbackSpawn = findUnitDeployment(
        rallyWorld, flat, definitions, 10, *rallyWorld.findEntity(rallyProducer), "light_tank");
    require(fallbackSpawn && !overlapsObject(rallyWorld, definitions, *fallbackSpawn, 2.2F),
            "Blocked rally-facing exit still finds a legal deployment position");
    World droneWorld;
    const auto droneHub = add(droneWorld, definitions, "command_hub", {0, 0});
    droneWorld.findEntity(droneHub)->production.rallyPoint = glm::vec2{-40, 0};
    const auto droneSpawn = findUnitDeployment(
        droneWorld, flat, definitions, 10, *droneWorld.findEntity(droneHub), "construction_drone");
    require(droneSpawn && droneSpawn->x < 0 && std::abs(droneSpawn->y) < .001F,
            "Command-hub drones also deploy from the rally-facing boundary");
    World crowded;
    const auto factory = add(crowded, definitions, "tank_factory", {0, 0});
    for (int z = -3; z <= 3; ++z)
        for (int x = -3; x <= 3; ++x)
            if (x || z)
                add(crowded, definitions, "storage_silo", {x * 8.F, z * 8.F});
    const auto position = findUnitDeployment(
        crowded, flat, definitions, 10, *crowded.findEntity(factory), "light_tank");
    require(position && glm::length(*position) > 28 &&
                !overlapsObject(crowded, definitions, *position, 2.2F),
            "A crowded factory deploys beyond multiple occupied rings without overlapping");
    require(position ==
                findUnitDeployment(
                    crowded, flat, definitions, 10, *crowded.findEntity(factory), "light_tank"),
            "Expanding deployment search is deterministic");
    crowded.findEntity(factory)->transform.position = {110, 0, 110};
    const auto edge = findUnitDeployment(
        crowded, flat, definitions, 10, *crowded.findEntity(factory), "light_tank");
    require(edge && MapArea{10}.contains(*edge, 2.2F + definitions.matchRules().terrainEdgeMargin),
            "A corner factory searches inward instead of spawning outside the map");
    auto water = test::levelingFixture(0, true);
    World wetWorld;
    const auto wetFactory = add(wetWorld, definitions, "tank_factory", {0, 0});
    const auto drySpawn = findUnitDeployment(
        wetWorld, water, definitions, 10, *wetWorld.findEntity(wetFactory), "light_tank");
    require(water.movementCostAt(0, 0, movementDomainBit(MovementDomain::land)) == 0 && drySpawn &&
                glm::length(*drySpawn) > 20 &&
                water.movementCostAt(
                    drySpawn->x, drySpawn->y, movementDomainBit(MovementDomain::land)) > 0,
            "A factory in the flooded fixture searches beyond water and deploys onto dry ground");

    GameSession session{definitions, 17, "france"};
    const auto anchor = session.startingAnchors().front();
    session.world().replaceEntities({});
    const auto producer = add(session.world(), definitions, "tank_factory", anchor);
    session.players().find(1)->resources = {{"alloy", 1000.F}, {"fuel", 1000.F}};
    session.world().findEntity(producer)->power.enabled = false;
    require(session.canStartRecipe(1, producer, RecipeId{recipe->id}),
            "Owned factory accepts its tank recipe");
    require(session.submit({1, 1, StartRecipeCommand{producer, recipe->id}}), "Queue tank");
    session.advanceTicks();
    require(session.players().find(1)->resources.at("alloy") == 875 &&
                session.players().find(1)->resources.at("fuel") == 975 &&
                session.world().findEntity(producer)->production.queue.front().remainingTicks ==
                    600,
            "Tank is paid once and production pauses without electricity");
    require(session.submit({1, 2, CancelProductionCommand{producer, 0}}), "Cancel tank");
    session.advanceTicks();
    require(session.players().find(1)->resources.at("alloy") == 1000 &&
                session.players().find(1)->resources.at("fuel") == 1000,
            "Cancellation refunds the unbuilt tank");
    const auto generator =
        add(session.world(), definitions, "basic_generator", anchor + glm::vec2{-17, 0});
    require(session.submit({1, 3, SetPowerEnabledCommand{producer, true}}), "Enable factory");
    require(session.submit({1, 4, ConnectPowerCommand{generator, producer}}), "Power factory");
    require(session.submit({1, 5, StartRecipeCommand{producer, recipe->id}}), "Queue powered tank");
    session.advanceTicks(30);
    require(session.world().findEntity(producer)->production.queue.front().remainingTicks == 570,
            "Powered tank production advances at the existing queue rate");
    session.world().findEntity(generator)->power.generation = 6;
    session.advanceTicks(10);
    require(session.world().findEntity(producer)->production.queue.front().remainingTicks == 565,
            "Underpowered factory production advances proportionally");
    session.world().findEntity(generator)->power.generation = 20;
    // Complete in a heavily occupied neighborhood, not just an isolated search fixture.
    for (int z = -3; z <= 3; ++z)
        for (int x = -3; x <= 3; ++x)
            if (x || z)
                add(session.world(),
                    definitions,
                    "storage_silo",
                    anchor + glm::vec2{x * 8.F, z * 8.F});
    session.advanceTicks(565);
    const Entity* tank = nullptr;
    for (const auto& entity : session.world().entities())
        if (entity.archetype.value == "light_tank")
            tank = &entity;
    require(tank && tank->health.maximum == 350 && tank->vision && tank->combat && !tank->flight &&
                !tank->battery && definitions.permitsDirectControl(*tank) &&
                session.world().findEntity(producer)->production.queue.empty(),
            "Completed tank deploys with health, sight, movement and cannon definitions");
    require(glm::length(glm::vec2{tank->transform.position.x, tank->transform.position.z} -
                        anchor) > 28 &&
                !overlapsObject(session.world(),
                                definitions,
                                {tank->transform.position.x, tank->transform.position.z},
                                2.2F,
                                tank->id),
            "Production deploys farther out rather than losing a tank to a crowded perimeter");
    const auto fuel = session.players().find(1)->resources.at("fuel");
    const auto tankId = tank->id;
    const auto beforeMove = tank->transform.position;
    const auto outward = glm::normalize(glm::vec2{beforeMove.x, beforeMove.z} - anchor);
    const auto destination = glm::vec2{beforeMove.x, beforeMove.z} + outward * 6.F;
    require(session.submit({1, 6, MoveUnitCommand{tankId, {destination.x, 0, destination.y}}}),
            "Spawned tank accepts an ordinary movement order");
    session.advanceTicks(45);
    require(glm::length(session.world().findEntity(tankId)->transform.position - beforeMove) >
                    .5F &&
                session.players().find(1)->resources.at("fuel") == fuel,
            "Tank moves using ground navigation without consuming ongoing Fuel");
    const auto path =
        std::filesystem::temp_directory_path() / "strategy_tank_factory_roundtrip.json";
    SaveGame::write(path, 17, session.world(), &session.players());
    const auto loaded = SaveGame::read(path);
    require(std::any_of(loaded.entities.begin(),
                        loaded.entities.end(),
                        [](const Entity& entity) {
                            return entity.archetype.value == "light_tank" && entity.combat &&
                                   entity.health.maximum == 350;
                        }),
            "New tank and factory definitions survive save/load");
    std::filesystem::remove(path);
    // A completely occupied map is the physical limit: preserve the paid unit and retry.
    session.world().replaceEntities({});
    const auto waitingFactory = add(session.world(), definitions, "tank_factory", {0, 0});
    ProductionOrder waiting;
    waiting.kind = ProductionKind::trainCharacter;
    waiting.recipeId = recipe->id;
    waiting.productId = "light_tank";
    waiting.iconId = "unit_light_tank";
    waiting.durationTicks = 600;
    waiting.remainingTicks = 0;
    waiting.reservedCosts = {{"alloy", 125.F}, {"fuel", 25.F}};
    session.world().findEntity(waitingFactory)->production.queue.push_back(waiting);
    const float half = MapArea{session.mapChunksPerSide()}.halfExtent();
    std::vector<EntityId> blockers;
    for (float z = -half + 10; z < half; z += 20)
        for (float x = -half + 10; x < half; x += 20) {
            const auto id = add(session.world(), definitions, "town_center", {x, z});
            session.world().findEntity(id)->vision.reset();
            blockers.push_back(id);
        }
    const auto money = session.players().find(1)->resources;
    session.advanceTicks(15);
    require(
        session.world().findEntity(waitingFactory)->production.queue.size() == 1 &&
            session.world().findEntity(waitingFactory)->production.queue.front().remainingTicks ==
                0 &&
            session.players().find(1)->resources == money,
        "An entirely occupied map retains completed production without losing or charging again");
    SaveGame::write(path, 17, session.world(), &session.players());
    const auto pending = SaveGame::read(path);
    const auto pendingFactory =
        std::find_if(pending.entities.begin(), pending.entities.end(), [&](const Entity& entity) {
            return entity.id == waitingFactory;
        });
    require(pendingFactory != pending.entities.end() &&
                pendingFactory->production.queue.front().remainingTicks == 0,
            "A completed deployment awaiting legal space survives save/load");
    std::filesystem::remove(path);
    for (auto id : blockers)
        session.world().destroyEntity(id);
    session.advanceTicks(15);
    require(session.world().findEntity(waitingFactory)->production.queue.empty() &&
                std::count_if(session.world().entities().begin(),
                              session.world().entities().end(),
                              [](const Entity& entity) {
                                  return entity.archetype.value == "light_tank";
                              }) == 1,
            "Releasing space deploys exactly one previously completed tank");

    session.world().replaceEntities({});
    const auto doomed = add(session.world(), definitions, "tank_factory", {0, 0});
    session.world().findEntity(doomed)->power.reset();
    session.world().findEntity(doomed)->health.current = .1F;
    waiting.remainingTicks = 2;
    session.world().findEntity(doomed)->production.queue.push_back(waiting);
    const auto attacker = add(session.world(), definitions, "worker", {1, 0});
    auto* enemy = session.world().findEntity(attacker);
    enemy->authority.owner = 2;
    session.advanceTicks(); // Establish enemy vision before the completion-tick attack.
    enemy = session.world().findEntity(attacker);
    enemy->unitControl.order = UnitOrderKind::attack;
    enemy->unitControl.orderTarget = doomed;
    session.advanceTicks();
    require(!session.world().findEntity(doomed) && std::none_of(session.world().entities().begin(),
                                                                session.world().entities().end(),
                                                                [](const Entity& entity) {
                                                                    return entity.archetype.value ==
                                                                           "light_tank";
                                                                }),
            "A factory destroyed in its completion tick cannot create a ghost tank");
    waiting.remainingTicks = 1;
    session.world().replaceEntities({});
    const auto rallyFactory = add(session.world(), definitions, "tank_factory", anchor);
    session.world().findEntity(rallyFactory)->power.reset();
    const glm::vec2 firstRally = anchor + glm::vec2{25, 0};
    const auto wire = CommandCodec::decode(
        CommandCodec::encode({1, 100, SetRallyPointCommand{rallyFactory, firstRally}}));
    require(wire && std::get<SetRallyPointCommand>(wire->payload).position == firstRally,
            "Rally point command survives wire encoding");
    const auto clearWire = CommandCodec::decode(
        CommandCodec::encode({1, 101, SetRallyPointCommand{rallyFactory, std::nullopt}}));
    require(clearWire && !std::get<SetRallyPointCommand>(clearWire->payload).position,
            "Clear rally point command survives wire encoding");
    require(!CommandCodec::decode(CommandCodec::encode(
                {1,
                 102,
                 SetRallyPointCommand{rallyFactory,
                                      glm::vec2{std::numeric_limits<float>::quiet_NaN(), 0}}})),
            "Wire decoder rejects nonfinite rally points");
    session.world().findEntity(rallyFactory)->production.queue.push_back(waiting);
    require(session.submit(*wire), "Set factory rally point before deployment");
    session.advanceTicks();
    EntityId firstTank{};
    for (const auto& entity : session.world().entities())
        if (entity.archetype.value == "light_tank")
            firstTank = entity.id;
    require(firstTank &&
                session.world().findEntity(firstTank)->unitControl.order == UnitOrderKind::move &&
                session.world().findEntity(firstTank)->unitControl.hasStrategicDestination,
            "New tank immediately receives its factory rally movement order");
    const auto firstDestination =
        session.world().findEntity(firstTank)->unitControl.strategicDestination;
    require(glm::length(glm::vec2{firstDestination.x, firstDestination.z} - firstRally) < 3,
            "Rally point uses normal tank navigation toward the requested destination");
    const auto startPosition = session.world().findEntity(firstTank)->transform.position;
    // Allow a newly deployed hull to pivot (up to two seconds for a half-turn) before driving.
    session.advanceTicks(90);
    require(glm::length(session.world().findEntity(firstTank)->transform.position - startPosition) >
                .5F,
            "New tank actually travels toward its rally point");
    const glm::vec2 nextRally = anchor + glm::vec2{-25, 0};
    require(session.submit({1, 101, SetRallyPointCommand{rallyFactory, nextRally}}),
            "Move rally point");
    session.world().findEntity(rallyFactory)->production.queue.push_back(waiting);
    session.advanceTicks();
    require(session.world().findEntity(firstTank)->unitControl.strategicDestination ==
                firstDestination,
            "Changing the factory rally does not retarget an already deployed tank");
    EntityId secondTank{};
    for (const auto& entity : session.world().entities())
        if (entity.archetype.value == "light_tank" && entity.id != firstTank)
            secondTank = entity.id;
    require(
        secondTank &&
            glm::length(
                glm::vec2{
                    session.world().findEntity(secondTank)->unitControl.strategicDestination.x,
                    session.world().findEntity(secondTank)->unitControl.strategicDestination.z} -
                nextRally) < 3,
        "Next tank receives the changed rally point");
    require(session.submit({1, 102, SetRallyPointCommand{rallyFactory, std::nullopt}}),
            "Clear rally");
    session.world().findEntity(rallyFactory)->production.queue.push_back(waiting);
    session.advanceTicks();
    bool idleThird = false;
    for (const auto& entity : session.world().entities())
        if (entity.archetype.value == "light_tank" && entity.id != firstTank &&
            entity.id != secondTank)
            idleThird = entity.unitControl.order == UnitOrderKind::idle &&
                        !entity.unitControl.hasStrategicDestination;
    require(idleThird && !session.world().findEntity(rallyFactory)->production.rallyPoint,
            "Clearing the rally leaves subsequent tanks at their deployment position");
    require(session.submit({1, 103, SetRallyPointCommand{rallyFactory, firstRally}}),
            "Restore rally");
    session.advanceTicks();
    require(session.submit({2, 1, SetRallyPointCommand{rallyFactory, nextRally}}),
            "Foreign command envelope");
    require(session.submit({1, 104, SetRallyPointCommand{rallyFactory, glm::vec2{99999, 0}}}),
            "Out-of-map envelope");
    session.advanceTicks();
    require(session.world().findEntity(rallyFactory)->production.rallyPoint == firstRally,
            "Enemy and out-of-map rally commands cannot change the factory");
    const auto rallyChecksum = session.stateChecksum();
    session.world().findEntity(rallyFactory)->production.rallyPoint = nextRally;
    require(session.stateChecksum() != rallyChecksum,
            "Rally point participates in authoritative checksum");
    session.world().findEntity(rallyFactory)->production.rallyPoint = firstRally;
    SaveGame::write(path, 17, session.world(), &session.players());
    const auto savedRally = SaveGame::read(path);
    const auto savedFactory =
        std::find_if(savedRally.entities.begin(),
                     savedRally.entities.end(),
                     [&](const Entity& entity) { return entity.id == rallyFactory; });
    require(savedFactory != savedRally.entities.end() &&
                savedFactory->production.rallyPoint == firstRally,
            "Factory rally point survives save/load");
    std::filesystem::remove(path);
    // Every producer uses the same rally/deployment contract, including flying drone output.
    std::uint64_t sequence = 200;
    for (const auto* type :
         {"command_hub", "town_center", "town_center_level_2", "town_center_level_3"}) {
        session.world().replaceEntities({});
        const auto hub = add(session.world(), definitions, type, anchor);
        session.world().findEntity(hub)->power.reset();
        require(definitions.producesUnits(*session.world().findEntity(hub)),
                "Every command-hub tier exposes unit production and rally controls");
        const auto recipes = definitions.recipesForProducer(type);
        const auto training = std::find_if(recipes.begin(), recipes.end(), [](const auto* r) {
            return r->product.kind == RecipeProductKind::unit;
        });
        require(training != recipes.end(), "Hub has a drone training recipe");
        session.players().find(1)->resources["alloy"] = 1000;
        const auto flag = anchor + glm::vec2{35, 0};
        require(session.submit({1, sequence++, SetRallyPointCommand{hub, flag}}) &&
                    session.submit({1, sequence++, StartRecipeCommand{hub, (*training)->id}}),
                "Set hub rally and train a drone through normal commands");
        session.advanceTicks();
        require(!session.world().findEntity(hub)->production.queue.empty(), "Drone queued");
        session.world().findEntity(hub)->production.queue.front().remainingTicks = 1;
        session.advanceTicks();
        EntityId drone{};
        for (const auto& entity : session.world().entities())
            if (entity.archetype.value == "construction_drone")
                drone = entity.id;
        require(drone && session.world().findEntity(drone)->flight &&
                    session.world().findEntity(drone)->transform.position.y ==
                        session.world().findEntity(drone)->flight.altitude &&
                    session.world().findEntity(drone)->unitControl.order == UnitOrderKind::move &&
                    !definitions.permitsDirectControl(*session.world().findEntity(drone)),
                "Hub drone deploys with rally movement and cannot be possessed");
        const auto initial = session.world().findEntity(drone)->transform.position;
        session.advanceTicks(30);
        const auto moved = session.world().findEntity(drone)->transform.position;
        require(glm::length(glm::vec2{moved.x, moved.z} - flag) <
                    glm::length(glm::vec2{initial.x, initial.z} - flag),
                "Spawned drone flies toward its command hub rally");
        require(session.submit({1, sequence++, PossessUnitCommand{drone}}),
                "Drone possession envelope");
        session.advanceTicks();
        require(session.world().findEntity(drone)->authority.directController == 0,
                "Simulation rejects direct control of drones");
    }
    for (const auto* type : {"worker", "light_tank"}) {
        const auto unit = add(session.world(), definitions, type, anchor);
        require(definitions.permitsDirectControl(*session.world().findEntity(unit)),
                "All non-drone units are possessable from their definitions");
        require(session.submit({2, sequence++, PossessUnitCommand{unit}}),
                "Foreign possession envelope");
        session.advanceTicks();
        require(session.world().findEntity(unit)->authority.directController == 0,
                "Enemy cannot possess a friendly unit");
        require(session.submit({1, sequence++, PossessUnitCommand{unit}}), "Possess friendly unit");
        session.advanceTicks();
        require(session.world().findEntity(unit)->authority.directController == 1,
                "Friendly non-drone unit enters direct control");
        require(session.submit({1, sequence++, ReleaseUnitCommand{unit}}), "Release unit");
        session.advanceTicks();
    }
    // Burst deployment shares the same collision-safe arrival planner as group moves.
    for (const auto& [producerType, product] : {
             std::pair{"tank_factory", "light_tank"}, std::pair{"command_hub", "construction_drone"}}) {
        session.world().replaceEntities({});
        const auto source = add(session.world(), definitions, producerType, anchor);
        auto* building = session.world().findEntity(source);
        building->power.reset();
        building->production.rallyPoint = anchor + glm::vec2{25, 0};
        auto batch = waiting;
        batch.productId = product;
        batch.amount = 5;
        batch.remainingTicks = 1;
        building->production.queue.push_back(batch);
        const auto balances = session.players().find(1)->resources;
        session.advanceTicks();
        std::vector<EntityId> deployed;
        for (const auto& unit : session.world().entities()) {
            if (unit.archetype.value != product) continue;
            deployed.push_back(unit.id);
            require(unit.unitControl.hasStrategicDestination, "Each unit in a burst receives a rally order");
            if (!unit.flight) {
                auto delta = unit.unitControl.strategicDestination - unit.transform.position;
                for (const auto& waypoint : unit.transient.navigationPath)
                    if (glm::length(waypoint - unit.transform.position) > .35F) { delta = waypoint - unit.transform.position; break; }
                const float expected = glm::degrees(std::atan2(delta.x, delta.z));
                require(std::abs(std::remainder(unit.transform.rotationDegrees.y - expected, 360.F)) < .01F,
                        "A deployed tank faces its actual rally route before its first movement tick");
            }
        }
        require(deployed.size() == 5 && session.world().findEntity(source)->production.queue.empty() &&
                    session.players().find(1)->resources == balances,
                "A completed batch deploys every paid unit without charging again");
        for (std::size_t i = 0; i < deployed.size(); ++i)
            for (std::size_t j = i + 1; j < deployed.size(); ++j)
                require(glm::length(session.world().findEntity(deployed[i])->unitControl.strategicDestination -
                                    session.world().findEntity(deployed[j])->unitControl.strategicDestination) >
                            (std::string_view{product} == "light_tank" ? 4.9F : 1.5F),
                        "Repeated factory and hub output does not share one exact rally destination");
        session.advanceTicks(600);
        for (const auto id : deployed)
            require(!session.world().findEntity(id)->unitControl.hasStrategicDestination,
                    "Every deployed unit clears its producer and settles at the rally area");
    }
    return 0;
}
