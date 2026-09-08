#include "persistence/GameConfig.hpp"
#include "persistence/SaveGame.hpp"
#include "players/PlayerRegistry.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "strategy_game_persistence_test";
    std::filesystem::create_directories(directory);
    const std::filesystem::path configPath = directory / "config.json";
    const std::filesystem::path savePath = directory / "roundtrip.json";
    {
        std::ofstream config{configPath};
        config << R"({"configuration":{"saveDirectory":"gamedata/saves/","saveFile":"test.json"}})";
    }

    bool valid = true;
    strategy::GameConfig config = strategy::GameConfig::load(configPath);
    valid = valid && config.savePath() == std::filesystem::path{"gamedata/saves/test.json"};
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

    strategy::World world;
    strategy::Entity& entity = world.createEntity("Town Center", "town_center");
    entity.kind = strategy::EntityKind::building;
    entity.health.emplace();
    entity.production.emplace();
    entity.buildingUpgrades.emplace();
    entity.upgrades.emplace();
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
    drone.unitControl.order = strategy::UnitOrderKind::returningToCharge;
    const strategy::EntityId droneId = drone.id;
    world.foundations().push_back({{1.0F, 6.0F, 3.0F}, 5.0F, 7.0F});
    strategy::PlayerRegistry players{"france", "brazil"};
    players.find(1)->resources["materials"] = 125.0F;
    players.find(1)->intelligence.push_back(
        {42, "town_center", {8, 0, 9}, {0, 45, 0}, {1, 1, 1}, true});
    strategy::SaveGame::write(savePath, 424242U, world, &players, 10);
    const strategy::SaveData loaded = strategy::SaveGame::read(savePath);
    valid = valid && loaded.terrainSeed == 424242U && loaded.mapChunksPerSide == 10 &&
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
            loaded.resources[0].at("materials") == 125.0F;
    const auto loadedDrone = std::find_if(loaded.entities.begin(), loaded.entities.end(),
        [droneId](const strategy::Entity& candidate) { return candidate.id == droneId; });
    valid = valid && loadedDrone != loaded.entities.end() && loadedDrone->battery &&
            loadedDrone->flight && loadedDrone->flight.altitude == 9.0F &&
            loadedDrone->battery.charge == 18.0F &&
            loadedDrone->battery.returningToCharge &&
            loadedDrone->battery.hasSuspendedOrder &&
            loadedDrone->battery.suspendedOrder == strategy::UnitOrderKind::construct &&
            loadedDrone->battery.suspendedTarget == originalId &&
            loadedDrone->battery.chargerTarget == 77;

    std::filesystem::remove_all(directory);
    if (!valid) {
        std::cerr << "Persistence validation failed\n";
        return 1;
    }
    std::cout << "Persistence validation passed\n";
    return 0;
}
