#pragma once

#include "simulation/Command.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace strategy {

class CommandCodec final {
  public:
    static std::vector<std::byte> encode(const PlayerCommand& command);
    static std::optional<PlayerCommand> decode(std::span<const std::byte> bytes);
};

} // namespace strategy
