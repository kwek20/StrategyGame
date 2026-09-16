#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace strategy {
enum class MovementDomain : std::uint8_t { land = 0, water = 1, air = 2 };
using MovementDomainMask = std::uint8_t;
constexpr MovementDomainMask movementDomainBit(MovementDomain domain) {
    return static_cast<MovementDomainMask>(1U << static_cast<unsigned>(domain));
}
constexpr bool hasMovementDomain(MovementDomainMask mask, MovementDomain domain) {
    return (mask & movementDomainBit(domain)) != 0;
}
constexpr std::array<MovementDomain, 3> movementDomains{
    MovementDomain::land, MovementDomain::water, MovementDomain::air};
constexpr std::string_view movementDomainName(MovementDomain domain) {
    switch (domain) {
    case MovementDomain::land: return "land";
    case MovementDomain::water: return "water";
    case MovementDomain::air: return "air";
    }
    return "unknown";
}
struct NavigationProfile {
    MovementDomainMask domains{movementDomainBit(MovementDomain::land)};
    bool ignoresEntityObstacles{false};
};
} // namespace strategy
