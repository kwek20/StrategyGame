#include "game/StartMenuState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
namespace strategy {

StartMenuState::StartMenuState(StateContext& context)
    : GameState(context)
    , config_(GameConfig::load(context.configPath)) {
    ui_.panel("panel", {30, 70, 420, 625}, {0.035F, 0.055F, 0.075F});
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
}

void StartMenuState::activateControl(std::string_view id) {
    if (id == "play") request_ = StateRequest::openMatchSetup;
    else if (id == "build") request_ = StateRequest::buildMap;
    else if (id == "load") request_ = StateRequest::loadGame;
    else if (id == "settings") request_ = StateRequest::openSettings;
    else if (id == "exit") request_ = StateRequest::exitApplication;
}

void StartMenuState::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        uiController_.pointerMoved({event.motion.x, event.motion.y});
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        context_.events.enqueue(AudioEvent{AudioCue::uiClick});
        int width = 1280, height = 720;
        if (SDL_Window* window = SDL_GetWindowFromID(event.button.windowID))
            SDL_GetWindowSize(window, &width, &height);
        UiDocument responsive = ui_;
        responsive.scaleFromReference(width, height, config_.uiScale);
        const auto activated = uiController_.press(
            responsive, {event.button.x, event.button.y});
        if (activated) activateControl(*activated);
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
                request_ = StateRequest::openMatchSetup;
        } else if (event.key.key == SDLK_ESCAPE) {
            request_ = StateRequest::exitApplication;
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
            request_ = StateRequest::exitApplication;
    }
}

void StartMenuState::update(float deltaSeconds) {
    uiController_.advance(deltaSeconds);
}

void StartMenuState::render(Renderer& renderer) const {
    UiDocument responsive = ui_;
    responsive.scaleFromReference(
        renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
    uiController_.apply(responsive);
    renderer.drawUi(responsive);
}

} // namespace strategy
