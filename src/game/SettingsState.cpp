#include "game/SettingsState.hpp"

#include "app/GameEvents.hpp"
#include "audio/AudioSystem.hpp"
#include "core/EventBus.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>

namespace strategy {
namespace {
constexpr const char* bindingNames[]{"forward", "backward", "left", "right", "debug", "pause"};
int settingItem(float x, float y) {
    if (x >= 60 && x <= 430) {
        if (y >= 130 && y <= 168) return 0;
        if (y >= 175 && y <= 213) return 1;
        for (int row = 0; row < 4; ++row)
            if (y >= 285 + row * 45 && y <= 323 + row * 45) return 2 + row;
    }
    if (x >= 470 && x <= 850) {
        for (int row = 0; row < 6; ++row)
            if (y >= 130 + row * 45 && y <= 168 + row * 45) return 6 + row;
        if (y >= 500 && y <= 538) return 12;
        if (y >= 545 && y <= 583) return 13;
    }
    return -1;
}
float adjustedVolume(float value, float clickX) {
    const float change = clickX < 245.0F ? -0.1F : 0.1F;
    return std::clamp(value + change, 0.0F, 1.0F);
}
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

void SettingsState::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        windowId_ = event.motion.windowID;
        hovered_ = settingItem(event.motion.x, event.motion.y);
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
        if (event.key.key == SDLK_ESCAPE)
            request_ = StateRequest::returnToMainMenu;
        return;
    }
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT)
        return;
    context_.events.enqueue(AudioEvent{AudioCue::uiClick});
    windowId_ = event.button.windowID;
    const int row = settingItem(event.button.x, event.button.y);
    if (row == 0)
        resolution_ = (resolution_ + 1) % resolutions.size();
    else if (row == 1)
        config_.fullscreen = !config_.fullscreen;
    else if (row == 2)
        config_.masterVolume = adjustedVolume(config_.masterVolume, event.button.x);
    else if (row == 3)
        config_.musicVolume = adjustedVolume(config_.musicVolume, event.button.x);
    else if (row == 4)
        config_.effectsVolume = adjustedVolume(config_.effectsVolume, event.button.x);
    else if (row == 5)
        config_.muted = !config_.muted;
    else if (row >= 6 && row < 12)
        binding_ = row - 6;
    else if (row == 12) {
        apply();
        request_ = StateRequest::returnToMainMenu;
    } else if (row == 13)
        request_ = StateRequest::returnToMainMenu;
}

void SettingsState::render(Renderer& renderer) const {
    renderer.drawSettings(config_, resolution_, hovered_, binding_);
}
} // namespace strategy
