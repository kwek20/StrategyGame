#pragma once

#include "app/GameState.hpp"
#include "game/GameplayParticlePresenter.hpp"
#include "game/RtsCamera.hpp"
#include "game/ThirdPersonCamera.hpp"
#include "persistence/GameConfig.hpp"
#include "persistence/SaveGame.hpp"
#include "render/RmlUiManager.hpp"
#include "simulation/GameSession.hpp"
#include "ui/EntityHudModel.hpp"
#include "ui/ResourceFieldSelection.hpp"
#include "world/PowerPlacement.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <vector>

namespace strategy {

class PlayState final : public GameState {
  public:
    PlayState(StateContext& context, MatchSetupOptions setup);
    PlayState(StateContext& context, MatchSetupOptions setup, GameSession preparedSession);
    PlayState(StateContext& context, SaveData data, WorldGenerationProgress* progress = nullptr);
    ~PlayState() override;
    void handleEvent(const SDL_Event& event) override;
    void update(float deltaSeconds) override;
    void render(Renderer& renderer) const override;
    StateRequest takeRequest() override;
    // Called only while loading; performs presentation work without update() or gameplay input.
    void beginPresentationPreparation(Renderer& renderer);
    bool advancePresentationPreparation(Renderer& renderer);
    const GameSession& session() const {
        return session_;
    }
    float presentationPreparationProgress() const {
        return preparationProgress_;
    }

  private:
    [[nodiscard]] InputContext controlledInputContext() const;
    [[nodiscard]] std::string controlledAction(std::string_view suffix) const;
    void restoreSavedPlayerProgress(SaveData& data);
    [[nodiscard]] CameraView controlledCameraView(Renderer&,const Entity&) const;
    enum class Preparation { terrain, resources, vegetation, world, hud, frame, fence, gpu, ready };
    Preparation preparation_{Preparation::terrain};
    float preparationProgress_{};
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
    bool manualFirePressed_{false};
    InputState input_;
    std::uint64_t settingsRevision_{};
    std::array<std::vector<EntityId>, 10> controlGroups_;
    bool orbiting_{false};
    bool mousePanning_{false};
    float rightDragDistance_{0.0F};
    mutable std::optional<glm::vec2> pendingMoveScreen_;
    mutable std::optional<glm::vec3> pendingMoveDestination_;
    mutable EntityId pendingOrderTarget_{0};
    bool paused_{false};
    float entryFadeRemaining_{0.35F};
    float pauseTransition_{-1.0F};
    StateRequest pausePendingRequest_{StateRequest::none};
    RmlUiScreenHandle pauseScreen_{};
    bool detailedDebug_{false};
    bool terrainDebug_{false};
    int waterDebugMode_{0};
    bool powerDebug_{false};
    bool powerOverlayVisible_{false};
    enum class PowerLinkMode { none, toggle };
    PowerLinkMode powerLinkMode_{PowerLinkMode::none};
    EntityId powerLinkSource_{0};
    struct HudAlert {
        EntityId source{0};
        std::string text;
        float remaining{0.0F};
    };
    std::vector<HudAlert> hudAlerts_;
    mutable std::vector<std::string> hudShortcuts_;
    mutable EntityId hudSelectedEntity_{0};
    mutable std::vector<EntityId> hudSelectedUnits_;
    StateRequest request_{StateRequest::none};
    GameConfig config_;
    mutable std::optional<std::uint32_t> pendingTerrainSeed_;
    mutable std::optional<std::uint32_t> pendingTerrainChunksPerSide_;
    mutable std::optional<TerrainLayoutId> pendingTerrainLayout_;
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
    mutable glm::vec2 pointerScreen_{0.0F};
    mutable EntityId hoveredEntity_{0};
    EntityId selectedEntity_{0};
    std::vector<EntityId> selectedUnits_;
    ResourceFieldSelection selectedResourceField_;
    bool draggingSelection_{false};
    glm::vec2 selectionStart_{0.0F};
    glm::vec2 selectionEnd_{0.0F};
    mutable std::optional<glm::vec4> pendingSelectionRectangle_;
    mutable std::optional<std::vector<EntityId>> pickedEntities_;
    std::uint32_t inputWindowId_{0};
    mutable RmlUiScreenHandle hudScreen_{};
    bool constructionPlacementMode_{false};
    std::string constructionRecipeId_{"construct.command_hub"};
    mutable std::optional<glm::vec2> pendingConstructionScreen_;
    mutable std::optional<PlaceBuildingCommand> pendingConstructionIntent_;
    mutable bool pendingConstructionRangeLock_{};
    bool pendingRepeatPlacement_{};
    bool constructionBatchActive_{};
    std::map<std::string, float> constructionRotations_;
    mutable PowerPlacementAssist constructionPowerAssist_;
    float constructionRotationDegrees_{};
    mutable TerrainPlacementCache constructionTerrainCache_;
    mutable std::optional<Entity> constructionGhost_;
    mutable std::optional<glm::vec2> constructionCursorScreen_;
    mutable bool constructionPreviewValid_{false};
    mutable std::string constructionPreviewReason_;
    mutable std::string constructionPreviewSummary_;
    std::optional<std::uint64_t> pendingPlacementSequence_;
    mutable bool pendingUnexploredPlacement_{};
    std::string placementRejection_;
    float placementRejectionRemaining_{};
    mutable GameplayParticlePresenter particlePresenter_;

    void setMouseCaptured(bool captured);
    void handleSelectionShortcut(std::string_view action);
    void cancelConstructionPlacement();
    [[nodiscard]] glm::vec3 assistedConstructionPosition(glm::vec3 position, bool lockRange) const;
    void sanitizeEntityReferences();
    void initializeStartingView();
    [[nodiscard]] std::optional<glm::vec2> minimapWorldAt(float screenX, float screenY) const;
    void ensureGameplayHud(Renderer& renderer) const;
    void updateGameplayHud(Renderer& renderer, const Player* player) const;
    bool handleGameplayHudAction(const std::string& action);

    void handlePauseEvent(const SDL_Event& event);
    void setPaused(bool paused);
    [[nodiscard]] EntityHudModel buildConstructionHudModel(const Entity& selected,
                                                           const Player* player) const;
    [[nodiscard]] EntityHudModel buildEntityActionHudModel(const Entity& selected,
                                                           const Player* player) const;
};

} // namespace strategy
