#pragma once

#include "players/Player.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "world/Entity.hpp"

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <variant>
#include <vector>

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
struct StartRecipeCommand {
    EntityId entity{0};
    RecipeId recipeId;
    StartRecipeCommand() = default;
    StartRecipeCommand(EntityId id, RecipeId recipe) : entity(id), recipeId(std::move(recipe)) {}
    StartRecipeCommand(EntityId id, std::string recipe) : entity(id), recipeId(std::move(recipe)) {}
};
struct StartUpgradeCommand {
    EntityId entity{0};
    std::string upgradeId;
};
struct PlaceBuildingCommand {
    EntityId entity{0};
    std::string buildingId;
    glm::vec3 position{0.0F};
    std::vector<EntityId> builders;
};
struct ConstructCommand { EntityId entity{0}; EntityId building{0}; };
struct StopConstructionCommand { EntityId entity{0}; };
struct CancelConstructionCommand { EntityId entity{0}; };
struct RepairCommand { EntityId entity{0}; EntityId target{0}; };
struct CancelProductionCommand { EntityId entity{0}; std::uint32_t queueIndex{0}; };

using CommandPayload = std::variant<PossessUnitCommand,
                                    ReleaseUnitCommand,
                                    DirectUnitInputCommand,
                                    MoveUnitCommand,
                                    GatherResourceCommand,
                                    AttackEntityCommand,
                                    StartRecipeCommand,
                                    StartUpgradeCommand,
                                    PlaceBuildingCommand,
                                    ConstructCommand,
                                    StopConstructionCommand,
                                    CancelConstructionCommand,
                                    RepairCommand,
                                    CancelProductionCommand>;

struct PlayerCommand {
    PlayerId player{0};
    std::uint64_t sequence{0};
    CommandPayload payload;
};

} // namespace strategy
