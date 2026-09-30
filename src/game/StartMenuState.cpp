#include "game/StartMenuState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <cmath>
namespace strategy {

StartMenuState::StartMenuState(StateContext& context)
    : GameState(context)
    , config_(GameConfig::load(context.configPath)) {
    ui_.panel("panel", {8, 35, 442, 660}, {0.72F, 0.76F, 0.80F}).texture = "ui/menu_panel";
    ui_.label("title", {60, 108, 0, 0}, Text::get("menu.title"), 3.0F);
    ui_.button("play", {60, 180, 390, 240}, Text::get("menu.play"),
               {0.16F, 0.36F, 0.18F}, {0.28F, 0.62F, 0.24F}).textScale = 3.0F;
    ui_.button("build", {60, 250, 390, 310}, Text::get("menu.build"),
               {0.14F, 0.27F, 0.40F}, {0.25F, 0.48F, 0.70F}).textScale = 3.0F;
    ui_.button("load", {60, 320, 390, 380}, Text::get("menu.load"),
               {0.31F, 0.27F, 0.13F}, {0.54F, 0.46F, 0.20F}).textScale = 3.0F;
    ui_.button("settings", {60, 390, 390, 450}, Text::get("menu.settings")).textScale = 3.0F;
    ui_.button("exit", {60, 500, 390, 560}, Text::get("menu.exit"),
               {0.40F, 0.16F, 0.14F}, {0.72F, 0.25F, 0.20F}).textScale = 3.0F;
    for (UiElement& element : ui_.elements())
        if (element.kind == UiElementKind::button) element.texture = "ui/menu_button";
    if (UiElement* item = const_cast<UiElement*>(ui_.find("play"))) item->icon = "action_move";
    if (UiElement* item = const_cast<UiElement*>(ui_.find("build"))) item->icon = "action_construct";
    if (UiElement* item = const_cast<UiElement*>(ui_.find("load"))) item->icon = "resource_data";
    if (UiElement* item = const_cast<UiElement*>(ui_.find("settings"))) item->icon = "action_power_priority";
    if (UiElement* item = const_cast<UiElement*>(ui_.find("exit"))) item->icon = "action_stop";
    ui_.label("version", {1060, 682, 1245, 704}, "v0.1.0  |  DEVELOPMENT", 1.1F,
              {0.72F, 0.80F, 0.84F});
    uiController_.focus("play");
}

void StartMenuState::activateControl(std::string_view id) {
    if (transitionOut_ >= 0.0F) return;
    if (id == "play") request_ = StateRequest::openMatchSetup;
    else if (id == "build") request_ = StateRequest::buildMap;
    else if (id == "load") request_ = StateRequest::loadGame;
    else if (id == "settings") request_ = StateRequest::openSettings;
    else if (id == "exit") request_ = StateRequest::exitApplication;
    if (request_ != StateRequest::none) transitionOut_ = 0.0F;
}

void StartMenuState::handleEvent(const SDL_Event& event) {
    if (transitionOut_ >= 0.0F) return;
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        uiController_.pointerMoved({event.motion.x, event.motion.y});
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        int width = 1280, height = 720;
        if (SDL_Window* window = SDL_GetWindowFromID(event.button.windowID))
            SDL_GetWindowSize(window, &width, &height);
        UiDocument responsive = ui_;
        responsive.scaleFromReference(width, height, config_.uiScale);
        (void)uiController_.press(responsive, {event.button.x, event.button.y});
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
        int width = 1280, height = 720;
        if (SDL_Window* window = SDL_GetWindowFromID(event.button.windowID))
            SDL_GetWindowSize(window, &width, &height);
        UiDocument responsive = ui_;
        responsive.scaleFromReference(width, height, config_.uiScale);
        if (const auto activated = uiController_.release(
                responsive, {event.button.x, event.button.y})) {
            context_.events.enqueue(AudioEvent{AudioCue::uiClick});
            activateControl(*activated);
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN) {
        UiDocument responsive = ui_;
        responsive.scaleFromReference(config_.resolutionWidth, config_.resolutionHeight,
                                      config_.uiScale);
        if (event.key.key == SDLK_TAB) {
            uiController_.moveFocus(responsive,
                (SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
            return;
        }
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
            if (const auto activated = uiController_.activateFocused(responsive))
                activateControl(*activated);
            else
                activateControl("play");
        } else if (event.key.key == SDLK_ESCAPE) {
            activateControl("exit");
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        UiDocument responsive = ui_;
        responsive.scaleFromReference(config_.resolutionWidth, config_.resolutionHeight,
                                      config_.uiScale);
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
            uiController_.moveFocus(responsive, 1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
            uiController_.moveFocus(responsive, -1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            if (const auto activated = uiController_.activateFocused(responsive))
                activateControl(*activated);
        } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            activateControl("exit");
    }
}

void StartMenuState::update(float deltaSeconds) {
    backgroundTime_ += deltaSeconds;
    if (transitionOut_ >= 0.0F) transitionOut_ += deltaSeconds;
    uiController_.advance(deltaSeconds);
}

void StartMenuState::render(Renderer& renderer) const {
    renderer.drawMenuBackground(std::sin(backgroundTime_ * 0.10F));
    UiDocument responsive = ui_;
    responsive.scaleFromReference(
        renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
    uiController_.apply(responsive);
    const float entrance = std::clamp(backgroundTime_ / 0.45F, 0.0F, 1.0F);
    const float eased = 1.0F - (1.0F - entrance) * (1.0F - entrance);
    for (UiElement& element : responsive.elements())
        if (element.id != "version") {
            const float offset = (1.0F - eased) * -44.0F;
            element.bounds.left += offset;
            element.bounds.right += offset;
        }
    renderer.drawUi(responsive);
    const float fadeIn = 1.0F - std::clamp(backgroundTime_ / 0.60F, 0.0F, 1.0F);
    const float fadeOut = transitionOut_ < 0.0F ? 0.0F : std::clamp(transitionOut_ / 0.24F, 0.0F, 1.0F);
    renderer.drawScreenFade(std::max(fadeIn, fadeOut));
}

StateRequest StartMenuState::takeRequest() {
    if (request_ == StateRequest::none || transitionOut_ < 0.24F) return StateRequest::none;
    const StateRequest result = request_;
    request_ = StateRequest::none;
    // Settings is pushed over this state rather than replacing it. Reset the completed
    // fade before suspension so popping settings cannot reveal a permanently black menu.
    if (result == StateRequest::openSettings) transitionOut_ = -1.0F;
    return result;
}

} // namespace strategy
