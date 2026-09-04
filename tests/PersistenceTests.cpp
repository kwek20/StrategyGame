#include "persistence/GameConfig.hpp"
#include "persistence/SaveGame.hpp"
#include "world/World.hpp"

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
    entity.transform.position = {1.0F, 2.0F, 3.0F};
    entity.transform.rotationDegrees = {4.0F, 5.0F, 6.0F};
    entity.transform.scale = {2.0F, 2.0F, 2.0F};
    const strategy::EntityId originalId = entity.id;
    strategy::SaveGame::write(savePath, 424242U, world);
    const strategy::SaveData loaded = strategy::SaveGame::read(savePath);
    valid = valid && loaded.terrainSeed == 424242U && loaded.entities.size() == 1;
    valid = valid && loaded.entities[0].id == originalId;
    valid = valid && loaded.entities[0].modelKey == "house";
    valid = valid && loaded.entities[0].transform.position == glm::vec3{1.0F, 2.0F, 3.0F};
    valid = valid && loaded.entities[0].transform.scale == glm::vec3{2.0F};

    std::filesystem::remove_all(directory);
    if (!valid) {
        std::cerr << "Persistence validation failed\n";
        return 1;
    }
    std::cout << "Persistence validation passed\n";
    return 0;
}
