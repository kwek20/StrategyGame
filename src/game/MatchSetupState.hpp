#pragma once

#include "app/GameState.hpp"
#include "persistence/GameConfig.hpp"
#include "ui/UiController.hpp"
#include "ui/UiDocument.hpp"

#include <array>
#include <string>
#include <string_view>

namespace strategy {

class MatchSetupState final : public GameState {
  public:
    explicit MatchSetupState(StateContext& context);
    void handleEvent(const SDL_Event& event) override;
    void update(float deltaSeconds) override;
    void render(Renderer& renderer) const override;
    StateRequest takeRequest() override;
    [[nodiscard]] MatchSetupOptions matchSetup() const override;

  private:
    struct Preset {
        const char* nameKey;
        float value;
    };
    static constexpr std::array<Preset, 3> mapSizes{{
        {"match_setup.map_10", 10.0F}, {"match_setup.map_15", 15.0F},
        {"match_setup.map_20", 20.0F}}};
    static constexpr std::array<Preset, 3> resourceStarts{{
        {"match_setup.low", 0.5F}, {"match_setup.standard", 1.0F},
        {"match_setup.high", 2.0F}}};
    static constexpr std::array<Preset, 3> resourceAbundance{{
        {"match_setup.sparse", 0.65F}, {"match_setup.standard", 1.0F},
        {"match_setup.rich", 1.5F}}};

    StateRequest request_{StateRequest::none};
    std::string seedText_{"1592594996"};
    std::size_t playerOneCountry_{0}, playerTwoCountry_{0};
    std::size_t mapSize_{1}, startingResources_{1}, abundance_{1};
    GameConfig config_;
    mutable UiController controller_;

    [[nodiscard]] UiDocument document(int width, int height) const;
    void activate(std::string_view id, int direction = 1);
    void cycle(std::size_t& value, std::size_t count, int direction);
};

} // namespace strategy
