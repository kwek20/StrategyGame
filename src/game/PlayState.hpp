#pragma once

#include "app/GameState.hpp"
#include "game/RtsCamera.hpp"
#include "game/ThirdPersonCamera.hpp"
#include "persistence/GameConfig.hpp"
#include "persistence/SaveGame.hpp"
#include "simulation/GameSession.hpp"
#include "ui/EntityHudModel.hpp"
#include "ui/UiDocument.hpp"
#include "ui/UiController.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <vector>

namespace strategy {

class PlayState final : public GameState {
  public:
    PlayState(StateContext& context, MatchSetupOptions setup);
    PlayState(StateContext& context, SaveData data);
    void handleEvent(const SDL_Event& event) override;
    void update(float deltaSeconds) override;
    void render(Renderer& renderer) const override;
    StateRequest takeRequest() override {
        const StateRequest result = request_;
        request_ = StateRequest::none;
        return result;
    }

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
    bool powerOverlayVisible_{false};
    StateRequest request_{StateRequest::none};
    GameConfig config_;
    mutable std::optional<std::uint32_t> pendingTerrainSeed_;
    mutable std::optional<std::uint32_t> pendingTerrainChunksPerSide_;
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
    mutable UiController uiController_;
    bool constructionPlacementMode_{false};
    std::string constructionRecipeId_{"construct.command_hub"};
    mutable std::optional<glm::vec2> pendingConstructionScreen_;
    mutable std::optional<glm::vec3> pendingConstructionPosition_;
    mutable std::optional<glm::vec2> constructionCursorScreen_;
    mutable bool constructionPreviewValid_{false};

    void setMouseCaptured(bool captured);

    void handlePauseEvent(const SDL_Event& event);
    [[nodiscard]] UiDocument pauseUi(int width, int height) const;
    [[nodiscard]] EntityHudModel buildConstructionHudModel(const Entity& selected,
                                                           const Player* player) const;
    [[nodiscard]] EntityHudModel buildEntityActionHudModel(const Entity& selected,
                                                           const Player* player) const;
};

} // namespace strategy
