#include "game/MatchSetupState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <charconv>

namespace strategy {

MatchSetupState::MatchSetupState(StateContext& context)
    : GameState(context), config_(GameConfig::load(context.configPath)) {
    const auto& countries = context_.definitions.countries();
    for (std::size_t index = 0; index < countries.size(); ++index) {
        if (countries[index].id == "spain") playerOneCountry_ = index;
        if (countries[index].id == "japan") playerTwoCountry_ = index;
    }
}

UiDocument MatchSetupState::document(int width, int height) const {
    UiDocument ui;
    ui.modal("match.panel", {120, 35, 1160, 685}, {0.025F, 0.04F, 0.06F});
    ui.label("match.title", {160, 70, 0, 0}, Text::get("match_setup.title"), 3.0F);
    ui.label("match.mode", {160, 112, 0, 0}, Text::get("match_setup.mode_1v1"), 1.4F,
             {0.55F, 0.75F, 0.84F});

    const auto selector = [&ui](const std::string& id, float top,
                                const std::string& label, const std::string& value) {
        ui.label(id + ".label", {160, top, 0, 0}, label, 1.45F);
        ui.button(id, {160, top + 25, 600, top + 70}, "<     " + value + "     >");
    };
    const auto& countries = context_.definitions.countries();
    selector("match.player_country", 155, Text::get("match_setup.player_country"),
             Text::get(countries[playerOneCountry_].nameKey));
    selector("match.opponent_country", 245, Text::get("match_setup.opponent_country"),
             Text::get(countries[playerTwoCountry_].nameKey));
    selector("match.map_size", 335, Text::get("match_setup.map_size"),
             Text::get(mapSizes[mapSize_].nameKey));
    selector("match.starting_resources", 425, Text::get("match_setup.starting_resources"),
             Text::get(resourceStarts[startingResources_].nameKey));
    selector("match.abundance", 515, Text::get("match_setup.resource_abundance"),
             Text::get(resourceAbundance[abundance_].nameKey));

    ui.label("match.seed.label", {680, 155, 0, 0}, Text::get("match_setup.seed"), 1.45F);
    ui.textField("match.seed", {680, 180, 1100, 225}, seedText_).textScale = 1.8F;
    ui.label("match.summary.map", {680, 270, 1085, 300},
             Text::format("match_setup.summary_map", {Text::get(mapSizes[mapSize_].nameKey)}),
             1.25F, {0.72F, 0.80F, 0.84F});
    ui.label("match.summary.resources", {680, 305, 1085, 335},
             Text::format("match_setup.summary_resources",
                          {Text::get(resourceStarts[startingResources_].nameKey)}),
             1.25F, {0.72F, 0.80F, 0.84F});
    ui.label("match.summary.deposits", {680, 340, 1085, 370},
             Text::format("match_setup.summary_deposits",
                          {Text::get(resourceAbundance[abundance_].nameKey)}),
             1.25F, {0.72F, 0.80F, 0.84F});
    ui.button("match.start", {680, 515, 1100, 570}, Text::get("match_setup.start"),
              {0.16F, 0.36F, 0.18F}, {0.28F, 0.62F, 0.24F}).textScale = 2.4F;
    ui.button("match.back", {680, 585, 1100, 635}, Text::get("match_setup.back")).textScale = 2.0F;
    ui.scaleFromReference(width, height, config_.uiScale);
    return ui;
}

void MatchSetupState::cycle(std::size_t& value, std::size_t count, int direction) {
    value = static_cast<std::size_t>((static_cast<int>(value) + direction +
        static_cast<int>(count)) % static_cast<int>(count));
}

void MatchSetupState::activate(std::string_view id, int direction) {
    const std::size_t countries = context_.definitions.countries().size();
    if (id == "match.player_country") cycle(playerOneCountry_, countries, direction);
    else if (id == "match.opponent_country") cycle(playerTwoCountry_, countries, direction);
    else if (id == "match.map_size") cycle(mapSize_, mapSizes.size(), direction);
    else if (id == "match.starting_resources")
        cycle(startingResources_, resourceStarts.size(), direction);
    else if (id == "match.abundance") cycle(abundance_, resourceAbundance.size(), direction);
    else if (id == "match.start") request_ = StateRequest::startGame;
    else if (id == "match.back") request_ = StateRequest::returnToMainMenu;
}

void MatchSetupState::handleEvent(const SDL_Event& event) {
    int width = config_.resolutionWidth, height = config_.resolutionHeight;
    std::uint32_t windowId = 0;
    if (event.type == SDL_EVENT_MOUSE_MOTION) windowId = event.motion.windowID;
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) windowId = event.button.windowID;
    else if (event.type == SDL_EVENT_KEY_DOWN) windowId = event.key.windowID;
    if (SDL_Window* window = SDL_GetWindowFromID(windowId)) SDL_GetWindowSize(window, &width, &height);
    UiDocument ui = document(width, height);
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        controller_.pointerMoved({event.motion.x, event.motion.y});
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        context_.events.enqueue(AudioEvent{AudioCue::uiClick});
        if (const auto id = controller_.press(ui, {event.button.x, event.button.y})) {
            const UiElement* element = ui.find(*id);
            const float center = element ? (element->bounds.left + element->bounds.right) * 0.5F
                                         : width * 0.5F;
            activate(*id, event.button.x < center ? -1 : 1);
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (controller_.focusedId() == "match.seed") {
            if (event.key.key == SDLK_BACKSPACE && !seedText_.empty()) seedText_.pop_back();
            else if (event.key.key >= SDLK_0 && event.key.key <= SDLK_9 && seedText_.size() < 10)
                seedText_.push_back(static_cast<char>('0' + event.key.key - SDLK_0));
            else if (event.key.key == SDLK_ESCAPE) request_ = StateRequest::returnToMainMenu;
            if (event.key.key != SDLK_TAB) return;
        }
        if (event.key.key == SDLK_TAB)
            controller_.moveFocus(ui, (SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
        else if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
            if (const auto id = controller_.activateFocused(ui))
                activate(*id, event.key.key == SDLK_LEFT ? -1 : 1);
        } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
            if (const auto id = controller_.activateFocused(ui)) activate(*id);
        } else if (event.key.key == SDLK_ESCAPE) request_ = StateRequest::returnToMainMenu;
        return;
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) controller_.moveFocus(ui, 1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) controller_.moveFocus(ui, -1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                 event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
            if (const auto id = controller_.activateFocused(ui))
                activate(*id, event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? -1 : 1);
        } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            if (const auto id = controller_.activateFocused(ui)) activate(*id);
        } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            request_ = StateRequest::returnToMainMenu;
    }
}

void MatchSetupState::update(float deltaSeconds) { controller_.advance(deltaSeconds); }

void MatchSetupState::render(Renderer& renderer) const {
    UiDocument ui = document(renderer.viewportWidth(), renderer.viewportHeight());
    controller_.apply(ui);
    renderer.drawUi(ui);
}

StateRequest MatchSetupState::takeRequest() {
    const StateRequest result = request_;
    request_ = StateRequest::none;
    return result;
}

MatchSetupOptions MatchSetupState::matchSetup() const {
    std::uint32_t seed = 0x5EED1234U;
    const auto parsed = std::from_chars(seedText_.data(), seedText_.data() + seedText_.size(), seed);
    if (parsed.ec != std::errc{}) seed = 0x5EED1234U;
    const auto& countries = context_.definitions.countries();
    return {seed, countries[playerOneCountry_].id, countries[playerTwoCountry_].id,
            static_cast<std::uint32_t>(mapSizes[mapSize_].value),
            resourceStarts[startingResources_].value,
            resourceAbundance[abundance_].value};
}

} // namespace strategy
