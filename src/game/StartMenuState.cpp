#include "game/StartMenuState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace strategy {

StartMenuState::StartMenuState(StateContext& context)
    : GameState(context)
    , config_(GameConfig::load(context.configPath)) {
    if (!context_.renderer)
        throw std::runtime_error("Main menu requires a renderer");
    screen_ = context_.renderer->pushUiScreen({"assets/ui/main_menu.rml",
                                               {{"menu-title", Text::get("menu.title")},
                                                {"play-label", Text::get("menu.play")},
                                                {"build-label", Text::get("menu.build")},
                                                {"load-label", Text::get("menu.load")},
                                                {"settings-label", Text::get("menu.settings")},
                                                {"exit-label", Text::get("menu.exit")}},
                                               {"play", "build", "load", "settings", "exit"},
                                               {"play", "build", "load", "settings", "exit"},
                                               config_.uiScale});
}

StartMenuState::~StartMenuState() {
    context_.renderer->removeUiScreen(screen_);
}

void StartMenuState::activateControl(std::string_view id) {
    if (transitionOut_ >= 0.0F)
        return;
    if (id == "play")
        request_ = StateRequest::openMatchSetup;
    else if (id == "build")
        request_ = StateRequest::buildMap;
    else if (id == "load")
        request_ = StateRequest::loadGame;
    else if (id == "settings")
        request_ = StateRequest::openSettings;
    else if (id == "exit")
        request_ = StateRequest::exitApplication;
    if (request_ != StateRequest::none) {
        context_.events.enqueue(AudioEvent{AudioCue::uiClick});
        transitionOut_ = 0.0F;
    }
}

void StartMenuState::handleEvent(const SDL_Event& event) {
    if (transitionOut_ >= 0.0F)
        return;
    context_.renderer->handleUiEvent(event);

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_TAB || event.key.key == SDLK_DOWN) {
            context_.renderer->focusUi(1);
            return;
        }
        if (event.key.key == SDLK_UP) {
            context_.renderer->focusUi(-1);
            return;
        }
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE)
            context_.renderer->activateFocusedUiItem();
        else if (event.key.key == SDLK_ESCAPE)
            activateControl("exit");
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
            context_.renderer->focusUi(1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
            context_.renderer->focusUi(-1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)
            context_.renderer->activateFocusedUiItem();
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            activateControl("exit");
    }

    if (const auto action = context_.renderer->takeUiAction())
        activateControl(*action);
}

void StartMenuState::update(float deltaSeconds) {
    backgroundTime_ += deltaSeconds;
    if (transitionOut_ >= 0.0F)
        transitionOut_ += deltaSeconds;
}

void StartMenuState::render(Renderer& renderer) const {
    renderer.drawMenuBackground(std::sin(backgroundTime_ * 0.10F));
    renderer.drawRmlUi();
    const float fadeIn = 1.0F - std::clamp(backgroundTime_ / 0.60F, 0.0F, 1.0F);
    const float fadeOut =
        transitionOut_ < 0.0F ? 0.0F : std::clamp(transitionOut_ / 0.24F, 0.0F, 1.0F);
    renderer.drawScreenFade(std::max(fadeIn, fadeOut));
}

StateRequest StartMenuState::takeRequest() {
    if (request_ == StateRequest::none || transitionOut_ < 0.24F)
        return StateRequest::none;
    const StateRequest result = request_;
    request_ = StateRequest::none;
    if (result == StateRequest::openSettings)
        transitionOut_ = -1.0F;
    return result;
}

} // namespace strategy
