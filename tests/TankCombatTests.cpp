#include "TerrainLevelingFixture.hpp"
#include "TerrainTiltFixture.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "persistence/SaveGame.hpp"
#include "players/PlayerRegistry.hpp"
#include "simulation/CombatRules.hpp"
#include "simulation/CommandCodec.hpp"
#include "simulation/GameSession.hpp"
#include "simulation/VehicleControl.hpp"
#include "world/Collision.hpp"
#include "world/Navigation.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <tuple>
#include <limits>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
strategy::EntityId add(strategy::World& world,
                       const strategy::DefinitionRegistry& definitions,
                       const char* type,
                       strategy::PlayerId owner,
                       glm::vec2 p) {
    const auto id = world.createEntity(type, type, owner).id;
    auto* e = world.findEntity(id);
    definitions.initializeEntity(*e);
    e->transform.position = {p.x, 0, p.y};
    return id;
}
} // namespace
int strategyTestMain() {
    using namespace strategy;
    const DefinitionRegistry definitions;
    auto flat = test::levelingFixture(0);
    World world;
    const auto source = add(world, definitions, "light_tank", 1, {0, -8});
    const auto target = add(world, definitions, "light_tank", 2, {0, 8});
    auto* actor = world.findEntity(source);
    require(definitions.archetype(actor->archetype)->movement.turnRateDegrees == 90,
            "Tank turn rate is authored in data");
    auto movement = turnLimitedMovement(*actor, {0, -1}, 90, 1.F / 30);
    require(std::abs(actor->transform.rotationDegrees.y) <= 3.001F && glm::length(movement) < .001F,
            "Tank turns at most three degrees per tick and does not reverse-slide");
    for (int i = 0; i < 40; ++i)
        movement = turnLimitedMovement(*actor, {1, 0}, 90, 1.F / 30);
    require(glm::length(movement) > .9F && std::abs(actor->transform.rotationDegrees.y - 90) < .01F,
            "Tank resumes full forward movement after turning");
    actor->transform.rotationDegrees.y = 179;
    require(turnToward(*actor, -179, 90, 1.F / 30) < .01F,
            "Vehicle turning takes the short wraparound direction");
    actor->transform.rotationDegrees.y = 0;
    const auto& driveTuning = definitions.archetype(actor->archetype)->movement;
    actor->unitControl.directInput = {0, -1};
    for (int i = 0; i < 30; ++i)
        (void)driveVehicle(*actor, driveTuning, 4, 1.F / 30);
    require(std::abs(actor->unitControl.driveSpeed + 2.4F) < .001F &&
                actor->transform.rotationDegrees.y == 0,
            "Reverse reaches sixty percent speed without turning the hull");
    actor->unitControl.directInput = {1, 1};
    const auto stopping = driveVehicle(*actor, driveTuning, 4, 1.F / 30);
    require(actor->unitControl.driveSpeed < 0 && stopping.y < 0 &&
                actor->transform.rotationDegrees.y > 0,
            "Opposite throttle brakes while steering remains independent");
    actor->unitControl.driveSpeed = 0;
    actor->transform.rotationDegrees.y = 0;
    const auto curve = driveVehicle(*actor, driveTuning, 4, 1.F / 30);
    require(curve.x > 0 && curve.y > 0,
            "Driving through a turn does not wait for a five-degree heading threshold");
    actor->unitControl.directInput = {0, 0};
    actor->unitControl.driveSpeed = 4;
    for (int i = 0; i < 15; ++i)
        (void)driveVehicle(*actor, driveTuning, 4, 1.F / 30);
    require(actor->unitControl.driveSpeed == 0, "Releasing throttle stops prolonged coasting");
    actor->unitControl.directInput = {0, 0};
    actor->transform.rotationDegrees.y = 0;
    PlayerRegistry players;
    require(validCombatTarget(definitions, players, *actor, *world.findEntity(target)),
            "Tank accepts enemy ground targets without possession eligibility");
    world.findEntity(target)->flight.emplace();
    require(!validCombatTarget(definitions, players, *actor, *world.findEntity(target)),
            "Ground cannon rejects flying drones");
    world.findEntity(target)->flight.reset();
    players.find(2)->team = 1;
    require(!validCombatTarget(definitions, players, *actor, *world.findEntity(target)),
            "Allied units are invalid targets");
    players.find(2)->team = 2;
    players.find(1)->visibility.revealArea(glm::vec2{-120}, glm::vec2{120}, false);
    require(!combatTargetVisible(players, *actor, *world.findEntity(target), 10),
            "Explored territory alone does not permit targeting a hidden moving enemy");
    const auto* weapon = primaryWeapon(definitions, *actor);
    require(weapon && clearFiringLine(
                          world, flat, definitions, *actor, *world.findEntity(target), *weapon),
            "Unobstructed cannon ray is clear");
    const auto cover = add(world, definitions, "tank_factory", 1, {0, 0});
    require(!clearFiringLine(world,
                             flat,
                             definitions,
                             *world.findEntity(source),
                             *world.findEntity(target),
                             *weapon),
            "Factory geometry blocks shots");
    const auto blockedShot = traceWeapon(world,
                                         flat,
                                         definitions,
                                         players,
                                         *world.findEntity(source),
                                         combatMuzzle(flat, *world.findEntity(source), *weapon),
                                         {0, 0, 1},
                                         16,
                                         10);
    require(blockedShot.entity == cover && blockedShot.impact,
            "Manual cannon ray hits friendly building cover first");
    world.findEntity(cover)->transform.rotationDegrees.y = 35;
    require(!clearFiringLine(world,
                             flat,
                             definitions,
                             *world.findEntity(source),
                             *world.findEntity(target),
                             *weapon),
            "Rotated building footprints block shots");
    world.findEntity(source)->transform.position.y = 12;
    world.findEntity(target)->transform.position.y = 12;
    require(clearFiringLine(world,
                            flat,
                            definitions,
                            *world.findEntity(source),
                            *world.findEntity(target),
                            *weapon),
            "Firing line passes above a building when both endpoints are high enough");
    world.destroyEntity(cover);
    world.findEntity(source)->transform.position.y = 0;
    world.findEntity(target)->transform.position.y = 0;
    auto ridge = test::levelingFixture(6, false, true);
    require(!clearFiringLine(world,
                             ridge,
                             definitions,
                             *world.findEntity(source),
                             *world.findEntity(target),
                             *weapon),
            "Terrain between units blocks the firing line");
    int hits = 0;
    for (std::uint64_t shot = 1; shot <= 1000; ++shot) {
        require(!weaponHits(7, source, shot, 0) && weaponHits(7, source, shot, 1),
                "Accuracy endpoints are exact");
        hits += weaponHits(7, source, shot, .5F);
        require(weaponHits(7, source, shot, .5F) == weaponHits(7, source, shot, .5F),
                "Accuracy is deterministic");
    }
    require(hits > 420 && hits < 580, "Accuracy distribution follows the authored probability");
    // Navigation must account for tank width, regardless of possession flags.
    World passage;
    const auto left = add(passage, definitions, "storage_silo", 1, {-5, 0});
    const auto right = add(passage, definitions, "storage_silo", 1, {5, 0});
    Navigation navigation{flat, definitions, 10};
    auto path = navigation.findPath(flat, passage, {0, 0, -15}, {0, 0, 15}, 2.2F, 0);
    require(!path.empty(), "Tank can route around a gap narrower than its footprint");
    for (const auto& p : path)
        require(!overlapsObject(passage, definitions, {p.x, p.z}, 2.2F),
                "Tank route keeps footprint clearance");
    passage.findEntity(left)->transform.position.x = -8;
    passage.findEntity(right)->transform.position.x = 8;
    path = navigation.findPath(flat, passage, {0, 0, -15}, {0, 0, 15}, 2.2F, 0);
    require(std::any_of(path.begin(),
                        path.end(),
                        [](glm::vec3 p) { return std::abs(p.z) < 2 && std::abs(p.x) < 2; }),
            "A sufficiently wide gap stays usable by tanks");
    passage.replaceEntities({});
    const auto emptyMine = add(passage, definitions, "synthetic_mine", 1, {0, 0});
    passage.findEntity(emptyMine)->resource.remaining = 0;
    path = navigation.findPath(flat, passage, {0, 0, -15}, {0, 0, 15}, 2.2F, 0);
    require(!path.empty(), "Tank can route around an empty mine");
    for (const auto& point : path)
        require(!overlapsObject(passage, definitions, {point.x, point.z}, 2.2F),
                "An empty mine remains a navigation obstacle");

    GameSession session{definitions, 17, "france"};
    const auto anchor = session.startingAnchors().front();
    const auto setup = [&](bool coverBetween = false) {
        session.world().replaceEntities({});
        (void)session.consumeCombatShots();
        const auto a =
            add(session.world(), definitions, "light_tank", 1, anchor + glm::vec2{0, -9});
        const auto b = add(session.world(), definitions, "light_tank", 2, anchor + glm::vec2{0, 9});
        if (coverBetween)
            add(session.world(), definitions, "tank_factory", 1, anchor);
        session.advanceTicks(); // Visibility is authoritative, not inferred from a command's target
                                // ID.
        return std::pair{a, b};
    };
    auto [a, b] = setup();
    require(session.submit({1, 1, AttackEntityCommand{a, b}}), "Submit ordinary tank attack");
    session.advanceTicks();
    require(session.world().findEntity(b)->health.current == 318 &&
                session.world().findEntity(a)->combat.shotsFired == 1,
            "First shot applies exactly the weapon's per-shot damage");
    const auto firstShots = session.consumeCombatShots();
    require(firstShots.size() == 1 && firstShots.front().hit && firstShots.front().target == b,
            "Simulation emits one presentation event per actual shot");
    session.advanceTicks(59);
    require(session.world().findEntity(b)->health.current == 318 &&
                session.consumeCombatShots().empty(),
            "Cannon cannot apply continuous damage between cooldowns");
    session.advanceTicks();
    require(session.world().findEntity(b)->health.current == 286 &&
                session.world().findEntity(a)->combat.shotsFired == 2,
            "Sixty-tick cannon cooldown produces the next discrete shot");
    const auto save = std::filesystem::temp_directory_path() / "strategy_tank_combat.json";
    SaveGame::write(save, 17, session.world(), &session.players());
    const auto loaded = SaveGame::read(save);
    const auto saved = std::find_if(
        loaded.entities.begin(), loaded.entities.end(), [&](const auto& e) { return e.id == a; });
    require(saved != loaded.entities.end() && saved->combat.shotsFired == 2 &&
                saved->combat.lastSeenTarget &&
                saved->combat.turretYawDegrees ==
                    session.world().findEntity(a)->combat.turretYawDegrees &&
                saved->combat.turretPitchDegrees ==
                    session.world().findEntity(a)->combat.turretPitchDegrees &&
                saved->combat.cooldownRemaining ==
                    session.world().findEntity(a)->combat.cooldownRemaining,
            "Shot sequence, cooldown and last-seen target survive save/load");
    for (std::uint64_t i = 1; i <= 100; ++i)
        require(weaponHits(17, a, saved->combat.shotsFired + i, .8F) ==
                    weaponHits(17, a, session.world().findEntity(a)->combat.shotsFired + i, .8F),
                "Reloading retains the future accuracy sequence without relying on session tick");
    std::filesystem::remove(save);
    const auto checksum = session.stateChecksum();
    ++session.world().findEntity(a)->combat.shotsFired;
    require(session.stateChecksum() != checksum,
            "Shot sequence participates in authoritative checksum");
    --session.world().findEntity(a)->combat.shotsFired;
    require(session.submit({1, 2, StopUnitCommand{a}}), "Stop attack");
    session.advanceTicks();
    require(session.submit({1, 3, AttackEntityCommand{a, b}}), "Reissue attack");
    session.advanceTicks();
    require(session.world().findEntity(a)->combat.shotsFired == 2,
            "Stop/reissue cannot reset weapon cooldown");
    // A tank does not teleport its heading to fire at a target behind it.
    std::tie(a, b) = setup();
    session.world().findEntity(a)->transform.rotationDegrees.y = 180;
    session.world().findEntity(a)->combat.turretYawDegrees = 180;
    require(session.submit({1, 4, AttackEntityCommand{a, b}}), "Attack behind hull");
    session.advanceTicks();
    require(
        session.world().findEntity(a)->combat.shotsFired == 0 &&
            std::abs(std::remainder(session.world().findEntity(a)->combat.turretYawDegrees - 180,
                                    360.F)) <= 5.001F &&
            session.world().findEntity(a)->transform.rotationDegrees.y == 180,
        "Turret turns gradually toward the target without turning the hull");
    session.advanceTicks(60);
    require(session.world().findEntity(a)->combat.shotsFired > 0, "Aligned tank eventually fires");
    // Lost sight must sever live entity tracking and pursue only the remembered position.
    std::tie(a, b) = setup();
    require(session.submit({1, 5, AttackEntityCommand{a, b}}), "Attack visible target");
    session.advanceTicks();
    const auto remembered = session.world().findEntity(a)->combat.lastSeenTarget;
    session.world().findEntity(b)->transform.position += glm::vec3{100, 0, 100};
    session.advanceTicks();
    const auto* seeker = session.world().findEntity(a);
    require(remembered && seeker->unitControl.order != UnitOrderKind::attack &&
                seeker->unitControl.orderTarget == 0 &&
                glm::length(glm::vec2{seeker->unitControl.strategicDestination.x,
                                      seeker->unitControl.strategicDestination.z} -
                            *remembered) < 4,
            "Hidden enemy movement is replaced by travel toward its last seen position");
    // A ground-only weapon rejects an air target without replacing its current order.
    std::tie(a, b) = setup();
    session.world().findEntity(b)->flight.emplace();
    require(session.submit({1, 6, AttackEntityCommand{a, b}}), "Air-target envelope");
    session.advanceTicks();
    require(session.world().findEntity(a)->unitControl.order == UnitOrderKind::idle &&
                session.world().findEntity(a)->combat.shotsFired == 0,
            "Ground cannon cannot acquire an aircraft");
    std::tie(a, b) = setup(true);
    require(session.submit({1, 7, AttackEntityCommand{a, b}}), "Attack behind cover");
    session.advanceTicks();
    require(session.world().findEntity(b)->health.current == 350 &&
                session.world().findEntity(a)->transient.firingLineBlocked,
            "Building cover prevents immediate damage");
    session.advanceTicks(450);
    require(session.world().findEntity(a)->combat.shotsFired > 0,
            "Tank moves around cover to a reachable clear firing position");
    std::tie(a, b) = setup();
    session.world().findEntity(a)->transform.rotationDegrees.y = 180;
    const auto aim = combatAimPoint(definitions, session.terrain(), *session.world().findEntity(b));
    require(session.submit({1, 8, PossessUnitCommand{a}}), "Possess tank for manual cannon");
    session.advanceTicks();
    const auto encoded = CommandCodec::encode({1, 9, AimWeaponCommand{a, aim, false}});
    const auto decoded = CommandCodec::decode(encoded);
    require(decoded && std::get<AimWeaponCommand>(decoded->payload).position == aim,
            "Aim and trigger survive wire encoding");
    require(session.submit(*decoded), "Aim manual cannon");
    session.advanceTicks(60);
    require(session.world().findEntity(a)->transform.rotationDegrees.y == 180 &&
                session.world().findEntity(a)->combat.shotsFired == 0,
            "Aiming does not fire or rotate the hull");
    require(session.submit({1, 10, AimWeaponCommand{a, aim, true}}), "Manual trigger");
    session.advanceTicks();
    require(session.world().findEntity(b)->health.current == 318,
            "Manual shot hits visible ground target along barrel ray");
    require(session.submit({1, 11, AimWeaponCommand{a, aim, false}}), "Release trigger");
    session.advanceTicks(65);
    require(session.world().findEntity(a)->combat.shotsFired == 1,
            "Released trigger cannot repeat fire");
    require(session.submit({1, 12, ReleaseUnitCommand{a}}), "Leave direct control");
    session.advanceTicks();
    require(!session.world().findEntity(a)->combat.directAim &&
                !session.world().findEntity(a)->combat.triggerHeld,
            "Leaving direct control clears manual weapon input");
    std::tie(a, b) = setup();
    const auto sourcePosition = session.world().findEntity(a)->transform.position;
    const auto pole = add(session.world(),
                          definitions,
                          "electricity_pole",
                          1,
                          {sourcePosition.x, sourcePosition.z + 2.6F});
    const auto blockedAim =
        combatAimPoint(definitions, session.terrain(), *session.world().findEntity(b));
    require(session.submit({1, 13, PossessUnitCommand{a}}), "Possess beside cover");
    session.advanceTicks();
    require(session.submit({1, 14, AimWeaponCommand{a, blockedAim, false}}),
            "Aim through narrow cover");
    session.advanceTicks(40);
    require(session.submit({1, 15, AimWeaponCommand{a, blockedAim, true}}),
            "Fire against narrow cover");
    session.advanceTicks();
    require(session.world().findEntity(a)->combat.shotsFired == 1 &&
                session.world().findEntity(b)->health.current == 350 &&
                session.world().findEntity(pole)->health.current ==
                    session.world().findEntity(pole)->health.maximum,
            "Barrel protrusion cannot bypass friendly cover or cause friendly-fire damage");
    // Direct driving uses the same collision/terrain checks as ordinary movement.
    session.world().replaceEntities({});
    const_cast<Terrain&>(session.terrain()) = test::levelingFixture(0);
    a = add(session.world(), definitions, "light_tank", 1, {0, 0});
    b = add(session.world(), definitions, "light_tank", 2, {0, 10});
    require(session.submit({1, 16, PossessUnitCommand{a}}), "Possess for direct driving");
    session.advanceTicks();
    const auto reverseWire = CommandCodec::encode({1, 17, DirectVehicleInputCommand{a, -1, 0, false}});
    const auto reverseCommand = CommandCodec::decode(reverseWire);
    require(reverseCommand && std::get<DirectVehicleInputCommand>(reverseCommand->payload).throttle == -1,
            "Throttle, steering and brake survive wire encoding");
    auto invalidWire = reverseWire;
    invalidWire.back() = std::byte{2};
    require(!CommandCodec::decode(invalidWire), "Vehicle brake flag must be a strict boolean");
    require(!CommandCodec::decode(CommandCodec::encode(
                {1, 17, DirectVehicleInputCommand{a, std::numeric_limits<float>::quiet_NaN(), 0, false}})),
            "Vehicle input rejects non-finite throttle");
    require(session.submit(*reverseCommand), "Drive in reverse");
    session.advanceTicks(30);
    require(session.world().findEntity(a)->transform.position.z < -1.8F &&
                session.world().findEntity(a)->transform.rotationDegrees.y == 0 &&
                session.world().findEntity(a)->unitControl.driveSpeed < 0,
            "S backs the actual tank away without rotating it");
    const auto retreatAim = combatAimPoint(definitions, session.terrain(), *session.world().findEntity(b));
    require(session.submit({1, 18, AimWeaponCommand{a, retreatAim, true}}), "Fire while retreating");
    session.advanceTicks();
    require(session.world().findEntity(a)->combat.shotsFired == 1 &&
                session.world().findEntity(b)->health.current == 318,
            "Tank can fire at the enemy while reversing away");
    require(session.submit({1, 19, DirectVehicleInputCommand{a, 1, 1, true}}), "Brake overrides throttle");
    const float brakeYaw = session.world().findEntity(a)->transform.rotationDegrees.y;
    session.advanceTicks(15);
    require(session.world().findEntity(a)->unitControl.driveSpeed == 0 &&
                session.world().findEntity(a)->transform.rotationDegrees.y == brakeYaw,
            "Space stops both tracks despite throttle/steering input");
    require(session.submit({1, 20, DirectVehicleInputCommand{a, 0, -1, false}}), "Pivot left");
    const auto pivotPosition = session.world().findEntity(a)->transform.position;
    session.advanceTicks(10);
    require(session.world().findEntity(a)->transform.position == pivotPosition &&
                session.world().findEntity(a)->transform.rotationDegrees.y < -29,
            "Steering alone pivots on the spot");
    session.world().destroyEntity(b);
    auto* driver = session.world().findEntity(a);
    driver->transform.position = {0, 0, -10};
    driver->transform.rotationDegrees.y = 0;
    driver->unitControl.driveSpeed = 0;
    add(session.world(), definitions, "electricity_pole", 1, {0, -4});
    require(session.submit({1, 21, DirectVehicleInputCommand{a, 1, 0, false}}), "Drive toward obstacle");
    session.advanceTicks(120);
    driver = session.world().findEntity(a);
    require(driver->transform.position.z < -4 && driver->unitControl.driveSpeed == 0 &&
                !overlapsObject(session.world(), definitions,
                    {driver->transform.position.x, driver->transform.position.z}, 2.2F, a),
            "Collision blocks the tank and clears accumulated driving speed");
    session.world().replaceEntities({*driver});
    const_cast<Terrain&>(session.terrain()) = test::tiltFixture(3, 0, false, true);
    driver = session.world().findEntity(a);
    driver->transform.position = {-4, 0, 1};
    driver->transform.rotationDegrees.y = 90;
    driver->unitControl.driveSpeed = 0;
    session.advanceTicks(90);
    require(session.world().findEntity(a)->transform.position.x > 2,
            "Direct driving crosses a walkable hillside using the common slope rules");
    require(session.submit({1, 22, ReleaseUnitCommand{a}}), "Return to RTS movement");
    session.advanceTicks();
    require(session.world().findEntity(a)->unitControl.driveSpeed == 0 &&
                session.world().findEntity(a)->unitControl.directInput == glm::vec2{0},
            "Leaving possession clears throttle and driving momentum");
    return 0;
}
