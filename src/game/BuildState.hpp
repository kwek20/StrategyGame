#pragma once
#include "app/GameState.hpp"
#include "game/RtsCamera.hpp"
#include "gameplay/GameplayCatalogue.hpp"
#include "persistence/GameConfig.hpp"
#include "world/World.hpp"
#include "ui/EntityHudModel.hpp"
#include "ui/UiController.hpp"

#include <optional>
#include <string>
namespace strategy {
class BuildState final : public GameState {
  public:
    BuildState(StateContext& context, std::uint32_t terrainSeed);
    void handleEvent(const SDL_Event& event) override;
    void update(float deltaSeconds) override;
    void render(Renderer& renderer) const override;
    StateRequest takeRequest() override {
        const StateRequest result = request_;
        request_ = StateRequest::none;
        return result;
    }

  private:
    std::uint32_t terrainSeed_;
    mutable World world_;
    RtsCamera camera_;
    GameConfig config_;
    const DefinitionRegistry& gameplay_;
    PlayerId team_{1};
    std::size_t paletteIndex_{0};
    mutable std::string status_;
    mutable std::optional<glm::vec2> pendingPlacement_;
    mutable std::optional<glm::vec2> hoverPosition_;
    mutable EntityId hoveredEntity_{0};
    bool forward_{false}, backward_{false}, left_{false}, right_{false}, orbiting_{false},
        mousePanning_{false};
    StateRequest request_{StateRequest::none};
    mutable UiController uiController_;
    [[nodiscard]] EntityHudModel paletteHud() const;
};
} // namespace strategy
