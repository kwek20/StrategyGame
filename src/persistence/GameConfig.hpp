#pragma once

#include "input/KeyBindings.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace strategy {

struct GameConfig {
    std::filesystem::path saveDirectory{"gamedata/saves/"};
    std::string saveFile{"autosave.json"};
    std::filesystem::path matchSetupFile{"gamedata/match_setup.json"};
    int resolutionWidth{1280};
    int resolutionHeight{720};
    bool fullscreen{false};
    float uiScale{1.0F};
    float masterVolume{1.0F};
    float musicVolume{0.8F};
    float effectsVolume{1.0F};
    bool muted{false};
    KeyBindings keybinds;

    [[nodiscard]] static GameConfig load(const std::filesystem::path& path);
    void write(const std::filesystem::path& path) const;
    [[nodiscard]] std::filesystem::path savePath() const;
    [[nodiscard]] const std::filesystem::path& matchSetupPath() const { return matchSetupFile; }
};

} // namespace strategy
