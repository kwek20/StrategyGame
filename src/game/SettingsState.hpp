#pragma once
#include "app/GameState.hpp"
#include "persistence/GameConfig.hpp"
#include "render/RmlUiManager.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace strategy {
class SettingsState final : public GameState {
  public:
    explicit SettingsState(StateContext& context);
    ~SettingsState() override;
    void handleEvent(const SDL_Event&) override;
    void update(float deltaSeconds) override;
    void render(Renderer&) const override;
    StateRequest takeRequest() override;

  private:
    GameConfig config_;
    float originalUiScale_{1};
    StateRequest request_{StateRequest::none};
    std::size_t resolution_{};
    std::string tab_{"video"}, filter_, category_, captureAction_, status_;
    unsigned captureSlot_{};
    std::optional<InputBinding> pendingModifier_, proposed_;
    std::uint32_t windowId_{};
    std::vector<std::pair<int, int>> resolutions{
        {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
    void apply();
    void refreshUi();
    void refreshBindings();
    void propose(InputBinding binding);
    void finishCapture();
    void activateControl(std::string_view id, int direction = 1);
    float menuTime_{}, transitionOut_{-1};
    RmlUiScreenHandle screen_{};
    void beginTransition(StateRequest request);
};
} // namespace strategy
