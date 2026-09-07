#pragma once
#include "app/GameState.hpp"
#include "persistence/GameConfig.hpp"
#include "ui/UiController.hpp"

#include <array>
#include <string_view>
namespace strategy {
class SettingsState final : public GameState {
  public:
    explicit SettingsState(StateContext& context);
    void handleEvent(const SDL_Event&) override;
    void update(float deltaSeconds) override;
    void render(Renderer&) const override;
    StateRequest takeRequest() override {
        const StateRequest result = request_;
        request_ = StateRequest::none;
        return result;
    }

  private:
    GameConfig config_;
    StateRequest request_{StateRequest::none};
    std::size_t resolution_{0};
    int binding_{-1};
    std::uint32_t windowId_{0};
    static constexpr std::array<std::pair<int, int>, 4> resolutions{
        {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}}};
    void apply();
    [[nodiscard]] UiDocument uiDocument(int width, int height) const;
    void activateControl(std::string_view id, int direction = 1);
    mutable UiController uiController_;
};
} // namespace strategy
