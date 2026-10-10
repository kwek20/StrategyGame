#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
namespace strategy {
enum class TerrainMode { v1, v2 };
// Both generators remain independently selectable; layouts resolve compatibility separately.
constexpr bool terrainModeAvailable(TerrainMode mode) {
    return mode == TerrainMode::v1 || mode == TerrainMode::v2;
}
constexpr std::string_view terrainModeName(TerrainMode mode) {
    return mode == TerrainMode::v2 ? "v2" : "v1";
}
inline TerrainMode terrainModeFromName(std::string_view name) {
    if (name == "v1")
        return TerrainMode::v1;
    if (name == "v2")
        return TerrainMode::v2;
    throw std::runtime_error("Unknown terrain generation mode: " + std::string(name));
}
} // namespace strategy
