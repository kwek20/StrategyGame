#include "game/RtsCamera.hpp"
#include "game/ThirdPersonCamera.hpp"
#include "gameplay/GameplayCatalogue.hpp"
#include "players/CountryCatalogue.hpp"
#include "simulation/GameSession.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/Navigation.hpp"
#include "world/World.hpp"

#include <cmath>
#include <iostream>
#include <unordered_set>

int main() {
    const strategy::CountryCatalogue countries;
    std::unordered_set<std::string> countryIds;
    bool valid = countries.countries().size() >= 2;
    for (const auto& country : countries.countries()) {
        valid = valid && !country.id.empty() && !country.nameKey.empty() &&
                country.specializationId == "unassigned" && countryIds.insert(country.id).second;
    }
    const strategy::GameplayCatalogue gameplay;
    strategy::Entity componentWorker;
    componentWorker.modelKey = "worker";
    gameplay.initializeEntity(componentWorker);
    strategy::Entity componentHall;
    componentHall.modelKey = "town_center";
    gameplay.initializeEntity(componentHall);
    valid = valid && componentWorker.unitControl && componentWorker.gatherer &&
            componentWorker.upgrades && !componentWorker.production &&
            !componentWorker.buildingUpgrades;
    valid = valid && !componentHall.unitControl && !componentHall.gatherer &&
            componentHall.production && componentHall.buildingUpgrades && componentHall.upgrades;
    valid = valid &&
            std::abs(gameplay.resolve(
                         strategy::GameplayStat::movementSpeed, "germany", "unassigned", "worker") -
                     7.35F) < 0.001F;
    valid =
        valid && std::abs(gameplay.resolve(
                              strategy::GameplayStat::gatherRate, "brazil", "logistics", "worker") -
                          4.84F) < 0.001F;
    const float townTraining =
        gameplay.productionDuration("spain", "unassigned", "town_center", "worker");
    const float outpostTraining =
        gameplay.productionDuration("spain", "unassigned", "outpost", "worker");
    valid = valid && gameplay.canTrain("town_center", "worker") &&
            gameplay.canTrain("outpost", "worker") && outpostTraining > townTraining;
    strategy::World world;
    const strategy::EntityId first = world.createEntity("First").id;
    const strategy::EntityId second = world.createEntity("Second", "house").id;
    valid = valid && first != second && world.size() == 2;
    valid = valid && world.findEntity(first) != nullptr;
    valid = valid && world.findEntity(second)->modelKey == "house";
    valid = valid && world.destroyEntity(first) && world.findEntity(first) == nullptr;
    valid = valid && world.size() == 1;

    strategy::World navigationWorld;
    strategy::Entity& navigationObstacle =
        navigationWorld.createEntity("Blocker", "town_center", 0);
    navigationObstacle.transform.position = {0.0F, 0.0F, 0.0F};
    const strategy::Terrain navigationTerrain{123U};
    strategy::Navigation navigation{navigationTerrain};
    const auto route = navigation.findPath(navigationWorld,
                                           {-18.0F, 0.0F, 0.0F},
                                           {18.0F, 0.0F, 0.0F},
                                           strategy::collisionRadius("worker"),
                                           999);
    valid = valid && !route.empty();
    for (const glm::vec3& point : route) {
        const float safe =
            strategy::collisionRadius("town_center") + strategy::collisionRadius("worker");
        valid = valid && (point.x * point.x + point.z * point.z >= safe * safe);
    }

    strategy::RtsCamera camera;
    const auto originalFocus = camera.focus();
    camera.pan(1.0F, 0.0F, 1.0F);
    valid = valid && camera.focus().z < originalFocus.z;

    strategy::RtsCamera rotatedCamera;
    rotatedCamera.orbit(-90.0F / 0.28F, 0.0F);
    const auto rotatedStart = rotatedCamera.focus();
    rotatedCamera.pan(1.0F, 0.0F, 1.0F);
    valid = valid && rotatedCamera.focus().x < rotatedStart.x;
    const auto beforeStrafe = rotatedCamera.focus();
    rotatedCamera.pan(0.0F, 1.0F, 1.0F);
    valid = valid && rotatedCamera.focus().z < beforeStrafe.z;
    camera.orbit(10000.0F, 10000.0F);
    valid = valid && camera.pitchDegrees() >= 18.0F && camera.pitchDegrees() <= 78.0F;
    camera.zoom(10000.0F);
    valid = valid && camera.distance() == 16.0F;
    camera.zoom(-10000.0F);
    valid = valid && camera.distance() == 120.0F;

    strategy::ThirdPersonCamera thirdPerson;
    const auto thirdPersonView = thirdPerson.view(16.0F / 9.0F, {2.0F, 3.0F, 4.0F});
    valid = valid && thirdPersonView.position.y > 3.0F;
    valid = valid && thirdPersonView.detailDistance < camera.distance();
    const glm::vec2 thirdPersonForward = thirdPerson.groundMovement(1.0F, 0.0F);
    valid = valid && std::abs(glm::length(thirdPersonForward) - 1.0F) < 0.002F;

    strategy::GameSession session{123U};
    strategy::Entity* playerOneUnit = nullptr;
    strategy::Entity* playerTwoUnit = nullptr;
    for (strategy::Entity& candidate : session.world().entities()) {
        if (candidate.modelKey == "worker" && candidate.authority.owner == 1)
            playerOneUnit = &candidate;
        if (candidate.modelKey == "worker" && candidate.authority.owner == 2)
            playerTwoUnit = &candidate;
    }
    valid = valid && playerOneUnit != nullptr && playerTwoUnit != nullptr;
    valid = valid && session.players().players().size() == 2;
    std::size_t resourceCount = 0;
    for (const strategy::Entity& resource : session.world().entities()) {
        if (resource.authority.owner != 0)
            continue;
        ++resourceCount;
        bool mirrored = false;
        for (const strategy::Entity& candidate : session.world().entities()) {
            if (candidate.authority.owner == 0 && candidate.modelKey == resource.modelKey &&
                std::abs(candidate.transform.position.x + resource.transform.position.x) < 0.001F &&
                std::abs(candidate.transform.position.z + resource.transform.position.z) < 0.001F) {
                mirrored = true;
                break;
            }
        }
        valid = valid && mirrored;
    }
    valid = valid && resourceCount >= 20;
    const auto playerOneStart = playerOneUnit->transform.position;
    valid = valid && session.submit({1, 1, strategy::PossessUnitCommand{playerTwoUnit->id}});
    valid = valid && session.submit({1, 2, strategy::PossessUnitCommand{playerOneUnit->id}});
    valid = valid && session.submit(
                         {1, 3, strategy::DirectUnitInputCommand{playerOneUnit->id, {1.0F, 0.0F}}});
    valid = valid && !session.submit({1, 3, strategy::ReleaseUnitCommand{playerOneUnit->id}});
    session.update(strategy::GameSession::fixedTickSeconds);
    valid = valid && playerTwoUnit->authority.directController == 0;
    valid = valid && playerOneUnit->authority.directController == 1;
    valid = valid && playerOneUnit->transform.position.x > playerOneStart.x;
    const strategy::EntityId movingId = playerOneUnit->id;
    const glm::vec3 beforeCollision = playerOneUnit->transform.position;
    strategy::Entity& obstacle = session.world().createEntity("Obstacle", "town_center", 0);
    obstacle.transform.position = {beforeCollision.x + strategy::collisionRadius("worker") +
                                       strategy::collisionRadius("town_center") + 0.1F,
                                   0.0F,
                                   beforeCollision.z};
    valid =
        valid && session.submit({1, 4, strategy::DirectUnitInputCommand{movingId, {1.0F, 0.0F}}});
    session.update(strategy::GameSession::fixedTickSeconds);
    playerOneUnit = session.world().findEntity(movingId);
    valid = valid && std::abs(playerOneUnit->transform.position.x - beforeCollision.x) < 0.001F;
    std::size_t workersBefore = 0;
    strategy::EntityId hallId = 0;
    for (strategy::Entity& entity : session.world().entities()) {
        if (entity.modelKey == "worker" && entity.authority.owner == 1)
            ++workersBefore;
        if (entity.modelKey == "town_center" && entity.authority.owner == 1) {
            hallId = entity.id;
            entity.production.productionSpeedMultiplier = 1000.0F;
        }
    }
    valid = valid && session.submit({1, 5, strategy::UpgradeTownHallCommand{hallId}});
    valid = valid && session.submit({1, 6, strategy::TrainCharacterCommand{hallId}});
    // The building processes one deterministic order at a time: upgrade, then recruit.
    for (int tick = 0; tick < 460; ++tick)
        session.update(strategy::GameSession::fixedTickSeconds);
    const strategy::Entity* upgradedHall = session.world().findEntity(hallId);
    std::size_t workersAfter = 0;
    for (const strategy::Entity& entity : session.world().entities())
        if (entity.modelKey == "worker" && entity.authority.owner == 1)
            ++workersAfter;
    valid = valid && upgradedHall && upgradedHall->buildingUpgrades.level == 2 &&
            upgradedHall->modelKey == "town_center_level_2" && workersAfter == workersBefore + 1;

    strategy::GameSession gatheringSession{321U};
    strategy::Entity* gatherer = nullptr;
    for (strategy::Entity& entity : gatheringSession.world().entities())
        if (entity.modelKey == "worker" && entity.authority.owner == 1) {
            gatherer = &entity;
            break;
        }
    valid = valid && gatherer != nullptr;
    if (gatherer) {
        strategy::Entity& tree = gatheringSession.world().createEntity("Test Tree", "tree", 0);
        tree.transform.position = gatherer->transform.position;
        tree.resource.emplace();
        tree.resource.kind = strategy::ResourceKind::wood;
        tree.resource.remaining = 2.0F;
        valid = valid && gatheringSession.submit(
                             {1, 1, strategy::GatherResourceCommand{gatherer->id, tree.id}});
        for (int tick = 0; tick < 100; ++tick)
            gatheringSession.update(strategy::GameSession::fixedTickSeconds);
        valid = valid && gatheringSession.players().find(1)->wood > 0.0F;
    }

    strategy::GameSession intelligenceSession{777U};
    strategy::Entity* scout = nullptr;
    strategy::Entity* observed = nullptr;
    for (strategy::Entity& entity : intelligenceSession.world().entities()) {
        if (entity.modelKey == "worker" && entity.authority.owner == 1)
            scout = &entity;
        if (entity.modelKey == "worker" && entity.authority.owner == 2)
            observed = &entity;
    }
    if (scout && observed) {
        observed->transform.position = scout->transform.position + glm::vec3{2, 0, 0};
        intelligenceSession.update(strategy::GameSession::fixedTickSeconds);
        const auto& records = intelligenceSession.players().find(1)->intelligence;
        const auto known = std::find_if(
            records.begin(), records.end(), [&](const strategy::LastKnownEntity& item) {
                return item.id == observed->id;
            });
        valid = valid && known != records.end();
        const glm::vec3 remembered = known != records.end() ? known->position : glm::vec3{};
        observed->transform.position = {65, 0, 65};
        intelligenceSession.update(strategy::GameSession::fixedTickSeconds);
        const auto& after = intelligenceSession.players().find(1)->intelligence;
        const auto stale =
            std::find_if(after.begin(), after.end(), [&](const strategy::LastKnownEntity& item) {
                return item.id == observed->id;
            });
        valid = valid && stale != after.end() && stale->position == remembered;
    } else
        valid = false;

    if (!valid) {
        std::cerr << "Game system validation failed\n";
        return 1;
    }
    std::cout << "Game system validation passed\n";
    return 0;
}
