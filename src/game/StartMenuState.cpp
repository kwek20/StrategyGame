#include "game/StartMenuState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <charconv>

namespace strategy {

StartMenuState::StartMenuState(StateContext& context)
    : GameState(context)
    , config_(GameConfig::load(context.configPath)) {
    const auto& countries = context_.definitions.countries();
    for (std::size_t index = 0; index < countries.size(); ++index) {
        if (countries[index].id == "spain")
            playerOneCountryIndex_ = index;
        if (countries[index].id == "japan")
            playerTwoCountryIndex_ = index;
    }
    ui_.panel("panel", {30, 70, 800, 625}, {0.035F, 0.055F, 0.075F});
    ui_.label("title", {60, 108, 0, 0}, Text::get("menu.title"), 3.0F);
    ui_.label("seed_label", {60, 157, 0, 0}, Text::get("menu.seed"), 2.0F);
    ui_.textField("seed", {60, 185, 300, 230}, seedText_).textScale = 2.0F;
    ui_.button("start", {60, 250, 300, 310}, Text::get("menu.start"),
               {0.16F, 0.36F, 0.18F}, {0.28F, 0.62F, 0.24F}).textScale = 3.0F;
    ui_.button("build", {60, 320, 300, 380}, Text::get("menu.build"),
               {0.14F, 0.27F, 0.40F}, {0.25F, 0.48F, 0.70F}).textScale = 3.0F;
    ui_.button("load", {60, 390, 300, 450}, Text::get("menu.load"),
               {0.31F, 0.27F, 0.13F}, {0.54F, 0.46F, 0.20F}).textScale = 3.0F;
    ui_.button("settings", {60, 460, 300, 520}, Text::get("menu.settings")).textScale = 3.0F;
    ui_.button("exit", {60, 530, 300, 590}, Text::get("menu.exit"),
               {0.40F, 0.16F, 0.14F}, {0.72F, 0.25F, 0.20F}).textScale = 3.0F;
    ui_.label("team_a_label", {380, 157, 0, 0}, Text::get("menu.team_a_country"), 2.0F);
    ui_.button("team_a_previous", {380, 185, 570, 230}, "<");
    ui_.button("team_a_next", {570, 185, 760, 230}, ">");
    ui_.label("team_b_label", {380, 222, 0, 0}, Text::get("menu.team_b_country"), 2.0F);
    ui_.button("team_b_previous", {380, 250, 570, 295}, "<");
    ui_.button("team_b_next", {570, 250, 760, 295}, ">");
    ui_.label("team_a_value", {425, 192, 0, 0}, "", 2.0F);
    ui_.label("team_b_value", {425, 257, 0, 0}, "", 2.0F);
    ui_.label("controls", {380, 312, 0, 0}, Text::get("menu.country_controls"), 1.5F);
    refreshUiText();
}

void StartMenuState::cycleCountry(std::size_t& index, int direction) {
    const std::size_t count = context_.definitions.countries().size();
    index = static_cast<std::size_t>(
        (static_cast<int>(index) + direction + static_cast<int>(count)) % static_cast<int>(count));
    refreshUiText();
}

void StartMenuState::refreshUiText() {
    ui_.setText("seed", seedText_.empty() ? "0" : seedText_);
    ui_.setText("team_a_value",
                Text::get(context_.definitions.countries()[playerOneCountryIndex_].nameKey));
    ui_.setText("team_b_value",
                Text::get(context_.definitions.countries()[playerTwoCountryIndex_].nameKey));
}

void StartMenuState::activateControl(std::string_view id) {
    if (id == "team_a_previous") cycleCountry(playerOneCountryIndex_, -1);
    else if (id == "team_a_next") cycleCountry(playerOneCountryIndex_, 1);
    else if (id == "team_b_previous") cycleCountry(playerTwoCountryIndex_, -1);
    else if (id == "team_b_next") cycleCountry(playerTwoCountryIndex_, 1);
    else if (id == "start") request_ = StateRequest::startGame;
    else if (id == "build") request_ = StateRequest::buildMap;
    else if (id == "load") request_ = StateRequest::loadGame;
    else if (id == "settings") request_ = StateRequest::openSettings;
    else if (id == "exit") request_ = StateRequest::exitApplication;
    refreshUiText();
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
        if (event.key.key == SDLK_Q) {
            cycleCountry(playerOneCountryIndex_, -1);
            return;
        }
        if (event.key.key == SDLK_E) {
            cycleCountry(playerOneCountryIndex_, 1);
            return;
        }
        if (event.key.key == SDLK_Z) {
            cycleCountry(playerTwoCountryIndex_, -1);
            return;
        }
        if (event.key.key == SDLK_C) {
            cycleCountry(playerTwoCountryIndex_, 1);
            return;
        }
        if (uiController_.focusedId() == "seed" && event.key.key == SDLK_BACKSPACE) {
            if (!seedText_.empty()) {
                seedText_.pop_back();
                refreshUiText();
            }
            return;
        }
        if (uiController_.focusedId() == "seed" && event.key.key >= SDLK_0 && event.key.key <= SDLK_9) {
            if (seedText_.size() < 10) {
                seedText_.push_back(static_cast<char>('0' + event.key.key - SDLK_0));
                refreshUiText();
            }
            return;
        }
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
            if (const auto activated = uiController_.activateFocused(responsive))
                activateControl(*activated);
            else
                request_ = StateRequest::startGame;
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

std::string StartMenuState::playerOneCountry() const {
    return context_.definitions.countries()[playerOneCountryIndex_].id;
}
std::string StartMenuState::playerTwoCountry() const {
    return context_.definitions.countries()[playerTwoCountryIndex_].id;
}

std::uint32_t StartMenuState::terrainSeed() const {
    std::uint32_t result = 0x5EED1234U;
    const auto conversion =
        std::from_chars(seedText_.data(), seedText_.data() + seedText_.size(), result);
    return conversion.ec == std::errc{} ? result : 0x5EED1234U;
}

} // namespace strategy
