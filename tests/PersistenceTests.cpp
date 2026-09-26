#include "persistence/GameConfig.hpp"
#include "persistence/MatchSetupProfile.hpp"
#include "persistence/SaveGame.hpp"
#include "players/PlayerRegistry.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

int strategyTestMain() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "strategy_game_persistence_test";
    std::filesystem::create_directories(directory);
    const std::filesystem::path configPath = directory / "config.json";
    const std::filesystem::path savePath = directory / "roundtrip.json";
    const std::filesystem::path matchSetupPath = directory / "match-setup.json";
    {
        std::ofstream config{configPath};
        config << R"({"configuration":{"saveDirectory":"gamedata/saves/","saveFile":"test.json","matchSetupFile":"gamedata/custom-match.json"}})";
    }

    bool valid = strategy::SaveGame::formatVersion == 1;
    strategy::GameConfig config = strategy::GameConfig::load(configPath);
    valid = valid && config.savePath() == std::filesystem::path{"gamedata/saves/test.json"};
    valid = valid && config.matchSetupPath() == std::filesystem::path{"gamedata/custom-match.json"};
    config.resolutionWidth = 1920;
    config.resolutionHeight = 1080;
    config.fullscreen = true;
    config.uiScale = 1.3F;
    config.masterVolume = 0.7F;
    config.musicVolume = 0.4F;
    config.effectsVolume = 0.9F;
    config.muted = true;
    config.keybinds["forward"] = 42;
    config.write(configPath);
    const strategy::GameConfig settingsRoundTrip = strategy::GameConfig::load(configPath);
    valid = valid && settingsRoundTrip.resolutionWidth == 1920 &&
            settingsRoundTrip.resolutionHeight == 1080 && settingsRoundTrip.fullscreen &&
            settingsRoundTrip.uiScale == 1.3F &&
            settingsRoundTrip.keybinds.at("forward") == 42 &&
            settingsRoundTrip.masterVolume == 0.7F && settingsRoundTrip.musicVolume == 0.4F &&
            settingsRoundTrip.effectsVolume == 0.9F && settingsRoundTrip.muted;

    {
        std::ofstream profile{matchSetupPath};
        profile << R"({"matchSetup":{"terrainSeed":1,"playerOneCountry":"france","playerTwoCountry":"brazil","mapChunksPerSide":20,"startingResourcesScale":2.0,"resourceAbundanceScale":1.5,"terrainLayout":"archipelago","biomes":{"wetland":{"enabled":false}}}})";
    }
    strategy::MatchSetupProfile profile = strategy::MatchSetupProfileStore::load(matchSetupPath);
    valid = valid && profile.terrainSeed == 1 && profile.mapChunksPerSide == 20 &&
            profile.playerOneCountry == "france" && profile.playerTwoCountry == "brazil" &&
            profile.startingResourcesScale == 2.0F &&
            profile.resourceAbundanceScale == 1.5F && profile.terrainLayout == "archipelago";
    profile.terrainSeed = 99;
    profile.mapChunksPerSide = 10;
    strategy::MatchSetupProfileStore::write(matchSetupPath, profile);
    const strategy::MatchSetupProfile profileRoundTrip =
        strategy::MatchSetupProfileStore::load(matchSetupPath);
    valid = valid && profileRoundTrip.terrainSeed == 99 &&
            profileRoundTrip.mapChunksPerSide == 10;
    {
        std::ifstream persisted{matchSetupPath};
        const std::string contents((std::istreambuf_iterator<char>(persisted)),
                                   std::istreambuf_iterator<char>());
        valid = valid && contents.find("\"biomes\"") != std::string::npos &&
                contents.find("\"wetland\"") != std::string::npos;
    }

    strategy::World world;
    strategy::Entity& entity = world.createEntity("Town Center", "town_center");
    entity.kind = strategy::EntityKind::building;
    entity.health.emplace();
    entity.production.emplace();
    entity.buildingUpgrades.emplace();
    entity.upgrades.emplace();
    entity.processor.emplace();
    entity.processor.bufferedInputs["scrap"] = 14.0F;
    entity.power.emplace();
    entity.power.generation = 0.0F;
    entity.power.demand = 8.0F;
    entity.power.supplied = 3.0F;
    entity.power.stored = 15.0F;
    entity.power.storageCapacity = 50.0F;
    entity.power.connectionRange = 24.0F;
    entity.power.transferLimit = 10.0F;
    entity.power.maximumConnections = 4;
    entity.power.gridId = entity.id;
    entity.power.enabled = false;
    entity.power.priority = 40;
    entity.power.state = strategy::PowerOperationalState::underpowered;
    entity.transform.position = {1.0F, 2.0F, 3.0F};
    entity.transform.rotationDegrees = {4.0F, 5.0F, 6.0F};
    entity.transform.scale = {2.0F, 2.0F, 2.0F};
    entity.health.current = 72.0F;
    entity.health.maximum = 125.0F;
    entity.buildingUpgrades.level = 2;
    entity.production.productionSpeedMultiplier = 1.4F;
    entity.production.productionSpeedUpgrades = 4;
    strategy::ProductionOrder production;
    production.kind = strategy::ProductionKind::trainCharacter;
    production.recipeId = "town_center.train_construction_drone";
    production.productId = "construction_drone";
    production.durationTicks = 210;
    production.remainingTicks = 135;
    entity.production.queue.push_back(production);
    entity.construction.emplace();
    entity.construction.recipeId = "construct.command_hub";
    entity.construction.powerRequired = 120.0F;
    entity.construction.powerProgress = 30.0F;
    entity.construction.state = strategy::BuildingLifecycleState::underConstruction;
    const strategy::EntityId originalId = entity.id;
    strategy::Entity& drone = world.createEntity("Drone", "construction_drone", 1);
    drone.kind = strategy::EntityKind::unit;
    drone.unitControl.emplace();
    drone.flight.emplace();
    drone.flight.altitude = 9.0F;
    drone.transform.position.y = 9.0F;
    drone.battery.emplace();
    drone.battery.capacity = 100.0F;
    drone.battery.charge = 18.0F;
    drone.battery.returningToCharge = true;
    drone.battery.hasSuspendedOrder = true;
    drone.battery.suspendedOrder = strategy::UnitOrderKind::construct;
    drone.battery.suspendedTarget = originalId;
    drone.battery.suspendedDestination = entity.transform.position;
    drone.battery.suspendedHasDestination = true;
    drone.battery.chargerTarget = 77;
    drone.battery.recoveryReason = strategy::BatteryRecoveryReason::chargersOccupied;
    drone.gatherer.emplace();
    drone.gatherer.carriedResource = "scrap";
    drone.gatherer.carriedAmount = 8.0F;
    drone.gatherer.sourceTarget = 91;
    drone.gatherer.deliveryTarget = originalId;
    drone.gatherer.preferredProcessor = originalId;
    drone.gatherer.preferredOutput = "alloy";
    drone.gatherer.repeatGathering = true;
    drone.gatherer.waitingForProcessor = false;
    drone.unitControl.order = strategy::UnitOrderKind::returningToCharge;
    const strategy::EntityId droneId = drone.id;
    world.foundations().push_back({{1.0F, 6.0F, 3.0F}, 5.0F, 7.0F});
    strategy::PlayerRegistry players{"france", "brazil"};
    players.find(1)->resources["alloy"] = 125.0F;
    players.find(1)->intelligence.push_back(
        {42, "town_center", {8, 0, 9}, {0, 45, 0}, {1, 1, 1}, true});
    strategy::SaveGame::write(savePath, 424242U, world, &players, 10, "central_hill");
    {
        std::ifstream saved{savePath};
        const std::string contents((std::istreambuf_iterator<char>(saved)),
                                   std::istreambuf_iterator<char>());
        valid = valid && contents.find("\"formatVersion\": 1") != std::string::npos;
    }
    const strategy::SaveData loaded = strategy::SaveGame::read(savePath);
    valid = valid && loaded.terrainSeed == 424242U && loaded.mapChunksPerSide == 10 &&
            loaded.terrainLayout == "central_hill" &&
            loaded.entities.size() == 2;
    valid = valid && loaded.foundations.size() == 1 &&
            loaded.foundations[0].center == glm::vec3{1.0F, 6.0F, 3.0F} &&
            loaded.foundations[0].innerRadius == 5.0F &&
            loaded.foundations[0].outerRadius == 7.0F;
    valid = valid && loaded.entities[0].id == originalId;
    valid = valid && loaded.entities[0].archetype.value == "town_center";
    valid = valid && loaded.entities[0].archetype.value == "town_center";
    valid = valid && loaded.entities[0].presentation.value == "town_center";
    valid = valid && loaded.entities[0].transform.position == glm::vec3{1.0F, 2.0F, 3.0F};
    valid = valid && loaded.entities[0].transform.scale == glm::vec3{2.0F};
    valid = valid && !loaded.entities[0].unitControl && loaded.entities[0].production;
    valid = valid && loaded.entities[0].health.current == 72.0F;
    valid = valid && loaded.entities[0].health.maximum == 125.0F;
    valid = valid && loaded.playerOneCountry == "france";
    valid = valid && loaded.playerTwoCountry == "brazil";
    valid = valid && loaded.playerOneSpecialization == "unassigned";
    valid = valid && loaded.playerTwoSpecialization == "unassigned";
    valid = valid && loaded.intelligence[0].size() == 1 &&
            loaded.intelligence[0][0].position == glm::vec3{8, 0, 9};
    valid = valid && loaded.entities[0].buildingUpgrades.level == 2;
    valid = valid && loaded.entities[0].production.productionSpeedMultiplier == 1.4F;
    valid = valid && loaded.entities[0].production.productionSpeedUpgrades == 4;
    valid = valid && loaded.entities[0].production.queue.size() == 1;
    valid = valid && loaded.entities[0].construction &&
            loaded.entities[0].construction.recipeId == "construct.command_hub" &&
            loaded.entities[0].construction.powerProgress == 30.0F &&
            loaded.entities[0].construction.state ==
                strategy::BuildingLifecycleState::underConstruction;
    valid = valid && loaded.entities[0].production.queue.front().remainingTicks == 135 &&
            loaded.entities[0].production.queue.front().recipeId ==
                "town_center.train_construction_drone" &&
            loaded.resources[0].at("alloy") == 125.0F && loaded.entities[0].processor &&
            loaded.entities[0].processor.bufferedInputs.at("scrap") == 14.0F &&
            loaded.entities[0].power && loaded.entities[0].power.supplied == 3.0F &&
            loaded.entities[0].power.stored == 15.0F &&
            loaded.entities[0].power.storageCapacity == 50.0F &&
            loaded.entities[0].power.connectionRange == 24.0F &&
            loaded.entities[0].power.transferLimit == 10.0F &&
            loaded.entities[0].power.maximumConnections == 4 &&
            loaded.entities[0].power.gridId == originalId &&
            !loaded.entities[0].power.enabled &&
            loaded.entities[0].power.priority == 40 &&
            loaded.entities[0].power.state == strategy::PowerOperationalState::underpowered;
    const auto loadedDrone = std::find_if(loaded.entities.begin(), loaded.entities.end(),
        [droneId](const strategy::Entity& candidate) { return candidate.id == droneId; });
    valid = valid && loadedDrone != loaded.entities.end() && loadedDrone->battery &&
            loadedDrone->flight && loadedDrone->flight.altitude == 9.0F &&
            loadedDrone->battery.charge == 18.0F &&
            loadedDrone->battery.returningToCharge &&
            loadedDrone->battery.hasSuspendedOrder &&
            loadedDrone->battery.suspendedOrder == strategy::UnitOrderKind::construct &&
            loadedDrone->battery.suspendedTarget == originalId &&
            loadedDrone->battery.chargerTarget == 77 &&
            loadedDrone->battery.recoveryReason ==
                strategy::BatteryRecoveryReason::chargersOccupied && loadedDrone->gatherer &&
            loadedDrone->gatherer.carriedResource == "scrap" &&
            loadedDrone->gatherer.carriedAmount == 8.0F &&
            loadedDrone->gatherer.sourceTarget == 91 &&
            loadedDrone->gatherer.deliveryTarget == originalId &&
            loadedDrone->gatherer.preferredProcessor == originalId &&
            loadedDrone->gatherer.preferredOutput == "alloy" &&
            loadedDrone->gatherer.repeatGathering &&
            !loadedDrone->gatherer.waitingForProcessor;

    std::filesystem::remove_all(directory);
    if (!valid) {
        std::cerr << "Persistence validation failed\n";
        return 1;
    }
    std::cout << "Persistence validation passed\n";
    return 0;
}
