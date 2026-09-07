#include "game/SettingsState.hpp"

#include "app/GameEvents.hpp"
#include "audio/AudioSystem.hpp"
#include "core/EventBus.hpp"
#include "render/Renderer.hpp"
#include "localization/Text.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace strategy {
namespace {
constexpr const char* bindingNames[]{"forward", "backward", "left", "right", "debug", "pause"};
}

UiDocument SettingsState::uiDocument(int width, int height) const {
    UiDocument ui;
    const auto percent = [](float value) {
        return std::to_string(static_cast<int>(std::round(value * 100.0F))) + "%";
    };
    const std::string enabled = Text::get("settings.on"), disabled = Text::get("settings.off");
    ui.modal("settings.panel", {30, 30, 880, 620}, {0.035F, 0.055F, 0.075F});
    ui.label("settings.title", {60, 52, 0, 0}, Text::get("settings.title"), 3.0F);
    ui.label("settings.video", {60, 100, 0, 0}, Text::get("settings.video"), 2.0F,
             {0.35F, 0.72F, 0.92F});
    ui.button("settings.resolution", {60, 130, 430, 168},
              Text::format("settings.resolution", {std::to_string(resolutions[resolution_].first),
                                                    std::to_string(resolutions[resolution_].second)}));
    ui.button("settings.fullscreen", {60, 175, 430, 213},
              Text::format("settings.fullscreen", {config_.fullscreen ? enabled : disabled}));
    ui.button("settings.ui_scale", {60, 218, 430, 248},
              Text::format("settings.ui_scale", {percent(config_.uiScale)}));
    ui.label("settings.audio", {60, 250, 0, 0}, Text::get("settings.audio"), 2.0F,
             {0.35F, 0.72F, 0.92F});
    const std::array<std::pair<const char*, std::string>, 4> audio{{
        {"settings.master", Text::format("settings.master_volume", {percent(config_.masterVolume)})},
        {"settings.music", Text::format("settings.music_volume", {percent(config_.musicVolume)})},
        {"settings.effects", Text::format("settings.effects_volume", {percent(config_.effectsVolume)})},
        {"settings.muted", Text::format("settings.mute", {config_.muted ? enabled : disabled})}}};
    for (std::size_t row = 0; row < audio.size(); ++row)
        ui.button(audio[row].first, {60.0F, 285.0F + row * 45.0F, 430.0F,
                                    323.0F + row * 45.0F}, audio[row].second);
    static constexpr const char* actionKeys[]{"settings.action.forward", "settings.action.backward",
        "settings.action.left", "settings.action.right", "settings.action.debug", "settings.action.pause"};
    ui.label("settings.controls", {470, 100, 0, 0}, Text::get("settings.controls"), 2.0F,
             {0.35F, 0.72F, 0.92F});
    for (int row = 0; row < 6; ++row) {
        const auto found = config_.keybinds.find(bindingNames[row]);
        const SDL_Keycode key = found == config_.keybinds.end() ? 0 : found->second;
        const std::string value = binding_ == row ? Text::get("settings.press_key")
            : Text::format("settings.bound_to", {SDL_GetKeyName(key)});
        ui.button(std::string("settings.bind.") + bindingNames[row],
                  {470.0F, 130.0F + row * 45.0F, 850.0F, 168.0F + row * 45.0F},
                  Text::get(actionKeys[row]) + "     " + value);
    }
    ui.button("settings.apply", {470, 500, 850, 538}, Text::get("settings.apply"));
    ui.button("settings.cancel", {470, 545, 850, 583}, Text::get("settings.cancel"));
    ui.scaleFromReference(width, height, config_.uiScale);
    return ui;
}

SettingsState::SettingsState(StateContext& context)
    : GameState(context)
    , config_(GameConfig::load(context.configPath)) {
    for (std::size_t index = 0; index < resolutions.size(); ++index)
        if (resolutions[index].first == config_.resolutionWidth &&
            resolutions[index].second == config_.resolutionHeight)
            resolution_ = index;
}

void SettingsState::apply() {
    config_.resolutionWidth = resolutions[resolution_].first;
    config_.resolutionHeight = resolutions[resolution_].second;
    config_.write(context_.configPath);
    context_.audio.setMasterVolume(config_.masterVolume);
    context_.audio.setMusicVolume(config_.musicVolume);
    context_.audio.setEffectsVolume(config_.effectsVolume);
    context_.audio.setMuted(config_.muted);
    if (SDL_Window* window = SDL_GetWindowFromID(windowId_)) {
        SDL_SetWindowFullscreen(window, config_.fullscreen);
        if (!config_.fullscreen)
            SDL_SetWindowSize(window, config_.resolutionWidth, config_.resolutionHeight);
    }
}

