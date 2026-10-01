#pragma once
#include "app/GameState.hpp"
#include "persistence/GameConfig.hpp"
#include "render/RmlUiManager.hpp"
#include "terrain/TerrainGeneration.hpp"

#include <array>
#include <string>
#include <string_view>
namespace strategy {
class MatchSetupState final : public GameState {
  public:
    explicit MatchSetupState(StateContext& context);
    ~MatchSetupState() override;
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
    static constexpr std::array<Preset, 3> mapSizes{
        {{"match_setup.map_10", 10.F}, {"match_setup.map_15", 15.F}, {"match_setup.map_20", 20.F}}};
    static constexpr std::array<Preset, 3> resourceStarts{
        {{"match_setup.low", .5F}, {"match_setup.standard", 1.F}, {"match_setup.high", 2.F}}};
    static constexpr std::array<Preset, 3> resourceAbundance{
        {{"match_setup.sparse", .65F}, {"match_setup.standard", 1.F}, {"match_setup.rich", 1.5F}}};
    StateRequest request_{StateRequest::none};
    std::string seedText_{"1592594996"};
    std::size_t playerOneCountry_{0}, playerTwoCountry_{0}, mapSize_{1}, startingResources_{1},
        abundance_{1}, terrainLayout_{0};
    GameConfig config_;
    TerrainGenerationDefinitions terrainDefinitions_;
    float menuTime_{0.F}, transitionOut_{-1.F};
    RmlUiScreenHandle screen_{};
    [[nodiscard]] MatchSetupOptions currentSetup() const;
    void refreshUi();
    void persist();
    void activate(std::string_view id, int direction = 1);
    void cycle(std::size_t& value, std::size_t count, int direction);
    void beginTransition(StateRequest request);
};
} // namespace strategy
