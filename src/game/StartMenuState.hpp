#pragma once

#include "app/GameState.hpp"
#include "ui/UiDocument.hpp"
#include "ui/UiController.hpp"
#include "persistence/GameConfig.hpp"

#include <string>
#include <string_view>

namespace strategy {

class StartMenuState final : public GameState {
  public:
    explicit StartMenuState(StateContext& context);
    void handleEvent(const SDL_Event& event) override;
    void update(float deltaSeconds) override;
    void render(Renderer& renderer) const override;
    StateRequest takeRequest() override {
        const StateRequest result = request_;
        request_ = StateRequest::none;
        return result;
    }
  private:
    StateRequest request_{StateRequest::none};
    mutable UiDocument ui_;
    mutable UiController uiController_;
    GameConfig config_;

    void activateControl(std::string_view id);
};

} // namespace strategy
