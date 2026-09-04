#pragma once

#include <filesystem>
#include <string>
#include <cstdint>
#include <unordered_map>

namespace strategy {

struct GameConfig {
    std::filesystem::path saveDirectory{"gamedata/saves/"};
    std::string saveFile{"autosave.json"};
    int resolutionWidth{1280};
    int resolutionHeight{720};
    bool fullscreen{false};
    float masterVolume{1.0F};
    std::unordered_map<std::string,std::int32_t> keybinds{{"forward",119},{"backward",115},{"left",97},{"right",100},{"debug",1073741884},{"pause",27}};

    [[nodiscard]] static GameConfig load(const std::filesystem::path& path);
    void write(const std::filesystem::path& path) const;
    [[nodiscard]] std::filesystem::path savePath() const;
};

} // namespace strategy
