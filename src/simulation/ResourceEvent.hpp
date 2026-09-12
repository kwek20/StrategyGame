#pragma once

#include "world/Entity.hpp"

#include <cstdint>
#include <string>

namespace strategy {

enum class ResourceEventKind : std::uint8_t {
    gatheringStarted,
    cargoFull,
    deliveryCompleted,
    conversionCompleted,
    waitingForPower,
    destinationLost,
    sourceDepleted,
    sourceInaccessible,
    destinationInaccessible
};

struct ResourceEvent {
    ResourceEventKind kind{ResourceEventKind::gatheringStarted};
    std::uint64_t tick{0};
    PlayerId player{0};
    EntityId actor{0};
    EntityId target{0};
    std::string resource;
    float amount{0.0F};
};

} // namespace strategy
