#pragma once

#include "players/Player.hpp"
#include "world/Entity.hpp"

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <variant>

namespace strategy {

struct PossessUnitCommand {
    EntityId entity{0};
};
struct ReleaseUnitCommand {
    EntityId entity{0};
};
struct DirectUnitInputCommand {
    EntityId entity{0};
    glm::vec2 movement{0.0F};
    bool running{false};
    float facingDegrees{0.0F};
};
struct MoveUnitCommand {
    EntityId entity{0};
    glm::vec3 destination{0.0F};
};
struct GatherResourceCommand {
    EntityId entity{0};
    EntityId resource{0};
};
struct AttackEntityCommand {
    EntityId entity{0};
    EntityId target{0};
};
struct UpgradeTownHallCommand {
    EntityId entity{0};
};
struct ImproveTrainingCommand {
    EntityId entity{0};
};
struct TrainCharacterCommand {
    EntityId entity{0};
};

using CommandPayload = std::variant<PossessUnitCommand,
                                    ReleaseUnitCommand,
                                    DirectUnitInputCommand,
                                    MoveUnitCommand,
                                    GatherResourceCommand,
                                    AttackEntityCommand,
                                    UpgradeTownHallCommand,
                                    ImproveTrainingCommand,
                                    TrainCharacterCommand>;

struct PlayerCommand {
    PlayerId player{0};
    std::uint64_t sequence{0};
    CommandPayload payload;
};

} // namespace strategy
