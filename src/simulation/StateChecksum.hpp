#pragma once

#include <cstdint>

namespace strategy {
class PlayerRegistry;
class World;
[[nodiscard]] std::uint64_t authoritativeStateChecksum(const World& world,
                                                       const PlayerRegistry& players,
                                                       std::uint32_t terrainSeed,
                                                       std::uint64_t tick,
                                                       std::uint32_t mapChunksPerSide = 20);
}
