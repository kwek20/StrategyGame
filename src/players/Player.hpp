#pragma once
#include "players/VisibilityGrid.hpp"

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <map>
#include <string>
#include <vector>

namespace strategy {

using PlayerId = std::uint64_t;
using TeamId = std::uint8_t;

struct LastKnownEntity {
    std::uint64_t id{0};
    std::string modelKey;
    glm::vec3 position{0.0F};
    glm::vec3 rotationDegrees{0.0F};
    glm::vec3 scale{1.0F};
    bool building{false};
};

struct Player {
    PlayerId id{0};
    TeamId team{0};
    std::string name;
    std::string countryId{"unassigned"};
    std::string specializationId{"unassigned"};
    std::map<std::string, float> resources;
    // Player research survives the loss of its original researcher.
    std::map<std::string, std::uint32_t> researchedUpgrades;
    VisibilityGrid visibility;
    std::vector<LastKnownEntity> intelligence;
};

} // namespace strategy
