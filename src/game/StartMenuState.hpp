#pragma once

#include "app/GameState.hpp"
#include "persistence/GameConfig.hpp"
#include "render/RmlUiManager.hpp"

#include <string>
#include <string_view>

namespace strategy {

class StartMenuState final : public GameState {
  public:
    explicit StartMenuState(StateContext& context);
    ~StartMenuState() override;
    void handleEvent(const SDL_Event& event) override;
    void update(float deltaSeconds) override;
    void render(Renderer& renderer) const override;
    StateRequest takeRequest() override;

  private:
    StateRequest request_{StateRequest::none};
    GameConfig config_;
    float backgroundTime_{0.0F};
    float transitionOut_{-1.0F};
    RmlUiScreenHandle screen_{};

    void activateControl(std::string_view id);
};

} // namespace strategy
