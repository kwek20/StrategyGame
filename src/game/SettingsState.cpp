#include "game/SettingsState.hpp"

#include "audio/AudioSystem.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace strategy {
namespace {
std::string escape(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '\'')
            out += "&#39;";
        else
            out += c;
    }
    return out;
}
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}
std::string contexts(unsigned mask) {
    std::string out;
    for (const auto& [bit, name] :
         {std::pair{1U, "strategy"}, {2U, "placement"}, {4U, "direct"}, {8U, "editor"}})
        if (mask & bit) {
            if (!out.empty())
                out += " / ";
            out += Text::get(std::string{"keybind.context."} + name);
        }
    return out;
}
} // namespace
SettingsState::SettingsState(StateContext& context)
    : GameState(context)
    , config_(GameConfig::load(context.configPath))
    , originalUiScale_(config_.uiScale) {
    if (!context_.renderer)
        throw std::runtime_error("Settings requires a renderer");
    for (std::size_t i = 0; i < resolutions.size(); ++i)
        if (resolutions[i].first == config_.resolutionWidth &&
            resolutions[i].second == config_.resolutionHeight)
            resolution_ = i;
    if (std::none_of(resolutions.begin(), resolutions.end(), [&](const auto& size) {
            return size.first == config_.resolutionWidth && size.second == config_.resolutionHeight;
        })) {
        resolutions.push_back({config_.resolutionWidth, config_.resolutionHeight});
        resolution_ = resolutions.size() - 1;
    }
    screen_ =
        context_.renderer->pushUiScreen({"assets/ui/settings.rml", {}, {}, {}, config_.uiScale});
    for (const auto& [id, key] :
         {std::pair{"settings-title", "settings.title"},
          std::pair{"settings.tab.video", "settings.video"},
          std::pair{"settings.tab.audio", "settings.audio"},
          std::pair{"settings.tab.keybinds", "settings.keybinds"},
          std::pair{"settings-display", "settings.display"},
          std::pair{"settings-video-help", "settings.video_help"},
          std::pair{"settings-audio-title", "settings.audio"},
          std::pair{"settings-audio-help", "settings.audio_help"},
          std::pair{"resolution-label", "settings.label.resolution"},
          std::pair{"fullscreen-label", "settings.label.fullscreen"},
          std::pair{"ui-scale-label", "settings.label.ui_scale"},
          std::pair{"master-label", "settings.label.master"},
          std::pair{"music-label", "settings.label.music"},
          std::pair{"effects-label", "settings.label.effects"},
          std::pair{"mute-label", "settings.label.mute"},
          std::pair{"settings-note", "settings.note"},
          std::pair{"cancel-label", "settings.back"},
          std::pair{"apply-label", "settings.apply"},
          std::pair{"settings.preset.default", "keybind.preset_default"},
          std::pair{"settings.preset.grid", "keybind.preset_grid"},
          std::pair{"keybind-action-heading", "keybind.heading_action"},
          std::pair{"keybind-primary-heading", "keybind.heading_primary"},
          std::pair{"keybind-secondary-heading", "keybind.heading_secondary"},
          std::pair{"settings.capture.replace", "keybind.replace"},
          std::pair{"settings.capture.clear", "keybind.clear"},
          std::pair{"settings.capture.cancel", "keybind.cancel"},
          std::pair{"settings.reset.category", "keybind.reset_category"},
          std::pair{"settings.reset.all", "keybind.reset_all"},
          std::pair{"keybind.option.all", "keybind.all_categories"},
          std::pair{"keybind.option.camera", "keybind.category.camera"},
          std::pair{"keybind.option.selection", "keybind.category.selection"},
          std::pair{"keybind.option.building", "keybind.category.building"},
          std::pair{"keybind.option.orders", "keybind.category.orders"},
          std::pair{"keybind.option.direct", "keybind.category.direct"},
          std::pair{"keybind.option.interface", "keybind.category.interface"},
          std::pair{"keybind.option.diagnostics", "keybind.category.diagnostics"},
          std::pair{"keybind.option.editor", "keybind.category.editor"}})
        context_.renderer->setUiText(id, Text::get(key));
    context_.renderer->setUiAttribute("keybind-search", "placeholder", Text::get("keybind.search"));
    refreshUi();
}
SettingsState::~SettingsState() {
    context_.renderer->setUiScale(originalUiScale_);
    if (auto* window = SDL_GetWindowFromID(windowId_))
        SDL_StopTextInput(window);
    context_.renderer->removeUiScreen(screen_);
}
void SettingsState::finishCapture() {
    captureAction_.clear();
    proposed_.reset();
    pendingModifier_.reset();
}
void SettingsState::refreshBindings() {
    std::ostringstream markup;
    std::vector<std::string> focus{"settings.tab.video",
                                   "settings.tab.audio",
                                   "settings.tab.keybinds",
                                   "keybind-search",
                                   "keybind-category",
                                   "settings.preset.default",
                                   "settings.preset.grid"};
    std::vector<std::string> actions{"settings.tab.video",
                                     "settings.tab.audio",
                                     "settings.tab.keybinds",
                                     "settings.preset.default",
                                     "settings.preset.grid"};
    const auto query = lower(filter_);
    for (const auto& action : inputActions()) {
        if (!category_.empty() && action.category != category_)
            continue;
        const auto& slots = config_.keybinds.values.at(action.id);
        const auto name = Text::get(action.labelKey);
        const auto context =
            Text::get("keybind.category." + action.category) + " / " + contexts(action.contexts);
        if (!query.empty() && lower(name + " " + action.id + " " + context + " " +
                                    bindingName(slots[0]) + " " + bindingName(slots[1]))
                                      .find(query) == std::string::npos)
            continue;
        markup << "<div class='binding-row'><div class='binding-label'><div class='binding-title'>"
               << escape(name) << "</div><div class='binding-context'>" << escape(context)
               << "</div></div>";
        for (unsigned i = 0; i < 2; ++i) {
            const std::string id = "settings.bind." + action.id + "." + std::to_string(i);
            const bool capturing = captureAction_ == action.id && captureSlot_ == i;
            markup << "<button id='" << id << "' class='binding" << (capturing ? " capturing" : "")
                   << "'>"
                   << escape(capturing && !proposed_ ? Text::get("settings.press_key")
                                                     : bindingName(slots[i]))
                   << "</button>";
            focus.push_back(id);
            actions.push_back(id);
        }
        markup << "</div>";
    }
    context_.renderer->setUiText("binding-list", markup.str());
    if (!captureAction_.empty()) {
        if (proposed_) {
            focus.push_back("settings.capture.replace");
            actions.push_back("settings.capture.replace");
        }
        for (const std::string id : {"settings.capture.clear", "settings.capture.cancel"}) {
            focus.push_back(id);
            actions.push_back(id);
        }
    }
    for (const std::string id :
         {"settings.reset.category", "settings.reset.all", "settings.cancel", "settings.apply"}) {
        if (id == "settings.reset.category" && category_.empty())
            continue;
        focus.push_back(id);
        actions.push_back(id);
    }
    context_.renderer->setUiControls(std::move(focus), std::move(actions));
    context_.renderer->setUiProperty(
        "capture-actions", "display", captureAction_.empty() ? "none" : "flex");
    context_.renderer->setUiProperty(
        "settings.capture.replace", "display", proposed_ ? "block" : "none");
    context_.renderer->setUiProperty(
        "settings.capture.clear", "display", captureAction_.empty() ? "none" : "block");
    context_.renderer->setUiProperty(
        "settings.capture.cancel", "display", captureAction_.empty() ? "none" : "block");
    context_.renderer->setUiProperty(
        "settings.reset.category", "display", category_.empty() ? "none" : "block");
    context_.renderer->setUiText("binding-status",
                                 escape(status_.empty() ? Text::get("keybind.help") : status_));
}
void SettingsState::refreshUi() {
    for (const std::string tab : {"video", "audio", "keybinds"}) {
        context_.renderer->setUiProperty("page-" + tab,
                                         "display",
                                         tab == tab_ ? (tab == "keybinds" ? "flex" : "block")
                                                     : "none");
        context_.renderer->setUiAttribute(
            "settings.tab." + tab, "class", tab == tab_ ? "tab active" : "tab");
    }
    const auto percent = [](float value) {
        return std::to_string(static_cast<int>(std::round(value * 100))) + "%";
    };
    context_.renderer->setUiText("settings.resolution.value",
                                 std::to_string(resolutions[resolution_].first) + " x " +
                                     std::to_string(resolutions[resolution_].second));
    context_.renderer->setUiText("settings.fullscreen.value",
                                 Text::get(config_.fullscreen ? "settings.on" : "settings.off"));
    context_.renderer->setUiText("settings.ui_scale.value", percent(config_.uiScale));
    context_.renderer->setUiText("settings.master.value", percent(config_.masterVolume));
    context_.renderer->setUiText("settings.music.value", percent(config_.musicVolume));
    context_.renderer->setUiText("settings.effects.value", percent(config_.effectsVolume));
    context_.renderer->setUiText("settings.muted.value",
                                 Text::get(config_.muted ? "settings.on" : "settings.off"));
    if (tab_ == "keybinds")
        refreshBindings();
    else {
        std::vector<std::string> focus{
            "settings.tab.video", "settings.tab.audio", "settings.tab.keybinds"},
            actions = focus;
        const std::vector<std::string> names =
            tab_ == "video" ? std::vector<std::string>{"resolution", "fullscreen", "ui_scale"}
                            : std::vector<std::string>{"master", "music", "effects", "muted"};
        for (const auto& name : names) {
            const auto id = "settings." + name;
            focus.push_back(id);
            if (name == "fullscreen" || name == "muted")
                actions.push_back(id);
            else {
                actions.push_back(id + ".left");
                actions.push_back(id + ".right");
            }
        }
        for (const std::string id : {"settings.cancel", "settings.apply"}) {
            focus.push_back(id);
            actions.push_back(id);
        }
        context_.renderer->setUiControls(std::move(focus), std::move(actions));
    }
}
void SettingsState::propose(InputBinding binding) {
    if (!binding || captureAction_.empty())
        return;
    if (inputAction(captureAction_)->requiresRelease && binding.device == BindingDevice::wheel) {
        status_ = Text::get("keybind.needs_hold");
        refreshBindings();
        return;
    }
    const auto& current = config_.keybinds.values.at(captureAction_)[captureSlot_];
    if (current.device == BindingDevice::drag && binding.device == BindingDevice::mouse)
        binding.device = BindingDevice::drag;
    auto conflicts = config_.keybinds.conflicts(captureAction_, captureSlot_, binding);
    if (!conflicts.empty()) {
        proposed_ = binding;
        std::string names;
        for (const auto& id : conflicts) {
            if (!names.empty())
                names += ", ";
            names += Text::get(inputAction(id)->labelKey);
        }
        status_ = Text::format("keybind.conflict", {bindingName(binding), names});
    } else {
        config_.keybinds.values[captureAction_][captureSlot_] = binding;
        status_ =
            Text::format("keybind.assigned",
                         {Text::get(inputAction(captureAction_)->labelKey), bindingName(binding)});
        finishCapture();
    }
    refreshBindings();
}
void SettingsState::apply() {
    for (const auto& action : inputActions())
        for (unsigned slot = 0; slot < 2; ++slot) {
            const auto conflicts = config_.keybinds.conflicts(
                action.id, slot, config_.keybinds.values.at(action.id)[slot]);
            if (!conflicts.empty()) {
                tab_ = "keybinds";
                finishCapture();
                status_ = Text::format("keybind.apply_conflict",
                                       {Text::get(action.labelKey),
                                        Text::get(inputAction(conflicts.front())->labelKey)});
                refreshUi();
                return;
            }
        }
    config_.resolutionWidth = resolutions[resolution_].first;
    config_.resolutionHeight = resolutions[resolution_].second;
    config_.write(context_.configPath);
    ++context_.settingsRevision;
    originalUiScale_ = config_.uiScale;
    context_.audio.setMasterVolume(config_.masterVolume);
    context_.audio.setMusicVolume(config_.musicVolume);
    context_.audio.setEffectsVolume(config_.effectsVolume);
    context_.audio.setMuted(config_.muted);
    if (auto* window = SDL_GetWindowFromID(windowId_)) {
        SDL_SetWindowFullscreen(window, config_.fullscreen);
        if (!config_.fullscreen)
            SDL_SetWindowSize(window, config_.resolutionWidth, config_.resolutionHeight);
    }
    status_ = Text::get("keybind.saved");
    finishCapture();
    refreshUi();
}
void SettingsState::beginTransition(StateRequest request) {
    if (transitionOut_ < 0) {
        request_ = request;
        transitionOut_ = 0;
    }
}
void SettingsState::activateControl(std::string_view raw, int direction) {
    std::string id{raw};
    if (id.ends_with(".left")) {
        id.resize(id.size() - 5);
        direction = -1;
    } else if (id.ends_with(".right")) {
        id.resize(id.size() - 6);
        direction = 1;
    }
    if (id.starts_with("settings.tab.")) {
        tab_ = id.substr(13);
        finishCapture();
        status_.clear();
    } else if (id == "settings.apply") {
        apply();
        return;
    } else if (id == "settings.cancel")
        beginTransition(StateRequest::returnToMainMenu);
    else if (id == "settings.resolution")
        resolution_ = (resolution_ + resolutions.size() + direction) % resolutions.size();
    else if (id == "settings.fullscreen")
        config_.fullscreen = !config_.fullscreen;
    else if (id == "settings.ui_scale") {
        config_.uiScale = std::clamp(config_.uiScale + direction * .1F, .75F, 1.5F);
        context_.renderer->setUiScale(config_.uiScale);
    } else if (id == "settings.master")
        config_.masterVolume = std::clamp(config_.masterVolume + direction * .1F, 0.F, 1.F);
    else if (id == "settings.music")
        config_.musicVolume = std::clamp(config_.musicVolume + direction * .1F, 0.F, 1.F);
    else if (id == "settings.effects")
        config_.effectsVolume = std::clamp(config_.effectsVolume + direction * .1F, 0.F, 1.F);
    else if (id == "settings.muted")
        config_.muted = !config_.muted;
    else if (id.starts_with("settings.bind.")) {
        finishCapture();
        const auto split = id.find_last_of('.');
        captureAction_ = id.substr(14, split - 14);
        captureSlot_ = static_cast<unsigned>(std::stoul(id.substr(split + 1)));
        status_ =
            Text::format("keybind.capture", {Text::get(inputAction(captureAction_)->labelKey)});
    } else if (id == "settings.capture.cancel") {
        finishCapture();
        status_.clear();
    } else if (id == "settings.capture.clear") {
        config_.keybinds.values[captureAction_][captureSlot_] = {};
        finishCapture();
        status_ = Text::get("keybind.cleared");
    } else if (id == "settings.capture.replace" && proposed_) {
        const auto* action = inputAction(captureAction_);
        for (const auto& conflict :
             config_.keybinds.conflicts(captureAction_, captureSlot_, *proposed_)) {
            const auto* other = inputAction(conflict);
            for (auto& binding : config_.keybinds.values[conflict])
                if (inputBindingsOverlap(
                        *proposed_, binding, action->extraModifiers, other->extraModifiers))
                    binding = {};
        }
        config_.keybinds.values[captureAction_][captureSlot_] = *proposed_;
        finishCapture();
        status_ = Text::get("keybind.replaced");
    } else if (id == "settings.reset.category" || id == "settings.reset.all") {
        config_.keybinds.reset(id == "settings.reset.all" ? std::string_view{}
                                                          : std::string_view{category_});
        finishCapture();
        status_ = Text::get("keybind.reset");
    } else if (id.starts_with("settings.preset.")) {
        config_.keybinds.applyPreset(id.substr(16));
        finishCapture();
        status_ = Text::get("keybind.reset");
    }
    refreshUi();
}
void SettingsState::handleEvent(const SDL_Event& event) {
    if (transitionOut_ >= 0)
        return;
    if (event.type == SDL_EVENT_MOUSE_MOTION)
        windowId_ = event.motion.windowID;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        windowId_ = event.button.windowID;
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        windowId_ = event.key.windowID;
    if (!captureAction_.empty() && !proposed_) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            context_.renderer->handleUiEvent(event);
            return;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            for (const std::string id : {"settings.capture.cancel", "settings.capture.clear"})
                if (const auto bounds = context_.renderer->uiBounds(id);
                    bounds && bounds->contains(event.button.x, event.button.y)) {
                    activateControl(id);
                    return;
                }
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            finishCapture();
            status_.clear();
            refreshBindings();
            return;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (modifierForKey(event.key.key))
                pendingModifier_ =
                    InputBinding{BindingDevice::modifier,
                                 static_cast<int>(normalizedModifiers(event.key.mod) |
                                                  modifierForKey(event.key.key))};
            else {
                pendingModifier_.reset();
                propose(bindingFromEvent(event));
            }
        } else if (event.type == SDL_EVENT_KEY_UP && pendingModifier_ &&
                   modifierForKey(event.key.key)) {
            auto binding = *pendingModifier_;
            pendingModifier_.reset();
            propose(binding);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                   event.type == SDL_EVENT_MOUSE_WHEEL) {
            pendingModifier_.reset();
            propose(bindingFromEvent(event));
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        if (!captureAction_.empty()) {
            finishCapture();
            status_.clear();
            refreshBindings();
        } else
            beginTransition(StateRequest::returnToMainMenu);
        return;
    }
    context_.renderer->handleUiEvent(event);
    const auto focused = context_.renderer->focusedUiId();
    const bool editing = focused == "keybind-search" || focused == "keybind-category";
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_TAB)
            context_.renderer->focusUi((event.key.mod & SDL_KMOD_SHIFT) ? -1 : 1);
        else if (!editing && event.key.key == SDLK_DOWN)
            context_.renderer->focusUi(1);
        else if (!editing && event.key.key == SDLK_UP)
            context_.renderer->focusUi(-1);
        else if (!editing && (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT))
            activateControl(focused, event.key.key == SDLK_LEFT ? -1 : 1);
        else if (!editing && (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE))
            context_.renderer->activateFocusedUiItem();
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
            context_.renderer->focusUi(1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
            context_.renderer->focusUi(-1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)
            context_.renderer->activateFocusedUiItem();
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            beginTransition(StateRequest::returnToMainMenu);
    }
    if (const auto action = context_.renderer->takeUiAction())
        activateControl(*action);
}
void SettingsState::update(float seconds) {
    menuTime_ += seconds;
    if (transitionOut_ >= 0)
        transitionOut_ += seconds;
    if (auto* window = SDL_GetWindowFromID(windowId_)) {
        if (tab_ == "keybinds" && context_.renderer->focusedUiId() == "keybind-search" &&
            captureAction_.empty()) {
            if (!SDL_TextInputActive(window))
                SDL_StartTextInput(window);
        } else if (SDL_TextInputActive(window))
            SDL_StopTextInput(window);
    }
    if (tab_ == "keybinds" && captureAction_.empty()) {
        const auto filter = context_.renderer->uiValue("keybind-search"),
                   category = context_.renderer->uiValue("keybind-category");
        if (filter != filter_ || category != category_) {
            filter_ = filter;
            category_ = category;
            context_.renderer->resetUiScroll("binding-list");
            refreshBindings();
        }
    }
}
void SettingsState::render(Renderer& renderer) const {
    renderer.drawMenuBackground(std::sin(menuTime_ * .1F));
    renderer.drawRmlUi();
    renderer.drawScreenFade(
        std::max(1.F - std::clamp(menuTime_ / .5F, 0.F, 1.F),
                 transitionOut_ < 0 ? 0.F : std::clamp(transitionOut_ / .24F, 0.F, 1.F)));
}
StateRequest SettingsState::takeRequest() {
    return request_ == StateRequest::none || transitionOut_ < .24F
               ? StateRequest::none
               : std::exchange(request_, StateRequest::none);
}
} // namespace strategy
