#include "game/SettingsState.hpp"

#include "app/GameEvents.hpp"
#include "audio/AudioSystem.hpp"
#include "core/EventBus.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace strategy {
namespace {
constexpr const char* bindingNames[]{"forward",
                                     "backward",
                                     "left",
                                     "right",
                                     "debug",
                                     "terrain_debug",
                                     "water_debug",
                                     "power_debug",
                                     "pause"};
constexpr const char* actionKeys[]{"settings.action.forward",
                                   "settings.action.backward",
                                   "settings.action.left",
                                   "settings.action.right",
                                   "settings.action.debug",
                                   "settings.action.terrain_debug",
                                   "settings.action.water_debug",
                                   "settings.action.power_debug",
                                   "settings.action.pause"};
std::string baseId(std::string_view id, int& direction) {
    if (id.ends_with(".left")) {
        direction = -1;
        id.remove_suffix(5);
    } else if (id.ends_with(".right")) {
        direction = 1;
        id.remove_suffix(6);
    }
    return std::string{id};
}
} // namespace

SettingsState::SettingsState(StateContext& context)
    : GameState(context)
    , config_(GameConfig::load(context.configPath))
    , originalUiScale_(config_.uiScale) {
    if (!context_.renderer)
        throw std::runtime_error("Settings requires a renderer");
    for (std::size_t index = 0; index < resolutions.size(); ++index)
        if (resolutions[index].first == config_.resolutionWidth &&
            resolutions[index].second == config_.resolutionHeight)
            resolution_ = index;
    std::vector<std::string> focus{"settings.resolution",
                                   "settings.fullscreen",
                                   "settings.ui_scale",
                                   "settings.master",
                                   "settings.music",
                                   "settings.effects",
                                   "settings.muted"};
    std::vector<std::string> actions;
    for (const std::string id : {"settings.resolution",
                                 "settings.ui_scale",
                                 "settings.master",
                                 "settings.music",
                                 "settings.effects"}) {
        actions.push_back(id + ".left");
        actions.push_back(id + ".right");
    }
    actions.insert(actions.end(), {"settings.fullscreen", "settings.muted"});
    for (const char* name : bindingNames) {
        focus.push_back(std::string{"settings.bind."} + name);
        actions.push_back(std::string{"settings.bind."} + name);
    }
    focus.insert(focus.end(), {"settings.cancel", "settings.apply"});
    actions.insert(actions.end(), {"settings.cancel", "settings.apply"});
    screen_ =
        context_.renderer->pushUiScreen({"assets/ui/settings.rml",
                                         {{"settings-title", Text::get("settings.title")},
                                          {"settings-video", Text::get("settings.video")},
                                          {"settings-audio", Text::get("settings.audio")},
                                          {"settings-controls", Text::get("settings.controls")},
                                          {"resolution-label", Text::get("settings.label.resolution")},
                                          {"fullscreen-label", Text::get("settings.label.fullscreen")},
                                          {"ui-scale-label", Text::get("settings.label.ui_scale")},
                                          {"master-label", Text::get("settings.label.master")},
                                          {"music-label", Text::get("settings.label.music")},
                                          {"effects-label", Text::get("settings.label.effects")},
                                          {"mute-label", Text::get("settings.label.mute")},
                                          {"apply-label", Text::get("settings.apply")},
                                          {"cancel-label", Text::get("settings.cancel")}},
                                         std::move(focus),
                                         std::move(actions),
                                         config_.uiScale});
    refreshUi();
}

SettingsState::~SettingsState() {
    if (!applied_)
        context_.renderer->setUiScale(originalUiScale_);
    context_.renderer->removeUiScreen(screen_);
}

void SettingsState::refreshUi() {
    const auto percent = [](float value) {
        return std::to_string(static_cast<int>(std::round(value * 100.0F))) + "%";
    };
    const std::string enabled = Text::get("settings.on"), disabled = Text::get("settings.off");
    context_.renderer->setUiText("settings.resolution.value",
                                 std::to_string(resolutions[resolution_].first) + " x " +
                                     std::to_string(resolutions[resolution_].second));
    context_.renderer->setUiText("settings.fullscreen.value", config_.fullscreen ? enabled : disabled);
    context_.renderer->setUiText("settings.ui_scale.value", percent(config_.uiScale));
    context_.renderer->setUiText("settings.master.value", percent(config_.masterVolume));
    context_.renderer->setUiText("settings.music.value", percent(config_.musicVolume));
    context_.renderer->setUiText("settings.effects.value", percent(config_.effectsVolume));
    context_.renderer->setUiText("settings.muted.value", config_.muted ? enabled : disabled);
    for (int row = 0; row < static_cast<int>(std::size(bindingNames)); ++row) {
        context_.renderer->setUiText(std::string{"bind."} + bindingNames[row] + ".label",
                                     Text::get(actionKeys[row]));
        const auto found = config_.keybinds.find(bindingNames[row]);
        const SDL_Keycode key = found == config_.keybinds.end() ? 0 : found->second;
        context_.renderer->setUiText(
            std::string{"bind."} + bindingNames[row] + ".value",
            binding_ == row ? Text::get("settings.press_key") : SDL_GetKeyName(key));
    }
}

