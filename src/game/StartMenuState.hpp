#pragma once

#include "app/GameState.hpp"
#include "ui/UiDocument.hpp"

#include <string>

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
    [[nodiscard]] std::uint32_t terrainSeed() const override;
    [[nodiscard]] std::string playerOneCountry() const override;
    [[nodiscard]] std::string playerTwoCountry() const override;

  private:
    bool seedFocused_{false};
    std::string seedText_{"1592594996"};
    StateRequest request_{StateRequest::none};
    std::size_t playerOneCountryIndex_{0};
    std::size_t playerTwoCountryIndex_{0};
    UiDocument ui_;

    void cycleCountry(std::size_t& index, int direction);
    void refreshUiText();
};

} // namespace strategy
