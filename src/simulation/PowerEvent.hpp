#pragma once

#include "world/Entity.hpp"

#include <cstdint>

namespace strategy {

enum class PowerEventKind {
    connectionCreated,
    connectionRemoved,
    shortage,
    recovered,
    shutdown,
    commandRejected
};
enum class PowerFailureReason : std::uint8_t {
    none, invalidTarget, enemyTarget, notOperational, connectionLimit, outOfRange, notConnected
};

struct PowerEvent {
    PowerEventKind kind{PowerEventKind::shortage};
    std::uint64_t tick{0};
    PlayerId player{0};
    EntityId source{0};
    EntityId target{0};
    std::uint64_t gridId{0};
    PowerFailureReason reason{PowerFailureReason::none};
};

} // namespace strategy