void SettingsState::beginTransition(StateRequest request) {
    if (transitionOut_ >= 0.0F)
        return;
    request_ = request;
    transitionOut_ = 0.0F;
}

void SettingsState::apply() {
    config_.resolutionWidth = resolutions[resolution_].first;
    config_.resolutionHeight = resolutions[resolution_].second;
    config_.write(context_.configPath);
    applied_ = true;
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

void SettingsState::activateControl(std::string_view rawId, int direction) {
    const std::string id = baseId(rawId, direction);
    if (id == "settings.resolution")
        resolution_ = static_cast<std::size_t>(
            (static_cast<int>(resolution_) + direction + static_cast<int>(resolutions.size())) %
            static_cast<int>(resolutions.size()));
    else if (id == "settings.fullscreen")
        config_.fullscreen = !config_.fullscreen;
    else if (id == "settings.ui_scale") {
        config_.uiScale = std::clamp(config_.uiScale + direction * .1F, .75F, 1.5F);
        context_.renderer->setUiScale(config_.uiScale);
    }
    else if (id == "settings.master")
        config_.masterVolume = std::clamp(config_.masterVolume + direction * .1F, 0.F, 1.F);
    else if (id == "settings.music")
        config_.musicVolume = std::clamp(config_.musicVolume + direction * .1F, 0.F, 1.F);
    else if (id == "settings.effects")
        config_.effectsVolume = std::clamp(config_.effectsVolume + direction * .1F, 0.F, 1.F);
    else if (id == "settings.muted")
        config_.muted = !config_.muted;
    else if (id.starts_with("settings.bind.")) {
        const std::string name{id.substr(std::string_view{"settings.bind."}.size())};
        const auto found = std::find(std::begin(bindingNames), std::end(bindingNames), name);
        if (found != std::end(bindingNames))
            binding_ = static_cast<int>(found - std::begin(bindingNames));
    } else if (id == "settings.apply") {
        apply();
        beginTransition(StateRequest::returnToMainMenu);
    } else if (id == "settings.cancel")
        beginTransition(StateRequest::returnToMainMenu);
    refreshUi();
}

void SettingsState::handleEvent(const SDL_Event& event) {
    if (transitionOut_ >= 0.0F)
        return;
    context_.renderer->handleUiEvent(event);
    if (event.type == SDL_EVENT_MOUSE_MOTION)
        windowId_ = event.motion.windowID;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        windowId_ = event.button.windowID;
    if (event.type == SDL_EVENT_KEY_DOWN) {
        windowId_ = event.key.windowID;
        if (binding_ >= 0) {
            if (event.key.key == SDLK_ESCAPE)
                binding_ = -1;
            else {
                config_.keybinds[bindingNames[binding_]] = static_cast<std::int32_t>(event.key.key);
                binding_ = -1;
            }
            refreshUi();
            return;
        }
        if (event.key.key == SDLK_TAB || event.key.key == SDLK_DOWN)
            context_.renderer->focusUi((SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
        else if (event.key.key == SDLK_UP)
            context_.renderer->focusUi(-1);
        else if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)
            activateControl(context_.renderer->focusedUiId(), event.key.key == SDLK_LEFT ? -1 : 1);
        else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE)
            context_.renderer->activateFocusedUiItem();
        else if (event.key.key == SDLK_ESCAPE)
            beginTransition(StateRequest::returnToMainMenu);
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
            context_.renderer->focusUi(1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
            context_.renderer->focusUi(-1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                 event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
            activateControl(context_.renderer->focusedUiId(),
                            event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? -1 : 1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)
            context_.renderer->activateFocusedUiItem();
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            beginTransition(StateRequest::returnToMainMenu);
    }
    if (const auto action = context_.renderer->takeUiAction()) {
        context_.events.enqueue(AudioEvent{AudioCue::uiClick});
        activateControl(*action);
    }
}

void SettingsState::render(Renderer& renderer) const {
    renderer.drawMenuBackground(std::sin(menuTime_ * .10F));
    renderer.drawRmlUi();
    const float fadeIn = 1.F - std::clamp(menuTime_ / .5F, 0.F, 1.F);
    const float fadeOut = transitionOut_ < 0.F ? 0.F : std::clamp(transitionOut_ / .24F, 0.F, 1.F);
    renderer.drawScreenFade(std::max(fadeIn, fadeOut));
}
void SettingsState::update(float deltaSeconds) {
    menuTime_ += deltaSeconds;
    if (transitionOut_ >= 0.F)
        transitionOut_ += deltaSeconds;
}
StateRequest SettingsState::takeRequest() {
    if (request_ == StateRequest::none || transitionOut_ < .24F)
        return StateRequest::none;
    return std::exchange(request_, StateRequest::none);
}
} // namespace strategy
