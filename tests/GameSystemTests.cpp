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
#include <array>
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
            gameplay.resource(strategy::ResourceArchetypeId{"scrap_field"}) != nullptr &&
            std::abs(gameplay.collisionRadius("worker") - 0.65F) < 0.001F &&
            std::abs(gameplay.collisionRadius("town_center") - 10.5F) < 0.001F;
    const auto* alloy = gameplay.resourceType(strategy::ResourceId{"alloy"});
    const auto* power = gameplay.resourceType(strategy::ResourceId{"power"});
    const auto* scrap = gameplay.resourceType(strategy::ResourceId{"scrap"});
    const auto* workerWeapon = gameplay.weapon(strategy::WeaponId{"worker_unarmed"});
    const auto* constructionDrone =
        gameplay.unit(strategy::UnitArchetypeId{"construction_drone"});
    const auto* commandHub = gameplay.building(strategy::BuildingArchetypeId{"command_hub"});
    const auto* chargingPad = gameplay.building(strategy::BuildingArchetypeId{"charging_pad"});
    const auto* extractor =
        gameplay.building(strategy::BuildingArchetypeId{"resource_extractor"});
    const auto* factory = gameplay.building(strategy::BuildingArchetypeId{"drone_factory"});
    const auto* sensorTower = gameplay.building(strategy::BuildingArchetypeId{"sensor_tower"});
    const auto* commandHubPower =
        gameplay.powerDevice(strategy::PowerDeviceId{"command_hub_integrated_grid"});
    const auto* droneRecipe =
        gameplay.recipe(strategy::RecipeId{"command_hub.train_construction_drone"});
    for (const std::string& building : gameplay.matchRules().buildPalette) {
        const auto* construction =
            gameplay.recipe(strategy::RecipeId{"construct." + building});
        valid = valid && construction && construction->constructionPower > 0.0F &&
                construction->workStep > 0.0F && construction->dronePowerPerStep > 0.0F;
    }
    const auto* generatorRecipe = gameplay.recipe(strategy::RecipeId{"construct.basic_generator"});
    const auto* scrapConversion = gameplay.conversionFor(
        strategy::BuildingArchetypeId{"alloy_processor"}, strategy::ResourceId{"scrap"});
    const auto* syntheticAlloyConversion = gameplay.conversionFor(
        strategy::BuildingArchetypeId{"alloy_processor"}, strategy::ResourceId{"synthetic"});
    const auto* syntheticFuelConversion = gameplay.conversionFor(
        strategy::BuildingArchetypeId{"fuel_processor"}, strategy::ResourceId{"synthetic"});
    const auto* hubScrapConversion = gameplay.conversionFor(
        strategy::BuildingArchetypeId{"command_hub"}, strategy::ResourceId{"scrap"});
    const auto* hubOilConversion = gameplay.conversionFor(
        strategy::BuildingArchetypeId{"command_hub"}, strategy::ResourceId{"oil"});
    valid = valid && alloy && alloy->enabled && power && power->enabled && scrap &&
            scrap->storage == strategy::ResourceStorageKind::cargo && workerWeapon && workerWeapon->cooldownTicks == 30 &&
            constructionDrone && constructionDrone->movement.type == "flying" &&
            constructionDrone->battery && commandHub && commandHub->powerDevice &&
            chargingPad && chargingPad->powerDevice && extractor && extractor->powerDevice &&
            factory && factory->powerDevice && sensorTower && sensorTower->powerDevice &&
            commandHubPower && commandHubPower->production == 10.0F && droneRecipe &&
            droneRecipe->producer == "command_hub" && droneRecipe->cost.at("alloy") == 75.0F &&
            generatorRecipe && generatorRecipe->producer.empty() &&
            generatorRecipe->product.kind == strategy::RecipeProductKind::building &&
            scrapConversion && scrapConversion->output.value == "alloy" &&
            syntheticAlloyConversion && syntheticAlloyConversion->output.value == "alloy" &&
            syntheticFuelConversion && syntheticFuelConversion->output.value == "fuel" &&
            std::abs(syntheticFuelConversion->outputPerInput - 1.5F) < 0.001F &&
            hubScrapConversion && hubScrapConversion->output.value == "alloy" &&
            std::abs(hubScrapConversion->outputPerInput - 0.5F) < 0.001F &&
            hubOilConversion && std::abs(hubOilConversion->outputPerInput - 0.5F) < 0.001F &&
            !gameplay.acceptsResource(strategy::BuildingArchetypeId{"command_hub"},
                                      strategy::ResourceId{"synthetic"});
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
    strategy::World airborneOccupancy;
    strategy::Entity& airborneDrone =
        airborneOccupancy.createEntity("Drone", "construction_drone", 1);
    gameplay.initializeEntity(airborneDrone);
    airborneDrone.transform.position = {4.0F, 6.0F, 4.0F};
    valid = valid && !strategy::overlapsObject(
                         airborneOccupancy, gameplay, {4.0F, 4.0F}, 10.5F);
    strategy::Entity& groundWorker = airborneOccupancy.createEntity("Worker", "worker", 1);
    gameplay.initializeEntity(groundWorker);
    groundWorker.transform.position = {4.0F, 0.0F, 4.0F};
    valid = valid && strategy::overlapsObject(
                         airborneOccupancy, gameplay, {4.0F, 4.0F}, 10.5F);
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
    resourceTarget.resourceTypes = {"alloy"};
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
                                      strategy::ResourceId{"alloy"}) -
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
    strategy::Navigation navigation{navigationTerrain, gameplay, 13};
    valid = valid && navigation.cellsPerSide() ==
                           static_cast<int>(std::ceil(
                               strategy::MapArea{13}.extent() / strategy::Navigation::cellSize));
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
    const strategy::SpatialShape navigationTarget =
        strategy::spatialShape(gameplay, navigationObstacle);
    for (const glm::vec3 start : std::array<glm::vec3, 4>{
             glm::vec3{-30.0F, 0.0F, 0.0F}, glm::vec3{30.0F, 0.0F, 0.0F},
             glm::vec3{0.0F, 0.0F, -30.0F}, glm::vec3{0.0F, 0.0F, 30.0F}}) {
        const auto interactionRoute = navigation.findPath(
            navigationWorld, start,
            strategy::NavigationGoalRegion{navigationTarget, 0.9F, {start.x, start.z},
                                           navigationObstacle.id},
            strategy::collisionRadius(gameplay, "worker"), 999);
        valid = valid && !interactionRoute.empty();
        if (!interactionRoute.empty()) {
            const glm::vec3 endpoint = interactionRoute.back();
            valid = valid && strategy::signedDistance(
                    navigationTarget, {endpoint.x, endpoint.z}) <= 1.55F;
        }
    }
    navigationObstacle.transform.rotationDegrees.y = 37.0F;
    const strategy::SpatialShape rotatedTarget =
        strategy::spatialShape(gameplay, navigationObstacle);
    const auto rotatedRoute = navigation.findPath(
        navigationWorld, {-32.0F, 0.0F, 7.0F},
        strategy::NavigationGoalRegion{rotatedTarget, 0.9F, {-32.0F, 7.0F},
                                       navigationObstacle.id, 42},
        1.25F, 42);
    valid = valid && !rotatedRoute.empty();
    if (!rotatedRoute.empty())
        valid = valid && strategy::signedDistance(
                rotatedTarget, {rotatedRoute.back().x, rotatedRoute.back().z}) <= 2.15F;

    strategy::Entity& sideBlocker =
        navigationWorld.createEntity("Side blocker", "alloy_processor", 0);
    sideBlocker.transform.position = {-19.0F, 0.0F, 0.0F};
    const auto blockedSideRoute = navigation.findPath(
        navigationWorld, {-36.0F, 0.0F, 0.0F},
        strategy::NavigationGoalRegion{rotatedTarget, 0.9F, {-36.0F, 0.0F},
                                       navigationObstacle.id, 43},
        strategy::collisionRadius(gameplay, "worker"), 43);
    valid = valid && !blockedSideRoute.empty();

    navigation.rebuildTerrain(navigationTerrain, 17);
    valid = valid && navigation.cellsPerSide() == static_cast<int>(std::ceil(
                           strategy::MapArea{17}.extent() / strategy::Navigation::cellSize));

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
    std::size_t playerOneHubs = 0, playerTwoHubs = 0;
    std::size_t playerOneDrones = 0, playerTwoDrones = 0;
    for (strategy::Entity& candidate : session.world().entities()) {
        if (candidate.archetype.value == "worker" && candidate.authority.owner == 1)
            playerOneUnit = &candidate;
        if (candidate.archetype.value == "worker" && candidate.authority.owner == 2)
            playerTwoUnit = &candidate;
        if (candidate.archetype.value == "command_hub") {
            if (candidate.authority.owner == 1) ++playerOneHubs;
            if (candidate.authority.owner == 2) ++playerTwoHubs;
        }
        if (candidate.archetype.value == "construction_drone") {
            if (candidate.authority.owner == 1) ++playerOneDrones;
            if (candidate.authority.owner == 2) ++playerTwoDrones;
        }
    }
    valid = valid && playerOneUnit != nullptr && playerTwoUnit != nullptr;
    valid = valid && playerOneHubs == 1 && playerTwoHubs == 1 &&
            playerOneDrones == 1 && playerTwoDrones == 1;
    valid = valid && playerOneUnit && playerOneUnit->unitControl &&
            playerOneUnit->unitControl.directlyControllable && !playerOneUnit->gatherer;
    valid = valid && playerTwoUnit && playerTwoUnit->unitControl &&
            playerTwoUnit->unitControl.directlyControllable && !playerTwoUnit->gatherer;
    if (playerOneHubs != 1 || playerTwoHubs != 1 || playerOneDrones != 1 ||
        playerTwoDrones != 1 || !playerOneUnit || playerOneUnit->gatherer ||
        !playerTwoUnit || playerTwoUnit->gatherer)
        std::cerr << "Starting roster validation failed\n";
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
    const strategy::PlayerCommand cancelWireCommand{
        1, 53, strategy::CancelProductionCommand{7, 3}};
    const auto decodedCancel =
        strategy::CommandCodec::decode(strategy::CommandCodec::encode(cancelWireCommand));
    valid = valid && decodedCancel &&
            std::holds_alternative<strategy::CancelProductionCommand>(decodedCancel->payload) &&
            std::get<strategy::CancelProductionCommand>(decodedCancel->payload).queueIndex == 3;
    const strategy::PlayerCommand cancelConstructionWireCommand{
        1, 54, strategy::CancelConstructionCommand{23}};
    const auto decodedConstructionCancel = strategy::CommandCodec::decode(
        strategy::CommandCodec::encode(cancelConstructionWireCommand));
    valid = valid && decodedConstructionCancel &&
            std::holds_alternative<strategy::CancelConstructionCommand>(
                decodedConstructionCancel->payload) &&
            std::get<strategy::CancelConstructionCommand>(
                decodedConstructionCancel->payload).entity == 23;
    const auto decodedRecharge = strategy::CommandCodec::decode(strategy::CommandCodec::encode(
        {1, 55, strategy::RechargeCommand{7}}));
    const auto decodedStop = strategy::CommandCodec::decode(strategy::CommandCodec::encode(
        {1, 56, strategy::StopUnitCommand{7}}));
    valid = valid && decodedRecharge && decodedStop &&
            std::holds_alternative<strategy::RechargeCommand>(decodedRecharge->payload) &&
            std::holds_alternative<strategy::StopUnitCommand>(decodedStop->payload);
    const auto decodedGather = strategy::CommandCodec::decode(strategy::CommandCodec::encode(
        {1, 57, strategy::GatherResourceCommand{7, 80, 90}}));
    const auto decodedDelivery = strategy::CommandCodec::decode(strategy::CommandCodec::encode(
        {1, 58, strategy::DeliverResourceCommand{7, 90}}));
    const auto decodedPreference = strategy::CommandCodec::decode(strategy::CommandCodec::encode(
        {1, 59, strategy::SetPreferredProcessorCommand{7, 90}}));
    const auto decodedOutput = strategy::CommandCodec::decode(strategy::CommandCodec::encode(
        {1, 60, strategy::SetDeliveryOutputCommand{7, "fuel"}}));
    valid = valid && decodedGather && decodedDelivery && decodedPreference && decodedOutput &&
            std::get<strategy::GatherResourceCommand>(decodedGather->payload).processor == 90 &&
            std::get<strategy::DeliverResourceCommand>(decodedDelivery->payload).processor == 90 &&
            std::get<strategy::SetPreferredProcessorCommand>(decodedPreference->payload).processor == 90 &&
            std::get<strategy::SetDeliveryOutputCommand>(decodedOutput->payload).output == "fuel";
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
    for (const glm::vec2 base : {glm::vec2{-28.0F, -28.0F}, glm::vec2{28.0F, 28.0F}}) {
        std::size_t nearbyScrap = 0;
        float nearbyCapacity = 0.0F;
        for (const strategy::Entity& resource : session.world().entities()) {
            if (resource.archetype.value != "scrap_field" || !resource.resource) continue;
            const glm::vec2 position{resource.transform.position.x, resource.transform.position.z};
            if (glm::distance(position, base) <= 24.01F) {
                ++nearbyScrap;
                nearbyCapacity += resource.resource.remaining;
            }
        }
        valid = valid && nearbyScrap >= 3 && nearbyCapacity >= 480.0F;
    }

    strategy::GameSession configuredMatch{gameplay, 123U, "spain", "japan",
        "unassigned", "unassigned", 20, 2.0F, 1.5F};
    valid = valid &&
            configuredMatch.players().find(1)->resources.at("alloy") == 300.0F &&
            configuredMatch.players().find(2)->resources.at("alloy") == 300.0F;
    std::size_t configuredResourceCount = 0;
    for (const strategy::Entity& entity : configuredMatch.world().entities())
        if (entity.authority.owner == 0) ++configuredResourceCount;
    valid = valid && configuredResourceCount > resourceCount;
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
        if (entity.archetype.value == "command_hub" && entity.authority.owner == 1) {
            hallId = entity.id;
            entity.production.productionSpeedMultiplier = 1000.0F;
        }
    }
    const bool startingHubQueued = session.submit(
        {1, 6,
         strategy::StartRecipeCommand{hallId, "command_hub.train_construction_drone"}});
    valid = startingHubQueued && valid;
    for (int tick = 0; tick < 460; ++tick)
        session.update(strategy::GameSession::fixedTickSeconds);
    const strategy::Entity* upgradedHall = session.world().findEntity(hallId);
    std::size_t dronesAfter = 0;
    for (const strategy::Entity& entity : session.world().entities())
        if (entity.archetype.value == "construction_drone" && entity.authority.owner == 1)
            ++dronesAfter;
    valid = valid && upgradedHall && dronesAfter == dronesBefore + 1;
    if (!startingHubQueued || !upgradedHall || dronesAfter != dronesBefore + 1)
        std::cerr << "Starting command hub production validation failed\n";

    strategy::GameSession recipeSession{gameplay, 654U};
    recipeSession.replaceWorld({}, 654U);
    strategy::Entity& commandHubEntity =
        recipeSession.world().createEntity("Command Hub", "command_hub", 1);
    gameplay.initializeEntity(commandHubEntity);
    commandHubEntity.transform.position = {0.0F, 0.0F, 0.0F};
    recipeSession.players().find(1)->resources["alloy"] = 75.0F;
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
            recipeSession.players().find(1)->resources.at("alloy") == 0.0F;

    strategy::Entity& researcher =
        recipeSession.world().createEntity("Outpost", "outpost", 1);
    gameplay.initializeEntity(researcher);
    valid = recipeSession.submit(
                {1, 2, strategy::StartUpgradeCommand{researcher.id, "production.efficient_training"}}) && valid;
    recipeSession.advanceTicks(300);
    valid = valid && researcher.upgrades &&
            researcher.upgrades.levels["production.efficient_training"] == 1;
    strategy::ProductionOrder cancellableUpgrade;
    cancellableUpgrade.kind = strategy::ProductionKind::improveTraining;
    cancellableUpgrade.upgradeId = "production.efficient_training";
    cancellableUpgrade.durationTicks = 300;
    cancellableUpgrade.remainingTicks = 200;
    cancellableUpgrade.reservedCosts["alloy"] = 12.0F;
    researcher.production.queue.push_back(cancellableUpgrade);
    const float materialsBeforeRefund =
        recipeSession.players().find(1)->resources["alloy"];
    valid = recipeSession.submit(
                {1, 3, strategy::CancelProductionCommand{researcher.id, 0}}) && valid;
    recipeSession.advanceTicks();
    valid = valid && researcher.production.queue.empty() &&
            recipeSession.players().find(1)->resources["alloy"] ==
                materialsBeforeRefund + 12.0F;

    strategy::ProductionOrder cancellableUnit;
    cancellableUnit.kind = strategy::ProductionKind::trainCharacter;
    cancellableUnit.productId = "construction_drone";
    cancellableUnit.durationTicks = 300;
    cancellableUnit.remainingTicks = 200;
    cancellableUnit.reservedCosts["alloy"] = 25.0F;
    researcher.production.queue.push_back(cancellableUnit);
    const float materialsBeforeUnitRefund =
        recipeSession.players().find(1)->resources["alloy"];
    valid = recipeSession.submit(
                {1, 4, strategy::CancelProductionCommand{researcher.id, 0}}) && valid;
    recipeSession.advanceTicks();
    valid = valid && researcher.production.queue.empty() &&
            recipeSession.players().find(1)->resources["alloy"] ==
                materialsBeforeUnitRefund + 25.0F;

    strategy::GameSession constructionPowerSession{gameplay, 999U};
    constructionPowerSession.replaceWorld({}, 999U);
    strategy::Entity& powerDrone = constructionPowerSession.world().createEntity(
        "Builder", "construction_drone", 1);
    gameplay.initializeEntity(powerDrone);
    powerDrone.transform.position = {0.0F, 6.0F, 0.0F};
    const strategy::EntityId powerDroneId = powerDrone.id;
    strategy::Entity& poweredBuild = constructionPowerSession.world().createEntity(
        "Generator", "basic_generator", 1);
    gameplay.initializeEntity(poweredBuild);
    poweredBuild.transform.position = {0.0F, 0.0F, 0.0F};
    poweredBuild.construction.emplace();
    poweredBuild.construction.recipeId = "construct.basic_generator";
    poweredBuild.construction.powerRequired = 100.0F;
    const strategy::EntityId poweredBuildId = poweredBuild.id;
    const float batteryBeforeConstruction =
        constructionPowerSession.world().findEntity(powerDroneId)->battery.charge;
    valid = constructionPowerSession.submit(
                {1, 1, strategy::ConstructCommand{powerDroneId, poweredBuildId}}) && valid;
    constructionPowerSession.advanceTicks();
    const strategy::Entity* updatedDrone = constructionPowerSession.world().findEntity(powerDroneId);
    const strategy::Entity* updatedBuild = constructionPowerSession.world().findEntity(poweredBuildId);
    valid = valid && updatedDrone && updatedBuild &&
            updatedBuild->construction.powerProgress == 2.0F &&
            updatedBuild->health &&
            std::abs(updatedBuild->health.current - updatedBuild->health.maximum * 0.02F) < 0.001F &&
            updatedDrone->battery.charge == batteryBeforeConstruction - 1.0F;

    strategy::Entity& chargingHub = constructionPowerSession.world().createEntity(
        "Command Hub", "command_hub", 1);
    gameplay.initializeEntity(chargingHub);
    chargingHub.transform.position = {0.0F, 0.0F, 0.0F};
    strategy::Entity* rechargingDrone =
        constructionPowerSession.world().findEntity(powerDroneId);
    rechargingDrone->battery.charge = 98.0F;
    valid = constructionPowerSession.submit(
                {1, 2, strategy::RechargeCommand{powerDroneId}}) && valid;
    constructionPowerSession.advanceTicks();
    rechargingDrone = constructionPowerSession.world().findEntity(powerDroneId);
    valid = valid && !rechargingDrone->battery.returningToCharge &&
            !rechargingDrone->battery.hasSuspendedOrder &&
            rechargingDrone->unitControl.order == strategy::UnitOrderKind::construct;

    rechargingDrone->battery.charge = 10.0F;
    valid = constructionPowerSession.submit(
                {1, 3, strategy::MoveUnitCommand{powerDroneId, {20.0F, 6.0F, 20.0F}}}) && valid;
    constructionPowerSession.advanceTicks();
    rechargingDrone = constructionPowerSession.world().findEntity(powerDroneId);
    valid = valid && !rechargingDrone->battery.returningToCharge &&
            rechargingDrone->unitControl.order == strategy::UnitOrderKind::move;

    strategy::Entity* depletedDrone =
        constructionPowerSession.world().findEntity(powerDroneId);
    depletedDrone->battery.charge = 0.0F;
    const glm::vec3 depletedPosition = depletedDrone->transform.position;
    valid = constructionPowerSession.submit(
                {1, 4, strategy::MoveUnitCommand{powerDroneId, {20.0F, 6.0F, 20.0F}}}) && valid;
    constructionPowerSession.advanceTicks();
    valid = valid && constructionPowerSession.world().findEntity(powerDroneId)->transform.position ==
                           depletedPosition &&
            constructionPowerSession.world().findEntity(powerDroneId)->battery.returningToCharge;
    const strategy::EntityId chargingHubId = chargingHub.id;
    constructionPowerSession.world().destroyEntity(chargingHubId);
    constructionPowerSession.advanceTicks();
    valid = valid &&
            constructionPowerSession.world().findEntity(powerDroneId)->unitControl.order ==
                strategy::UnitOrderKind::stranded;
    strategy::Entity& replacementHub = constructionPowerSession.world().createEntity(
        "Replacement Hub", "command_hub", 1);
    gameplay.initializeEntity(replacementHub);
    replacementHub.transform.position = {30.0F, 0.0F, 0.0F};
    constructionPowerSession.advanceTicks();
    valid = valid &&
            constructionPowerSession.world().findEntity(powerDroneId)->unitControl.order ==
                strategy::UnitOrderKind::returningToCharge;
    valid = constructionPowerSession.submit(
                {1, 5, strategy::StopUnitCommand{powerDroneId}}) && valid;
    constructionPowerSession.advanceTicks();
    valid = valid &&
            constructionPowerSession.world().findEntity(powerDroneId)->unitControl.order ==
                strategy::UnitOrderKind::idle &&
            !constructionPowerSession.world().findEntity(powerDroneId)->battery.returningToCharge;

    strategy::Entity& unfinished =
        recipeSession.world().createEntity("Unfinished Hub", "command_hub", 1);
    gameplay.initializeEntity(unfinished);
    unfinished.construction.emplace();
    unfinished.construction.recipeId = "construct.command_hub";
    unfinished.construction.powerRequired = 120.0F;
    unfinished.construction.powerProgress = 30.0F;
    unfinished.construction.state = strategy::BuildingLifecycleState::underConstruction;
    const strategy::EntityId unfinishedId = unfinished.id;
    valid = valid && !strategy::isOperational(unfinished) &&
            !recipeSession.canStartRecipe(
                1, unfinishedId,
                strategy::RecipeId{"command_hub.train_construction_drone"});
    recipeSession.players().find(1)->resources["alloy"] = 0.0F;
    valid = recipeSession.submit(
                {1, 5, strategy::CancelConstructionCommand{unfinishedId}}) && valid;
    recipeSession.advanceTicks();
    valid = valid && recipeSession.world().findEntity(unfinishedId) == nullptr &&
            std::abs(recipeSession.players().find(1)->resources["alloy"] - 150.0F) < 0.001F;

    strategy::GameSession gatheringSession{gameplay, 321U};
    strategy::Entity* startingWorker = nullptr;
    for (strategy::Entity& entity : gatheringSession.world().entities())
        if (entity.archetype.value == "worker" && entity.authority.owner == 1) {
            startingWorker = &entity;
            break;
        }
    valid = valid && startingWorker != nullptr && !startingWorker->gatherer;
    if (startingWorker) {
        strategy::Entity& tree = gatheringSession.world().createEntity("Test Scrap", "scrap_field", 0);
        tree.transform.position = startingWorker->transform.position;
        tree.resource.emplace();
        tree.resource.type = "scrap";
        tree.resource.remaining = 2.0F;
        valid = gatheringSession.submit(
                    {1, 1,
                     strategy::GatherResourceCommand{startingWorker->id, tree.id}}) && valid;
        gatheringSession.advanceTicks();
        valid = valid && startingWorker->unitControl.order == strategy::UnitOrderKind::idle &&
                tree.resource.remaining == 2.0F;
    }

    strategy::GameSession conversionSession{gameplay, 322U};
    conversionSession.replaceWorld({}, 322U);
    conversionSession.players().find(1)->resources["alloy"] = 0.0F;
    strategy::Entity& conversionHub = conversionSession.world().createEntity(
        "Grid source", "command_hub", 1);
    gameplay.initializeEntity(conversionHub);
    const strategy::EntityId conversionHubId = conversionHub.id;
    strategy::Entity& alloyProcessor = conversionSession.world().createEntity(
        "Alloy Processor", "alloy_processor", 1);
    gameplay.initializeEntity(alloyProcessor);
    const strategy::EntityId alloyProcessorId = alloyProcessor.id;
    strategy::Entity& deliveryDrone = conversionSession.world().createEntity(
        "Delivery Drone", "construction_drone", 1);
    gameplay.initializeEntity(deliveryDrone);
    const strategy::EntityId deliveryDroneId = deliveryDrone.id;
    conversionSession.world().findEntity(alloyProcessorId)->transform.position = {0.0F, 0.0F, 0.0F};
    conversionSession.world().findEntity(conversionHubId)->transform.position = {20.0F, 0.0F, 0.0F};
    strategy::Entity* activeDeliveryDrone = conversionSession.world().findEntity(deliveryDroneId);
    activeDeliveryDrone->transform.position = {0.0F, 6.0F, 0.0F};
    activeDeliveryDrone->gatherer.carriedResource = "scrap";
    activeDeliveryDrone->gatherer.carriedAmount = 10.0F;
    activeDeliveryDrone->unitControl.order = strategy::UnitOrderKind::returnResources;
    conversionSession.advanceTicks();
    const strategy::Entity* activeAlloyProcessor = conversionSession.world().findEntity(alloyProcessorId);
    valid = valid && activeAlloyProcessor && activeAlloyProcessor->processor &&
            std::abs(activeAlloyProcessor->processor.bufferedInputs.at("scrap") - 10.0F) < 0.001F &&
            !conversionSession.players().find(1)->resources.contains("scrap");
    conversionSession.advanceTicks();
    activeAlloyProcessor = conversionSession.world().findEntity(alloyProcessorId);
    valid = valid && activeAlloyProcessor->processor.bufferedInputs.empty() &&
            activeAlloyProcessor->processor.state == strategy::ProcessorOperationalState::processing &&
            std::abs(conversionSession.players().find(1)->resources.at("alloy") - 10.0F) < 0.001F;
    strategy::Entity* activeConversionHub = conversionSession.world().findEntity(conversionHubId);
    activeConversionHub->processor.bufferedInputs["oil"] = 10.0F;
    conversionSession.advanceTicks();
    valid = valid && activeConversionHub->processor.bufferedInputs.empty() &&
            std::abs(conversionSession.players().find(1)->resources.at("fuel") - 5.0F) < 0.001F;

    strategy::GameSession deliverySelectionSession{gameplay, 325U};
    deliverySelectionSession.replaceWorld({}, 325U);
    deliverySelectionSession.players().find(1)->resources["alloy"] = 0.0F;
    strategy::Entity& nearbyHub = deliverySelectionSession.world().createEntity(
        "Nearby Hub", "command_hub", 1);
    gameplay.initializeEntity(nearbyHub);
    const strategy::EntityId nearbyHubId = nearbyHub.id;
    strategy::Entity& distantProcessor = deliverySelectionSession.world().createEntity(
        "Distant Processor", "alloy_processor", 1);
    gameplay.initializeEntity(distantProcessor);
    const strategy::EntityId distantProcessorId = distantProcessor.id;
    strategy::Entity& selectionDrone = deliverySelectionSession.world().createEntity(
        "Selection Drone", "construction_drone", 1);
    gameplay.initializeEntity(selectionDrone);
    const strategy::EntityId selectionDroneId = selectionDrone.id;
    deliverySelectionSession.world().findEntity(nearbyHubId)->transform.position = {0, 0, 0};
    deliverySelectionSession.world().findEntity(distantProcessorId)->transform.position = {30, 0, 0};
    strategy::Entity* selectedDeliveryDrone = deliverySelectionSession.world().findEntity(selectionDroneId);
    selectedDeliveryDrone->transform.position = {0, 6, 0};
    selectedDeliveryDrone->gatherer.carriedResource = "scrap";
    selectedDeliveryDrone->gatherer.carriedAmount = 10.0F;
    selectedDeliveryDrone->unitControl.order = strategy::UnitOrderKind::returnResources;
    deliverySelectionSession.advanceTicks();
    selectedDeliveryDrone = deliverySelectionSession.world().findEntity(selectionDroneId);
    valid = valid && selectedDeliveryDrone->gatherer.deliveryTarget == distantProcessorId;
    deliverySelectionSession.world().destroyEntity(distantProcessorId);
    deliverySelectionSession.advanceTicks(2);
    selectedDeliveryDrone = deliverySelectionSession.world().findEntity(selectionDroneId);
    valid = valid && selectedDeliveryDrone->gatherer.carriedAmount == 0.0F &&
            std::abs(deliverySelectionSession.players().find(1)->resources.at("alloy") - 5.0F) < 0.001F;

    strategy::Entity& syntheticDeliveryDrone = deliverySelectionSession.world().createEntity(
        "Synthetic Delivery", "construction_drone", 1);
    gameplay.initializeEntity(syntheticDeliveryDrone);
    const strategy::EntityId syntheticDeliveryDroneId = syntheticDeliveryDrone.id;
    strategy::Entity* activeSyntheticDelivery =
        deliverySelectionSession.world().findEntity(syntheticDeliveryDroneId);
    activeSyntheticDelivery->gatherer.carriedResource = "synthetic";
    activeSyntheticDelivery->gatherer.carriedAmount = 4.0F;
    activeSyntheticDelivery->unitControl.order = strategy::UnitOrderKind::returnResources;
    deliverySelectionSession.advanceTicks();
    activeSyntheticDelivery = deliverySelectionSession.world().findEntity(syntheticDeliveryDroneId);
    valid = valid && activeSyntheticDelivery->unitControl.order == strategy::UnitOrderKind::idle &&
            activeSyntheticDelivery->gatherer.waitingForProcessor &&
            activeSyntheticDelivery->gatherer.carriedAmount == 4.0F;

    strategy::Entity& routeAlloy = deliverySelectionSession.world().createEntity(
        "Route Alloy", "alloy_processor", 1);
    gameplay.initializeEntity(routeAlloy);
    const strategy::EntityId routeAlloyId = routeAlloy.id;
    strategy::Entity& routeFuel = deliverySelectionSession.world().createEntity(
        "Route Fuel", "fuel_processor", 1);
    gameplay.initializeEntity(routeFuel);
    const strategy::EntityId routeFuelId = routeFuel.id;
    deliverySelectionSession.world().findEntity(routeAlloyId)->transform.position = {2, 0, 0};
    deliverySelectionSession.world().findEntity(routeFuelId)->transform.position = {20, 0, 0};
    activeSyntheticDelivery = deliverySelectionSession.world().findEntity(syntheticDeliveryDroneId);
    activeSyntheticDelivery->unitControl.order = strategy::UnitOrderKind::returnResources;
    valid = deliverySelectionSession.submit(
        {1, 2, strategy::SetDeliveryOutputCommand{syntheticDeliveryDroneId, "fuel"}}) && valid;
    deliverySelectionSession.advanceTicks();
    activeSyntheticDelivery = deliverySelectionSession.world().findEntity(syntheticDeliveryDroneId);
    valid = valid && activeSyntheticDelivery->gatherer.preferredOutput == "fuel" &&
            activeSyntheticDelivery->gatherer.deliveryTarget == routeFuelId;

    strategy::GameSession repeatedGatherSession{gameplay, 326U};
    repeatedGatherSession.replaceWorld({}, 326U);
    repeatedGatherSession.players().find(1)->resources["alloy"] = 0.0F;
    strategy::Entity& loopHub = repeatedGatherSession.world().createEntity("Loop Hub", "command_hub", 1);
    gameplay.initializeEntity(loopHub);
    const strategy::EntityId loopHubId = loopHub.id;
    strategy::Entity& loopDrone = repeatedGatherSession.world().createEntity(
        "Loop Drone", "construction_drone", 1);
    gameplay.initializeEntity(loopDrone);
    const strategy::EntityId loopDroneId = loopDrone.id;
    strategy::Entity& loopSource = repeatedGatherSession.world().createEntity(
        "Loop Scrap", "scrap_field", 0);
    gameplay.initializeEntity(loopSource);
    const strategy::EntityId loopSourceId = loopSource.id;
    repeatedGatherSession.world().findEntity(loopHubId)->transform.position = {0, 0, 0};
    repeatedGatherSession.world().findEntity(loopDroneId)->transform.position = {0, 6, 0};
    repeatedGatherSession.world().findEntity(loopSourceId)->transform.position = {0, 0, 0};
    valid = repeatedGatherSession.submit(
                {1, 1, strategy::GatherResourceCommand{loopDroneId, loopSourceId, 0}}) && valid;
    repeatedGatherSession.advanceTicks(500);
    const auto resourceEvents = repeatedGatherSession.consumeResourceEvents();
    const auto hasResourceEvent = [&](strategy::ResourceEventKind kind) {
        return std::any_of(resourceEvents.begin(), resourceEvents.end(),
                           [kind](const strategy::ResourceEvent& event) {
                               return event.kind == kind;
                           });
    };
    valid = valid && repeatedGatherSession.players().find(1)->resources.at("alloy") > 0.0F &&
            repeatedGatherSession.world().findEntity(loopSourceId)->resource.remaining < 160.0F &&
            repeatedGatherSession.world().findEntity(loopDroneId)->gatherer.sourceTarget == loopSourceId &&
            hasResourceEvent(strategy::ResourceEventKind::gatheringStarted) &&
            hasResourceEvent(strategy::ResourceEventKind::deliveryCompleted) &&
            hasResourceEvent(strategy::ResourceEventKind::conversionCompleted);

    strategy::GameSession explicitDeliverySession{gameplay, 327U};
    explicitDeliverySession.replaceWorld({}, 327U);
    explicitDeliverySession.players().find(1)->resources["alloy"] = 0.0F;
    strategy::Entity& explicitHub = explicitDeliverySession.world().createEntity(
        "Explicit Hub", "command_hub", 1);
    gameplay.initializeEntity(explicitHub);
    const strategy::EntityId explicitHubId = explicitHub.id;
    strategy::Entity& availableDedicated = explicitDeliverySession.world().createEntity(
        "Available Dedicated", "alloy_processor", 1);
    gameplay.initializeEntity(availableDedicated);
    strategy::Entity& explicitDrone = explicitDeliverySession.world().createEntity(
        "Explicit Drone", "construction_drone", 1);
    gameplay.initializeEntity(explicitDrone);
    const strategy::EntityId explicitDroneId = explicitDrone.id;
    explicitDeliverySession.world().findEntity(explicitHubId)->transform.position = {0, 0, 0};
    explicitDeliverySession.world().findEntity(explicitDroneId)->transform.position = {0, 6, 0};
    strategy::Entity* commandedDrone = explicitDeliverySession.world().findEntity(explicitDroneId);
    commandedDrone->gatherer.carriedResource = "scrap";
    commandedDrone->gatherer.carriedAmount = 10.0F;
    valid = explicitDeliverySession.submit(
                {1, 1, strategy::DeliverResourceCommand{explicitDroneId, explicitHubId}}) && valid;
    explicitDeliverySession.advanceTicks(2);
    valid = valid &&
            std::abs(explicitDeliverySession.players().find(1)->resources.at("alloy") - 5.0F) < 0.001F;

    strategy::GameSession powerAllocationSession{gameplay, 323U};
    powerAllocationSession.replaceWorld({}, 323U);
    strategy::Entity& allocationHub = powerAllocationSession.world().createEntity(
        "Grid source", "command_hub", 1);
    gameplay.initializeEntity(allocationHub);
    strategy::Entity& firstProcessor = powerAllocationSession.world().createEntity(
        "First processor", "alloy_processor", 1);
    gameplay.initializeEntity(firstProcessor);
    const strategy::EntityId firstProcessorId = firstProcessor.id;
    strategy::Entity& secondProcessor = powerAllocationSession.world().createEntity(
        "Second processor", "fuel_processor", 1);
    gameplay.initializeEntity(secondProcessor);
    const strategy::EntityId secondProcessorId = secondProcessor.id;
    powerAllocationSession.advanceTicks();
    const strategy::Entity* allocatedFirst = powerAllocationSession.world().findEntity(firstProcessorId);
    const strategy::Entity* allocatedSecond = powerAllocationSession.world().findEntity(secondProcessorId);
    valid = valid && allocatedFirst->power.state == strategy::PowerOperationalState::powered &&
            allocatedFirst->power.supplied == 8.0F &&
            allocatedSecond->power.state == strategy::PowerOperationalState::underpowered &&
            allocatedSecond->power.supplied == 2.0F;
    powerAllocationSession.world().findEntity(secondProcessorId)->processor.bufferedInputs["oil"] = 4.0F;
    powerAllocationSession.advanceTicks();
    allocatedSecond = powerAllocationSession.world().findEntity(secondProcessorId);
    valid = valid && allocatedSecond->processor.bufferedInputs.at("oil") == 4.0F &&
            allocatedSecond->processor.state == strategy::ProcessorOperationalState::blocked &&
            powerAllocationSession.players().find(1)->resources.at("fuel") == 0.0F;
    powerAllocationSession.advanceTicks(10);
    const auto blockedEvents = powerAllocationSession.consumeResourceEvents();
    valid = valid && std::count_if(blockedEvents.begin(), blockedEvents.end(),
        [](const strategy::ResourceEvent& event) {
            return event.kind == strategy::ResourceEventKind::waitingForPower;
        }) == 1;

    strategy::GameSession capacitySession{gameplay, 328U};
    capacitySession.replaceWorld({}, 328U);
    strategy::Entity& cappedProcessor = capacitySession.world().createEntity(
        "Capped", "alloy_processor", 1);
    gameplay.initializeEntity(cappedProcessor);
    const strategy::EntityId cappedProcessorId = cappedProcessor.id;
    cappedProcessor.processor.bufferedInputs["scrap"] = 399.0F;
    strategy::Entity& capacityDrone = capacitySession.world().createEntity(
        "Capacity Drone", "construction_drone", 1);
    gameplay.initializeEntity(capacityDrone);
    const strategy::EntityId capacityDroneId = capacityDrone.id;
    capacityDrone.transform.position = {0, 6, 0};
    cappedProcessor.transform.position = {0, 0, 0};
    capacityDrone.gatherer.carriedResource = "scrap";
    capacityDrone.gatherer.carriedAmount = 10.0F;
    capacityDrone.unitControl.order = strategy::UnitOrderKind::returnResources;
    capacitySession.advanceTicks();
    valid = valid && std::abs(capacitySession.world().findEntity(cappedProcessorId)
                                  ->processor.bufferedInputs.at("scrap") - 400.0F) < 0.001F &&
            std::abs(capacitySession.world().findEntity(capacityDroneId)
                                  ->gatherer.carriedAmount - 9.0F) < 0.001F;

    strategy::GameSession syntheticSession{gameplay, 324U};
    syntheticSession.replaceWorld({}, 324U);
    strategy::Entity& syntheticHub = syntheticSession.world().createEntity(
        "Grid source", "command_hub", 1);
    gameplay.initializeEntity(syntheticHub);
    strategy::Entity& syntheticMine = syntheticSession.world().createEntity(
        "Synthetic Mine", "synthetic_mine", 1);
    gameplay.initializeEntity(syntheticMine);
    const strategy::EntityId syntheticMineId = syntheticMine.id;
    syntheticSession.advanceTicks(5);
    const strategy::Entity* activeSyntheticMine = syntheticSession.world().findEntity(syntheticMineId);
    valid = valid && activeSyntheticMine->resource.type == "synthetic" &&
            std::abs(activeSyntheticMine->resource.remaining - 0.5F) < 0.001F &&
            activeSyntheticMine->power.state == strategy::PowerOperationalState::powered;

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
