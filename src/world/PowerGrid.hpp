#pragma once

#include "world/Entity.hpp"

#include <cstdint>
#include <vector>

namespace strategy {

using PowerGridId = std::uint64_t;
using PowerConnectionId = std::uint64_t;

// Authoritative topology storage for the future connected-grid simulation. It is
// intentionally empty today: power is presented as one player-wide aggregate.
struct PowerConnection {
    PowerConnectionId id{0};
    EntityId source{0};
    EntityId destination{0};
    float transferLimit{0.0F};
    bool enabled{true};
};

struct PowerGridTopology {
    PowerConnectionId nextConnectionId{1};
    std::vector<PowerConnection> connections;
};

} // namespace strategy
