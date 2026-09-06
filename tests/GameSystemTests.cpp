#include "game/RtsCamera.hpp"
#include "game/ThirdPersonCamera.hpp"
#include "gameplay/GameplayCatalogue.hpp"
#include "simulation/GameSession.hpp"
#include "simulation/CommandCodec.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/Navigation.hpp"
#include "world/World.hpp"

#include <cmath>
#include <iostream>
#include <unordered_set>

int main() {
    const strategy::GameplayCatalogue gameplay;
    std::unordered_set<std::string> countryIds;
    bool valid = gameplay.countries().size() >= 2;
    for (const auto& country : gameplay.countries())
        valid = valid && !country.id.empty() && !country.nameKey.empty() &&
                country.specializationId == "unassigned" && countryIds.insert(country.id).second;
    valid = valid && gameplay.unit(strategy::UnitArchetypeId{"worker"}) != nullptr &&
            gameplay.unit(strategy::UnitArchetypeId{"town_center"}) == nullptr &&
            gameplay.building(strategy::BuildingArchetypeId{"town_center"}) != nullptr &&
            gameplay.building(strategy::BuildingArchetypeId{"worker"}) == nullptr &&
            gameplay.resource(strategy::ResourceArchetypeId{"tree"}) != nullptr &&
            std::abs(gameplay.collisionRadius("worker") - 0.65F) < 0.001F &&
            std::abs(gameplay.collisionRadius("town_center") - 10.5F) < 0.001F;
    const auto* materials = gameplay.resourceType(strategy::ResourceId{"materials"});
    const auto* power = gameplay.resourceType(strategy::ResourceId{"power"});
    const auto* components = gameplay.resourceType(strategy::ResourceId{"components"});
    const auto* workerWeapon = gameplay.weapon(strategy::WeaponId{"worker_unarmed"});
    const auto* constructionDrone =
        gameplay.unit(strategy::UnitArchetypeId{"construction_drone"});
    const auto* commandHub = gameplay.building(strategy::BuildingArchetypeId{"command_hub"});
    const auto* commandHubPower =
        gameplay.powerDevice(strategy::PowerDeviceId{"command_hub_integrated_grid"});
    const auto* droneRecipe =
        gameplay.recipe(strategy::RecipeId{"command_hub.train_construction_drone"});
    const auto* generatorRecipe = gameplay.recipe(strategy::RecipeId{"construct.basic_generator"});
    valid = valid && materials && materials->enabled && power && power->enabled && components &&
            !components->enabled && workerWeapon && workerWeapon->cooldownTicks == 30 &&
            constructionDrone && constructionDrone->movement.type == "flying" &&
            constructionDrone->battery && commandHub && commandHub->powerDevice &&
            commandHubPower && commandHubPower->production == 10.0F && droneRecipe &&
            droneRecipe->producer == "command_hub" && droneRecipe->cost.at("materials") == 75.0F &&
            generatorRecipe && generatorRecipe->producer.empty() &&
            generatorRecipe->product.kind == strategy::RecipeProductKind::building;
    strategy::Entity componentWorker;
    componentWorker.archetype = strategy::EntityArchetypeId{"worker"};
    gameplay.initializeEntity(componentWorker);
    strategy::Entity componentHall;
    componentHall.archetype = strategy::EntityArchetypeId{"town_center"};
    gameplay.initializeEntity(componentHall);
    valid = valid && componentWorker.unitControl && componentWorker.gatherer &&
            componentWorker.upgrades && !componentWorker.production &&
            !componentWorker.buildingUpgrades;
    valid = valid && componentWorker.combat && workerWeapon &&
            std::abs(componentWorker.combat.damage - workerWeapon->damage) < 0.001F &&
            std::abs(componentWorker.combat.range - workerWeapon->range) < 0.001F;
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
        gameplay.productionDuration("spain", "unassigned", "town_center", "construction_drone");
    valid = valid && gameplay.canTrain("town_center", "construction_drone") &&
            !gameplay.canTrain("town_center", "worker") &&
            !gameplay.canTrain("outpost", "worker") && townTraining > 0.0F;
    const float normalDroneTraining = gameplay.productionDuration(
        "united_states", "unassigned", "command_hub", "construction_drone");
    const float specializedDroneTraining = gameplay.productionDuration(
        "united_states", "rapid_deployment", "command_hub", "construction_drone");
    valid = valid && std::abs(normalDroneTraining - 10.0F) < 0.001F &&
            std::abs(specializedDroneTraining - 9.0F) < 0.001F;
    strategy::RuntimeModifierLayers orderedLayers;
    orderedLayers.permanentUpgrades.push_back({"test.permanent",
                                                strategy::GameplayStat::movementSpeed,
                                                {},
                                                strategy::ModifierOperation::overrideValue,
                                                2.0F,
                                                100});
    orderedLayers.producingBuildingUpgrades.push_back({"test.producer",
                                                       strategy::GameplayStat::movementSpeed,
                                                       {},
                                                       strategy::ModifierOperation::add,
                                                       3.0F,
                                                       100});
    orderedLayers.temporaryEffects.push_back({"test.temporary",
                                              strategy::GameplayStat::movementSpeed,
                                              {},
                                              strategy::ModifierOperation::multiply,
                                              2.0F,
                                              100});
    orderedLayers.powerState.push_back({"test.power",
                                        strategy::GameplayStat::movementSpeed,
                                        {},
                                        strategy::ModifierOperation::maximum,
                                        9.0F,
                                        100});
    valid = valid &&
            std::abs(gameplay.resolve(strategy::GameplayStat::movementSpeed,
                                      "germany",
                                      "rapid_deployment",
                                      "worker",
                                      {},
                                      orderedLayers) -
                     9.0F) < 0.001F;
    strategy::RuntimeModifierLayers domainLayers;
    strategy::ModifierTarget weaponTarget;
    weaponTarget.weaponTags = {"melee"};
    domainLayers.permanentUpgrades.push_back({"test.weapon_target",
                                              strategy::GameplayStat::attackDamage,
                                              weaponTarget,
                                              strategy::ModifierOperation::multiply,
                                              2.0F,
                                              100});
    strategy::ModifierTarget resourceTarget;
    resourceTarget.resourceTypes = {"materials"};
    domainLayers.permanentUpgrades.push_back({"test.resource_target",
                                              strategy::GameplayStat::gatherRate,
                                              resourceTarget,
                                              strategy::ModifierOperation::multiply,
                                              2.0F,
                                              110});
    strategy::ModifierTarget powerTarget;
    powerTarget.powerDeviceTags = {"headquarters"};
    domainLayers.permanentUpgrades.push_back({"test.power_target",
                                              strategy::GameplayStat::productionSpeed,
                                              powerTarget,
                                              strategy::ModifierOperation::multiply,
                                              2.0F,
                                              120});
    valid = valid && std::abs(gameplay.resolve(strategy::GameplayStat::attackDamage,
                                               "united_states",
                                               "unassigned",
                                               "worker",
                                               {},
                                               domainLayers) -
                                    16.0F) <
                         0.001F &&
            std::abs(gameplay.resolve(strategy::GameplayStat::gatherRate,
                                      "united_states",
                                      "unassigned",
                                      "worker",
                                      {},
                                      domainLayers,
                                      strategy::ResourceId{"materials"}) -
                     8.0F) < 0.001F &&
            std::abs(gameplay.resolve(strategy::GameplayStat::productionSpeed,
                                      "united_states",
                                      "unassigned",
                                      "command_hub",
                                      {},
                                      domainLayers) -
                     2.0F) < 0.001F;
    strategy::World world;
    const strategy::EntityId first = world.createEntity("First").id;
    const strategy::EntityId second = world.createEntity("Second", "house").id;
    valid = valid && first != second && world.size() == 2;
    valid = valid && world.findEntity(first) != nullptr;
    valid = valid && world.findEntity(second)->archetype.value == "house";
    valid = valid && world.destroyEntity(first) && world.findEntity(first) == nullptr;
    valid = valid && world.size() == 1;

    strategy::World navigationWorld;
    strategy::Entity& navigationObstacle =
        navigationWorld.createEntity("Blocker", "town_center", 0);
    navigationObstacle.transform.position = {0.0F, 0.0F, 0.0F};
    const strategy::Terrain navigationTerrain{123U};
    strategy::Navigation navigation{navigationTerrain, gameplay};
    const auto route = navigation.findPath(navigationWorld,
                                           {-18.0F, 0.0F, 0.0F},
                                           {18.0F, 0.0F, 0.0F},
                                           strategy::collisionRadius(gameplay, "worker"),
                                           999);
    valid = valid && !route.empty();
    for (const glm::vec3& point : route) {
        const float safe =
            strategy::collisionRadius(gameplay, "town_center") +
            strategy::collisionRadius(gameplay, "worker");
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

    strategy::GameSession session{gameplay, 123U};
    strategy::Entity* playerOneUnit = nullptr;
    strategy::Entity* playerTwoUnit = nullptr;
    for (strategy::Entity& candidate : session.world().entities()) {
        if (candidate.archetype.value == "worker" && candidate.authority.owner == 1)
            playerOneUnit = &candidate;
        if (candidate.archetype.value == "worker" && candidate.authority.owner == 2)
            playerTwoUnit = &candidate;
    }
    valid = valid && playerOneUnit != nullptr && playerTwoUnit != nullptr;
    valid = valid && session.players().players().size() == 2;

    strategy::GameSession replayA{gameplay, 123U}, replayB{gameplay, 123U};
    valid = valid && replayA.stateChecksum() == replayB.stateChecksum();
    const strategy::PlayerCommand wireCommand{
        1, 50, strategy::MoveUnitCommand{1, {-12.0F, 0.0F, -10.0F}}};
    const auto encoded = strategy::CommandCodec::encode(wireCommand);
    const auto decoded = strategy::CommandCodec::decode(encoded);
    valid = valid && decoded && decoded->player == wireCommand.player &&
            decoded->sequence == wireCommand.sequence &&
            std::holds_alternative<strategy::MoveUnitCommand>(decoded->payload) &&
            std::get<strategy::MoveUnitCommand>(decoded->payload).destination ==
                std::get<strategy::MoveUnitCommand>(wireCommand.payload).destination;
    if (decoded) {
        const bool replaySubmitted = replayA.submit(*decoded) && replayB.submit(*decoded);
        valid = replaySubmitted && valid;
        replayA.advanceTicks(12);
        replayB.advanceTicks(12);
        valid = valid && replayA.stateChecksum() == replayB.stateChecksum();
        if (strategy::Entity* cached = replayB.world().findEntity(1))
            cached->transient.navigationPath.push_back({999.0F, 0.0F, 999.0F});
        valid = valid && replayA.stateChecksum() == replayB.stateChecksum();
    }
    const strategy::PlayerCommand recipeWireCommand{
        1, 51, strategy::StartRecipeCommand{1, "town_center.train_construction_drone"}};
    const auto encodedRecipe = strategy::CommandCodec::encode(recipeWireCommand);
    const auto decodedRecipe = strategy::CommandCodec::decode(encodedRecipe);
    (void)decodedRecipe;
    const strategy::PlayerCommand placementWireCommand{
        1,
        52,
        strategy::PlaceBuildingCommand{7,
                                       "construct.command_hub",
                                       {12.0F, 0.0F, -9.0F},
                                       {7, 11, 15}}};
    const auto encodedPlacement = strategy::CommandCodec::encode(placementWireCommand);
    const auto decodedPlacement = strategy::CommandCodec::decode(encodedPlacement);
    valid = valid && decodedPlacement &&
            std::holds_alternative<strategy::PlaceBuildingCommand>(decodedPlacement->payload) &&
            std::get<strategy::PlaceBuildingCommand>(decodedPlacement->payload).builders ==
                std::vector<strategy::EntityId>{7, 11, 15};
    std::size_t resourceCount = 0;
    for (const strategy::Entity& resource : session.world().entities()) {
        if (resource.authority.owner != 0)
            continue;
        ++resourceCount;
        bool mirrored = false;
        for (const strategy::Entity& candidate : session.world().entities()) {
            if (candidate.authority.owner == 0 && candidate.archetype.value == resource.archetype.value &&
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
    valid = session.submit({1, 1, strategy::PossessUnitCommand{playerTwoUnit->id}}) && valid;
    valid = session.submit({1, 2, strategy::PossessUnitCommand{playerOneUnit->id}}) && valid;
    valid = session.submit(
                         {1, 3, strategy::DirectUnitInputCommand{playerOneUnit->id, {1.0F, 0.0F}}}) && valid;
    valid = !session.submit({1, 3, strategy::ReleaseUnitCommand{playerOneUnit->id}}) && valid;
    session.update(strategy::GameSession::fixedTickSeconds);
    valid = valid && playerTwoUnit->authority.directController == 0;
    valid = valid && playerOneUnit->authority.directController == 1;
    valid = valid && playerOneUnit->transform.position.x > playerOneStart.x;
    const strategy::EntityId movingId = playerOneUnit->id;
    const glm::vec3 beforeCollision = playerOneUnit->transform.position;
    strategy::Entity& obstacle = session.world().createEntity("Obstacle", "town_center", 0);
    obstacle.transform.position = {beforeCollision.x +
                                       strategy::collisionRadius(gameplay, "worker") +
                                       strategy::collisionRadius(gameplay, "town_center") + 0.1F,
                                   0.0F,
                                   beforeCollision.z};
    valid =
        session.submit({1, 4, strategy::DirectUnitInputCommand{movingId, {1.0F, 0.0F}}}) && valid;
    session.update(strategy::GameSession::fixedTickSeconds);
    playerOneUnit = session.world().findEntity(movingId);
    valid = valid && std::abs(playerOneUnit->transform.position.x - beforeCollision.x) < 0.001F;
    std::size_t dronesBefore = 0;
    strategy::EntityId hallId = 0;
    for (strategy::Entity& entity : session.world().entities()) {
        if (entity.archetype.value == "construction_drone" && entity.authority.owner == 1)
            ++dronesBefore;
        if (entity.archetype.value == "town_center" && entity.authority.owner == 1) {
            hallId = entity.id;
            entity.production.productionSpeedMultiplier = 1000.0F;
        }
    }
    valid = session.submit(
                         {1,
                          6,
                          strategy::StartRecipeCommand{hallId, "town_center.train_construction_drone"}}) && valid;
    for (int tick = 0; tick < 460; ++tick)
        session.update(strategy::GameSession::fixedTickSeconds);
    const strategy::Entity* upgradedHall = session.world().findEntity(hallId);
    std::size_t dronesAfter = 0;
    for (const strategy::Entity& entity : session.world().entities())
        if (entity.archetype.value == "construction_drone" && entity.authority.owner == 1)
            ++dronesAfter;
    valid = valid && upgradedHall && dronesAfter == dronesBefore + 1;

    strategy::GameSession recipeSession{gameplay, 654U};
    recipeSession.replaceWorld({}, 654U);
    strategy::Entity& commandHubEntity =
        recipeSession.world().createEntity("Command Hub", "command_hub", 1);
    gameplay.initializeEntity(commandHubEntity);
    commandHubEntity.transform.position = {0.0F, 0.0F, 0.0F};
    recipeSession.players().find(1)->resources["materials"] = 75.0F;
    valid = recipeSession.submit(
                         {1,
                          1,
                          strategy::StartRecipeCommand{
                              commandHubEntity.id,
                              "command_hub.train_construction_drone"}}) && valid;
    recipeSession.advanceTicks(300);
    std::size_t constructedDrones = 0;
    for (const strategy::Entity& entity : recipeSession.world().entities())
        if (entity.archetype.value == "construction_drone" && entity.authority.owner == 1)
            ++constructedDrones;
    valid = valid && constructedDrones == 1 &&
            recipeSession.players().find(1)->resources.at("materials") == 0.0F;

    strategy::Entity& researcher =
        recipeSession.world().createEntity("Outpost", "outpost", 1);
    gameplay.initializeEntity(researcher);
    valid = recipeSession.submit(
                {1, 2, strategy::StartUpgradeCommand{researcher.id, "production.efficient_training"}}) && valid;
    recipeSession.advanceTicks(300);
    valid = valid && researcher.upgrades &&
            researcher.upgrades.levels["production.efficient_training"] == 1;

    strategy::GameSession gatheringSession{gameplay, 321U};
    strategy::Entity* gatherer = nullptr;
    for (strategy::Entity& entity : gatheringSession.world().entities())
        if (entity.archetype.value == "worker" && entity.authority.owner == 1) {
            gatherer = &entity;
            break;
        }
    valid = valid && gatherer != nullptr;
    if (gatherer) {
        strategy::Entity& tree = gatheringSession.world().createEntity("Test Tree", "tree", 0);
        tree.transform.position = gatherer->transform.position;
        tree.resource.emplace();
        tree.resource.type = "materials";
        tree.resource.remaining = 2.0F;
        valid = gatheringSession.submit(
                             {1, 1, strategy::GatherResourceCommand{gatherer->id, tree.id}}) && valid;
        for (int tick = 0; tick < 100; ++tick)
            gatheringSession.update(strategy::GameSession::fixedTickSeconds);
        valid = valid && gatheringSession.players().find(1)->wood > 0.0F;
    }

    strategy::GameSession intelligenceSession{gameplay, 777U};
    strategy::Entity* scout = nullptr;
    strategy::Entity* observed = nullptr;
    for (strategy::Entity& entity : intelligenceSession.world().entities()) {
        if (entity.archetype.value == "worker" && entity.authority.owner == 1)
            scout = &entity;
        if (entity.archetype.value == "worker" && entity.authority.owner == 2)
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
