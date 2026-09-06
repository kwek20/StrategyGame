#pragma once

#include "world/Entity.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

namespace strategy {

class World;
class PlayerRegistry;

struct SaveData {
    std::uint32_t terrainSeed{0};
    std::vector<Entity> entities;
    std::string playerOneCountry{"spain"};
    std::string playerTwoCountry{"japan"};
    std::string playerOneSpecialization{"unassigned"};
    std::string playerTwoSpecialization{"unassigned"};
    std::array<float, 2> wood{0, 0}, stone{0, 0}, gold{0, 0};
    std::array<std::map<std::string, float>, 2> resources;
    std::array<std::vector<std::uint8_t>, 2> discovered;
    std::array<std::vector<LastKnownEntity>, 2> intelligence;
};

class SaveGame final {
  public:
    static void write(const std::filesystem::path& path,
                      std::uint32_t terrainSeed,
                      const World& world,
                      const PlayerRegistry* players = nullptr);
    [[nodiscard]] static SaveData read(const std::filesystem::path& path);
};

} // namespace strategy
