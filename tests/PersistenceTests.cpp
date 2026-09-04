#include "persistence/GameConfig.hpp"
#include "persistence/SaveGame.hpp"
#include "world/World.hpp"
#include "players/PlayerRegistry.hpp"

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
    const strategy::GameConfig config = strategy::GameConfig::load(configPath);
    valid = valid && config.savePath() == std::filesystem::path{"gamedata/saves/test.json"};

    strategy::World world;
    strategy::Entity& entity = world.createEntity("House", "house");
    entity.kind=strategy::EntityKind::building;
    entity.health.emplace();entity.production.emplace();entity.buildingUpgrades.emplace();entity.upgrades.emplace();
    entity.transform.position = {1.0F, 2.0F, 3.0F};
    entity.transform.rotationDegrees = {4.0F, 5.0F, 6.0F};
    entity.transform.scale = {2.0F, 2.0F, 2.0F};
    entity.health.current=72.0F;entity.health.maximum=125.0F;
    entity.production.level=2;
    entity.buildingUpgrades.level=2;
    entity.production.characterBuildSeconds=7.0F;
    entity.production.productionSpeedMultiplier=1.4F;
    entity.production.productionSpeedUpgrades=4;
    entity.production.queue.push_back({strategy::ProductionKind::trainCharacter,7.0F,4.5F});
    const strategy::EntityId originalId = entity.id;
    strategy::PlayerRegistry players{"france","brazil"};
    players.find(1)->intelligence.push_back({42,"town_center",{8,0,9},{0,45,0},{1,1,1},true});
    strategy::SaveGame::write(savePath, 424242U, world,&players);
    const strategy::SaveData loaded = strategy::SaveGame::read(savePath);
    valid = valid && loaded.terrainSeed == 424242U && loaded.entities.size() == 1;
    valid = valid && loaded.entities[0].id == originalId;
    valid = valid && loaded.entities[0].modelKey == "house";
    valid = valid && loaded.entities[0].transform.position == glm::vec3{1.0F, 2.0F, 3.0F};
    valid = valid && loaded.entities[0].transform.scale == glm::vec3{2.0F};
    valid = valid && !loaded.entities[0].unitControl && loaded.entities[0].production;
    valid = valid && loaded.entities[0].health.current == 72.0F;
    valid = valid && loaded.entities[0].health.maximum == 125.0F;
    valid = valid && loaded.playerOneCountry == "france";
    valid = valid && loaded.playerTwoCountry == "brazil";
    valid = valid && loaded.playerOneSpecialization == "unassigned";
    valid = valid && loaded.playerTwoSpecialization == "unassigned";
    valid = valid && loaded.intelligence[0].size()==1&&loaded.intelligence[0][0].position==glm::vec3{8,0,9};
    valid = valid && loaded.entities[0].production.level == 2;
    valid = valid && loaded.entities[0].buildingUpgrades.level == 2;
    valid = valid && loaded.entities[0].production.productionSpeedMultiplier == 1.4F;
    valid = valid && loaded.entities[0].production.productionSpeedUpgrades == 4;
    valid = valid && loaded.entities[0].production.queue.size() == 1;
    valid = valid && loaded.entities[0].production.queue.front().remainingSeconds == 4.5F;


    std::filesystem::remove_all(directory);
    if (!valid) {
        std::cerr << "Persistence validation failed\n";
        return 1;
    }
    std::cout << "Persistence validation passed\n";
    return 0;
}
