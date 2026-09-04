#pragma once

#include "app/GameState.hpp"
#include "players/CountryCatalogue.hpp"

#include <string>

namespace strategy {

class StartMenuState final : public GameState {
public:
    StartMenuState();
    void handleEvent(const SDL_Event& event) override;
    void update(float deltaSeconds) override;
    void render(Renderer& renderer) const override;
    [[nodiscard]] StateRequest request() const override { return request_; }
    [[nodiscard]] std::uint32_t terrainSeed() const override;
    [[nodiscard]] std::string playerOneCountry() const override;
    [[nodiscard]] std::string playerTwoCountry() const override;

private:
    static constexpr float buttonLeft = 60.0F;
    static constexpr float buttonRight = 300.0F;
    static constexpr float startTop = 250.0F;
    static constexpr float startBottom = 310.0F;
    static constexpr float buildTop = 320.0F;
    static constexpr float buildBottom = 380.0F;
    static constexpr float loadTop = 390.0F;
    static constexpr float loadBottom = 450.0F;
    static constexpr float settingsTop = 460.0F,settingsBottom=520.0F;
    static constexpr float exitTop = 530.0F,exitBottom=590.0F;

    bool startHovered_{false};
    bool buildHovered_{false};
    bool loadHovered_{false};
    bool settingsHovered_{false};
    bool exitHovered_{false};
    bool seedFocused_{false};
    std::string seedText_{"1592594996"};
    StateRequest request_{StateRequest::none};
    CountryCatalogue countryCatalogue_;
    std::size_t playerOneCountryIndex_{0};
    std::size_t playerTwoCountryIndex_{0};

    void updateHover(float mouseX, float mouseY);
    void cycleCountry(std::size_t& index,int direction);
};

} // namespace strategy
