#pragma once

#include "terrain/TerrainMode.hpp"

#include <cstdint>
#include <string_view>

namespace strategy {
class PlayerRegistry;
class World;
[[nodiscard]] std::uint64_t
authoritativeStateChecksum(const World& world,
                           const PlayerRegistry& players,
                           std::uint32_t terrainSeed,
                           std::uint64_t tick,
                           std::uint32_t mapChunksPerSide = 20,
                           std::string_view terrainLayout = "continental",
                           TerrainMode terrainMode = TerrainMode::v1,
                           std::uint32_t terrainRepairLevel = 0,
                           std::uint64_t terrainFingerprint = 0);
}
