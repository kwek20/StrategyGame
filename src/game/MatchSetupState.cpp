#include "game/MatchSetupState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "diagnostics/Logger.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <random>
#include <stdexcept>
#include <utility>

namespace strategy {
MatchSetupState::MatchSetupState(StateContext& context)
    : GameState(context)
    , config_(GameConfig::load(context.configPath))
    , terrainDefinitions_(TerrainGenerationDefinitions::load()) {
    if (!context_.renderer)
        throw std::runtime_error("Match setup requires a renderer");
    const auto& countries = context_.definitions.countries();
    for (std::size_t i = 0; i < countries.size(); ++i) {
        if (countries[i].id == "spain")
            playerOneCountry_ = i;
        if (countries[i].id == "japan")
            playerTwoCountry_ = i;
    }
    try {
        const MatchSetupProfile saved = MatchSetupProfileStore::load(config_.matchSetupPath());
        seedText_ = std::to_string(saved.terrainSeed);
        for (std::size_t i = 0; i < countries.size(); ++i) {
            if (countries[i].id == saved.playerOneCountry)
                playerOneCountry_ = i;
            if (countries[i].id == saved.playerTwoCountry)
                playerTwoCountry_ = i;
        }
        const auto closest = [](const auto& values, float wanted) {
            std::size_t result = 0;
            for (std::size_t i = 1; i < values.size(); ++i)
                if (std::abs(values[i].value - wanted) < std::abs(values[result].value - wanted))
                    result = i;
            return result;
        };
        mapSize_ = closest(mapSizes, static_cast<float>(saved.mapChunksPerSide));
        startingResources_ = closest(resourceStarts, saved.startingResourcesScale);
        abundance_ = closest(resourceAbundance, saved.resourceAbundanceScale);
        const auto& layouts = terrainDefinitions_.layouts();
        for (std::size_t i = 0; i < layouts.size(); ++i)
            if (layouts[i].id.value == saved.terrainLayout)
                terrainLayout_ = i;
    } catch (const std::exception& error) {
        context_.logger.warning(
            "configuration",
            std::string{"Could not load match setup profile; using defaults: "} + error.what());
    }
    const std::vector<std::string> controls{"match.player_country",
                                            "match.opponent_country",
                                            "match.terrain_layout",
                                            "match.map_size",
                                            "match.starting_resources",
                                            "match.abundance"};
    std::vector<std::string> focus = controls, actions;
    for (const auto& id : controls) {
        actions.push_back(id + ".left");
        actions.push_back(id + ".right");
    }
    focus.insert(focus.end(), {"match.seed", "match.random_seed", "match.back", "match.start"});
    actions.insert(actions.end(), {"match.seed", "match.random_seed", "match.back", "match.start"});
    screen_ = context_.renderer->pushUiScreen(
        {"assets/ui/match_setup.rml",
         {{"match-title", Text::get("match_setup.title")},
          {"match-mode", Text::get("match_setup.mode_1v1")},
          {"players-header", Text::get("match_setup.section_players")},
          {"world-header", Text::get("match_setup.section_world")},
          {"economy-header", Text::get("match_setup.section_economy")},
          {"options-header", Text::get("match_setup.section_options")},
          {"player-label", Text::get("match_setup.player_country")},
          {"opponent-label", Text::get("match_setup.opponent_country")},
          {"layout-label", Text::get("match_setup.terrain_layout")},
          {"size-label", Text::get("match_setup.map_size")},
          {"resources-label", Text::get("match_setup.starting_resources")},
          {"abundance-label", Text::get("match_setup.resource_abundance")},
          {"seed-label", Text::get("match_setup.seed")},
          {"random-label", Text::get("match_setup.random_seed")},
          {"summary-header", Text::get("match_setup.summary")},
          {"preview-caption", Text::get("match_setup.tactical_overview")},
          {"start-label", Text::get("match_setup.start")},
          {"back-label", Text::get("match_setup.back")}},
         std::move(focus),
         std::move(actions),
         config_.uiScale});
    refreshUi();
}
MatchSetupState::~MatchSetupState() {
    context_.renderer->removeUiScreen(screen_);
}
void MatchSetupState::refreshUi() {
    const auto& c = context_.definitions.countries();
    const auto& l = terrainDefinitions_.layouts();
    auto text = [this](const std::string& id, const std::string& value) {
        context_.renderer->setUiText(id, value);
    };
    text("match.player_country.value", Text::get(c[playerOneCountry_].nameKey));
    text("match.opponent_country.value", Text::get(c[playerTwoCountry_].nameKey));
    text("match.terrain_layout.value", Text::get(l[terrainLayout_].nameKey));
    text("match.map_size.value", Text::get(mapSizes[mapSize_].nameKey));
    text("match.starting_resources.value", Text::get(resourceStarts[startingResources_].nameKey));
    text("match.abundance.value", Text::get(resourceAbundance[abundance_].nameKey));
    text("match.seed.value", seedText_);
    text("summary-player",
         Text::get("match_setup.summary_player") + "  " + Text::get(c[playerOneCountry_].nameKey));
    text("summary-opponent",
         Text::get("match_setup.summary_opponent") + "  " +
             Text::get(c[playerTwoCountry_].nameKey));
    text("summary-map",
         Text::get("match_setup.summary_map_label") + "  " + Text::get(mapSizes[mapSize_].nameKey));
    text("summary-resources",
         Text::get("match_setup.summary_resources_label") + "  " +
             Text::get(resourceStarts[startingResources_].nameKey));
    text("summary-deposits",
         Text::get("match_setup.summary_deposits_label") + "  " +
             Text::get(resourceAbundance[abundance_].nameKey));
    context_.renderer->setUiAttribute("player-emblem",
                                      "src",
                                      "../textures/ui/country_emblem_" + c[playerOneCountry_].id +
                                          ".png");
    context_.renderer->setUiAttribute("opponent-emblem",
                                      "src",
                                      "../textures/ui/country_emblem_" + c[playerTwoCountry_].id +
                                          ".png");
}
void MatchSetupState::beginTransition(StateRequest r) {
    if (transitionOut_ >= 0.F)
        return;
    request_ = r;
    transitionOut_ = 0.F;
}
void MatchSetupState::cycle(std::size_t& v, std::size_t count, int d) {
    v = static_cast<std::size_t>((static_cast<int>(v) + d + static_cast<int>(count)) %
                                 static_cast<int>(count));
}
void MatchSetupState::activate(std::string_view id, int direction) {
    if (id.ends_with(".left")) {
        id.remove_suffix(5);
        direction = -1;
    } else if (id.ends_with(".right")) {
        id.remove_suffix(6);
        direction = 1;
    }
    bool changed = true;
    const auto count = context_.definitions.countries().size();
    if (id == "match.player_country")
        cycle(playerOneCountry_, count, direction);
    else if (id == "match.opponent_country")
        cycle(playerTwoCountry_, count, direction);
    else if (id == "match.terrain_layout")
        cycle(terrainLayout_, terrainDefinitions_.layouts().size(), direction);
    else if (id == "match.map_size")
        cycle(mapSize_, mapSizes.size(), direction);
    else if (id == "match.starting_resources")
        cycle(startingResources_, resourceStarts.size(), direction);
    else if (id == "match.abundance")
        cycle(abundance_, resourceAbundance.size(), direction);
    else if (id == "match.random_seed") {
        std::random_device r;
        seedText_ = std::to_string((static_cast<std::uint32_t>(r()) << 16U) ^
                                   static_cast<std::uint32_t>(r()));
    } else if (id == "match.start") {
        persist();
        beginTransition(StateRequest::startGame);
        return;
    } else if (id == "match.back") {
        persist();
        beginTransition(StateRequest::returnToMainMenu);
        return;
    } else
        changed = false;
    if (changed) {
        refreshUi();
        persist();
    }
}
void MatchSetupState::handleEvent(const SDL_Event& event) {
    if (transitionOut_ >= 0.F)
        return;
    context_.renderer->handleUiEvent(event);
    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (context_.renderer->focusedUiId() == "match.seed") {
            bool changed = false;
            if (event.key.key == SDLK_BACKSPACE && !seedText_.empty()) {
                seedText_.pop_back();
                changed = true;
            } else if (event.key.key >= SDLK_0 && event.key.key <= SDLK_9 &&
                       seedText_.size() < 10) {
                seedText_.push_back(static_cast<char>('0' + event.key.key - SDLK_0));
                changed = true;
            }
            if (changed) {
                refreshUi();
                persist();
            }
            if (event.key.key != SDLK_TAB && event.key.key != SDLK_ESCAPE)
                return;
        }
        if (event.key.key == SDLK_TAB || event.key.key == SDLK_DOWN)
            context_.renderer->focusUi((SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
        else if (event.key.key == SDLK_UP)
            context_.renderer->focusUi(-1);
        else if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)
            activate(context_.renderer->focusedUiId(), event.key.key == SDLK_LEFT ? -1 : 1);
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
            activate(context_.renderer->focusedUiId(),
                     event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? -1 : 1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)
            context_.renderer->activateFocusedUiItem();
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            beginTransition(StateRequest::returnToMainMenu);
    }
    if (const auto action = context_.renderer->takeUiAction()) {
        context_.events.enqueue(AudioEvent{AudioCue::uiClick});
        activate(*action);
    }
}
void MatchSetupState::update(float d) {
    menuTime_ += d;
    if (transitionOut_ >= 0.F)
        transitionOut_ += d;
}
void MatchSetupState::render(Renderer& r) const {
    r.drawMenuBackground(std::sin(menuTime_ * .10F));
    r.drawRmlUi();
    const float in = 1.F - std::clamp(menuTime_ / .5F, 0.F, 1.F),
                out = transitionOut_ < 0.F ? 0.F : std::clamp(transitionOut_ / .24F, 0.F, 1.F);
    r.drawScreenFade(std::max(in, out));
}
StateRequest MatchSetupState::takeRequest() {
    if (request_ == StateRequest::none || transitionOut_ < .24F)
        return StateRequest::none;
    return std::exchange(request_, StateRequest::none);
}
MatchSetupOptions MatchSetupState::currentSetup() const {
    std::uint32_t seed = 0x5EED1234U;
    const auto parsed =
        std::from_chars(seedText_.data(), seedText_.data() + seedText_.size(), seed);
    if (parsed.ec != std::errc{})
        seed = 0x5EED1234U;
    const auto& c = context_.definitions.countries();
    const auto& l = terrainDefinitions_.layouts();
    return {seed,
            c[playerOneCountry_].id,
            c[playerTwoCountry_].id,
            static_cast<std::uint32_t>(mapSizes[mapSize_].value),
            resourceStarts[startingResources_].value,
            resourceAbundance[abundance_].value,
            l[terrainLayout_].id.value};
}
MatchSetupOptions MatchSetupState::matchSetup() const {
    return currentSetup();
}
void MatchSetupState::persist() {
    try {
        MatchSetupProfileStore::write(config_.matchSetupPath(), currentSetup());
    } catch (const std::exception& e) {
        context_.logger.error("configuration",
                              std::string{"Could not persist match setup: "} + e.what());
    }
}
} // namespace strategy
