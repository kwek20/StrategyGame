#pragma once

#include "app/GameState.hpp"
#include "game/RtsCamera.hpp"
#include "game/ThirdPersonCamera.hpp"
#include "simulation/GameSession.hpp"
#include "persistence/GameConfig.hpp"
#include "persistence/SaveGame.hpp"

#include <glm/vec3.hpp>
#include <cstdint>
#include <optional>
#include <array>
#include <vector>
#include <glm/vec4.hpp>

namespace strategy {

class PlayState final : public GameState {
public:
    explicit PlayState(std::uint32_t terrainSeed,std::string playerOneCountry="spain",
                       std::string playerTwoCountry="japan");
    explicit PlayState(SaveData data);
    void handleEvent(const SDL_Event& event) override;
    void update(float deltaSeconds) override;
    void render(Renderer& renderer) const override;
    [[nodiscard]] StateRequest request() const override { return request_; }

private:
    enum class ViewMode { strategy, unitControl };
    RtsCamera camera_;
    ThirdPersonCamera thirdPersonCamera_;
    ViewMode viewMode_{ViewMode::strategy};
    GameSession session_;
    bool forward_{false};
    bool backward_{false};
    bool left_{false};
    bool right_{false};
    bool running_{false};
    bool orbiting_{false};
    bool mousePanning_{false};
    float rightDragDistance_{0.0F};
    mutable std::optional<glm::vec2> pendingMoveScreen_;
    mutable std::optional<glm::vec3> pendingMoveDestination_;
    mutable EntityId pendingOrderTarget_{0};
    bool paused_{false};
    bool detailedDebug_{false};
    bool resumeHovered_{false};
    bool exitHovered_{false};
    StateRequest request_{StateRequest::none};
    GameConfig config_;
    mutable std::optional<std::uint32_t> pendingTerrainSeed_;
    PlayerId localPlayer_{1};
    EntityId possessedEntity_{0};
    std::uint64_t nextCommandSequence_{1};
    mutable std::optional<glm::vec2> pendingSelection_;
    mutable std::optional<EntityId> pickedEntity_;
    mutable bool pickedEntityAdditive_{false};
    mutable bool pendingSelectionAdditive_{false};
    mutable bool pickedEntityDoubleClick_{false};
    mutable bool pendingSelectionDoubleClick_{false};
    EntityId lastWorldClickEntity_{0};
    mutable std::optional<glm::vec2> hoverPosition_;
    mutable EntityId hoveredEntity_{0};
    EntityId selectedEntity_{0};
    std::vector<EntityId> selectedUnits_;
    bool draggingSelection_{false};
    glm::vec2 selectionStart_{0.0F};
    glm::vec2 selectionEnd_{0.0F};
    mutable std::optional<glm::vec4> pendingSelectionRectangle_;
    mutable std::optional<std::vector<EntityId>> pickedEntities_;
    std::uint32_t inputWindowId_{0};
    std::array<bool,3> townHallButtonHovered_{false,false,false};

    void setMouseCaptured(bool captured);

    void handlePauseEvent(const SDL_Event& event);
    void updatePauseHover(float mouseX, float mouseY);
};

} // namespace strategy