void SettingsState::activateControl(std::string_view id, int direction) {
    if (id == "settings.resolution")
        resolution_ = static_cast<std::size_t>(
            (static_cast<int>(resolution_) + direction + static_cast<int>(resolutions.size())) %
            static_cast<int>(resolutions.size()));
    else if (id == "settings.fullscreen") config_.fullscreen = !config_.fullscreen;
    else if (id == "settings.ui_scale")
        config_.uiScale = std::clamp(config_.uiScale + direction * 0.1F, 0.75F, 1.5F);
    else if (id == "settings.master")
        config_.masterVolume = std::clamp(config_.masterVolume + direction * 0.1F, 0.0F, 1.0F);
    else if (id == "settings.music")
        config_.musicVolume = std::clamp(config_.musicVolume + direction * 0.1F, 0.0F, 1.0F);
    else if (id == "settings.effects")
        config_.effectsVolume = std::clamp(config_.effectsVolume + direction * 0.1F, 0.0F, 1.0F);
    else if (id == "settings.muted") config_.muted = !config_.muted;
    else if (id.starts_with("settings.bind.")) {
        const std::string name{id.substr(std::string_view{"settings.bind."}.size())};
        const auto found = std::find(std::begin(bindingNames), std::end(bindingNames), name);
        if (found != std::end(bindingNames))
            binding_ = static_cast<int>(found - std::begin(bindingNames));
    } else if (id == "settings.apply") {
        apply();
        request_ = StateRequest::returnToMainMenu;
    } else if (id == "settings.cancel") request_ = StateRequest::returnToMainMenu;
}

void SettingsState::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        windowId_ = event.motion.windowID;
        uiController_.pointerMoved({event.motion.x, event.motion.y});
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN) {
        windowId_ = event.key.windowID;
        if (binding_ >= 0) {
            if (event.key.key == SDLK_ESCAPE)
                binding_ = -1;
            else {
                config_.keybinds[bindingNames[binding_]] = static_cast<std::int32_t>(event.key.key);
                binding_ = -1;
            }
            return;
        }
        UiDocument ui = uiDocument(config_.resolutionWidth, config_.resolutionHeight);
        if (event.key.key == SDLK_TAB) {
            uiController_.moveFocus(ui, (SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
            return;
        }
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
            if (const auto activated = uiController_.activateFocused(ui))
                activateControl(*activated);
            return;
        }
        if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
            if (const auto activated = uiController_.activateFocused(ui))
                activateControl(*activated, event.key.key == SDLK_LEFT ? -1 : 1);
            return;
        }
        if (event.key.key == SDLK_ESCAPE)
            request_ = StateRequest::returnToMainMenu;
        return;
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        UiDocument ui = uiDocument(config_.resolutionWidth, config_.resolutionHeight);
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
            uiController_.moveFocus(ui, 1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
            uiController_.moveFocus(ui, -1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                 event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
            if (const auto activated = uiController_.activateFocused(ui))
                activateControl(*activated,
                    event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? -1 : 1);
        } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            if (const auto activated = uiController_.activateFocused(ui))
                activateControl(*activated);
        } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            request_ = StateRequest::returnToMainMenu;
        return;
    }
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT)
        return;
    context_.events.enqueue(AudioEvent{AudioCue::uiClick});
    windowId_ = event.button.windowID;
    int width = 1280, height = 720;
    if (SDL_Window* window = SDL_GetWindowFromID(windowId_))
        SDL_GetWindowSize(window, &width, &height);
    UiDocument ui = uiDocument(width, height);
    const auto activated = uiController_.press(ui, {event.button.x, event.button.y});
    if (!activated) return;
    const UiElement* control = ui.find(*activated);
    const float controlCenter = control
        ? (control->bounds.left + control->bounds.right) * 0.5F
        : width * 0.5F;
    activateControl(*activated, event.button.x < controlCenter ? -1 : 1);
}

void SettingsState::render(Renderer& renderer) const {
    UiDocument ui = uiDocument(renderer.viewportWidth(), renderer.viewportHeight());
    uiController_.apply(ui);
    renderer.drawUi(ui);
}

void SettingsState::update(float deltaSeconds) {
    uiController_.advance(deltaSeconds);
}
} // namespace strategy
