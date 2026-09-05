#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
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
    static constexpr int explorationCells = 128;
    PlayerId id{0};
    TeamId team{0};
    std::string name;
    std::string countryId{"unassigned"};
    std::string specializationId{"unassigned"};
    float wood{0.0F}, stone{0.0F}, gold{0.0F};
    std::vector<std::uint8_t> discovered =
        std::vector<std::uint8_t>(explorationCells * explorationCells, 0);
    std::vector<std::uint8_t> visible =
        std::vector<std::uint8_t>(explorationCells * explorationCells, 0);
    std::vector<LastKnownEntity> intelligence;
};

} // namespace strategy
