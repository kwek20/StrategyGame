#include "game/MatchSetupState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "diagnostics/Logger.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <charconv>
#include <cmath>
#include <exception>
#include <random>

namespace strategy {

MatchSetupState::MatchSetupState(StateContext& context)
    : GameState(context)
    , config_(GameConfig::load(context.configPath))
    , terrainDefinitions_(TerrainGenerationDefinitions::load()) {
    const auto& countries = context_.definitions.countries();
    for (std::size_t index = 0; index < countries.size(); ++index) {
        if (countries[index].id == "spain") playerOneCountry_ = index;
        if (countries[index].id == "japan") playerTwoCountry_ = index;
    }
    try {
        const MatchSetupProfile saved = MatchSetupProfileStore::load(config_.matchSetupPath());
        seedText_ = std::to_string(saved.terrainSeed);
        for (std::size_t index = 0; index < countries.size(); ++index) {
            if (countries[index].id == saved.playerOneCountry) playerOneCountry_ = index;
            if (countries[index].id == saved.playerTwoCountry) playerTwoCountry_ = index;
        }
        const auto closestPreset = [](const auto& presets, float wanted) {
            std::size_t closest = 0;
            for (std::size_t index = 1; index < presets.size(); ++index)
                if (std::abs(presets[index].value - wanted) <
                    std::abs(presets[closest].value - wanted)) closest = index;
            return closest;
        };
        mapSize_ = closestPreset(mapSizes, static_cast<float>(saved.mapChunksPerSide));
        startingResources_ = closestPreset(resourceStarts, saved.startingResourcesScale);
        abundance_ = closestPreset(resourceAbundance, saved.resourceAbundanceScale);
        const auto& layouts = terrainDefinitions_.layouts();
        for (std::size_t index = 0; index < layouts.size(); ++index)
            if (layouts[index].id.value == saved.terrainLayout) terrainLayout_ = index;
    } catch (const std::exception& error) {
        context_.logger.warning("configuration",
            std::string{"Could not load match setup profile; using defaults: "} + error.what());
    }
    controller_.focus("match.player_country");
}

void MatchSetupState::beginTransition(StateRequest request) {
    if (transitionOut_ >= 0.0F) return;
    request_ = request;
    transitionOut_ = 0.0F;
}

UiDocument MatchSetupState::document(int width, int height) const {
    UiDocument ui;
    ui.modal("match.panel", {175, 45, 1105, 675}, {0.72F, 0.76F, 0.80F})
        .texture = "ui/menu_panel";
    ui.label("match.title", {260, 82, 572, 111}, Text::get("match_setup.title"), 2.8F);
    ui.label("match.mode", {260, 116, 572, 136}, Text::get("match_setup.mode_1v1"), 1.35F,
             {0.55F, 0.75F, 0.84F});
    ui.scrollList("match.title.divider", {260, 146, 1020, 148}).color = {0.12F, 0.54F, 0.68F};

    const auto section = [&ui](const std::string& id, float top, const std::string& text) {
        ui.label(id, {260, top, 572, top + 21}, text, 1.48F, {0.38F, 0.88F, 1.0F});
    };
    const auto selector = [&ui](const std::string& id, float top,
                                const std::string& label, const std::string& value) {
        ui.label(id + ".label", {260, top, 572, top + 17}, label, 1.08F,
                 {0.54F, 0.67F, 0.72F});
        UiElement& left = ui.button(id + ".left", {260, top + 18, 294, top + 54}, "<",
                                    {0.08F, 0.15F, 0.19F}, {0.12F, 0.55F, 0.68F});
        left.textScale = 1.55F;
        left.keyboardFocusable = false;
        left.texture = "ui/menu_button";
        UiElement& choice = ui.button(id, {300, top + 18, 532, top + 54}, value,
                                      {0.11F, 0.22F, 0.27F}, {0.16F, 0.66F, 0.78F});
        choice.textScale = 1.40F;
        choice.texture = "ui/menu_button";
        UiElement& right = ui.button(id + ".right", {538, top + 18, 572, top + 54}, ">",
                                     {0.08F, 0.15F, 0.19F}, {0.12F, 0.55F, 0.68F});
        right.textScale = 1.55F;
        right.keyboardFocusable = false;
        right.texture = "ui/menu_button";
    };
    const auto& countries = context_.definitions.countries();
    section("match.players.header", 160, Text::get("match_setup.section_players"));
    selector("match.player_country", 184, Text::get("match_setup.player_country"),
             Text::get(countries[playerOneCountry_].nameKey));
    selector("match.opponent_country", 244, Text::get("match_setup.opponent_country"),
             Text::get(countries[playerTwoCountry_].nameKey));
    const auto& layouts = terrainDefinitions_.layouts();
    section("match.world.header", 310, Text::get("match_setup.section_world"));
    selector("match.terrain_layout", 334, Text::get("match_setup.terrain_layout"),
             Text::get(layouts[terrainLayout_].nameKey));
    selector("match.map_size", 394, Text::get("match_setup.map_size"),
             Text::get(mapSizes[mapSize_].nameKey));
    section("match.economy.header", 450, Text::get("match_setup.section_economy"));
    selector("match.starting_resources", 474, Text::get("match_setup.starting_resources"),
             Text::get(resourceStarts[startingResources_].nameKey));
    selector("match.abundance", 532, Text::get("match_setup.resource_abundance"),
             Text::get(resourceAbundance[abundance_].nameKey));

    ui.label("match.options.header", {650, 160, 1015, 181},
             Text::get("match_setup.section_options"), 1.48F, {0.38F, 0.88F, 1.0F});
    ui.label("match.seed.label", {650, 186, 1015, 203}, Text::get("match_setup.seed"), 1.08F,
             {0.54F, 0.67F, 0.72F});
    ui.textField("match.seed", {650, 205, 805, 241}, seedText_).textScale = 1.42F;
    UiElement& randomSeed = ui.button("match.random_seed", {815, 205, 1015, 241},
                                      Text::get("match_setup.random_seed"),
                                      {0.10F, 0.22F, 0.27F}, {0.16F, 0.62F, 0.74F});
    randomSeed.textScale = 1.20F;
    randomSeed.icon = "resource_data";
    randomSeed.centerIconWithText = true;

    UiElement& summaryPanel = ui.panel("match.summary.panel", {640, 268, 1015, 485},
                                       {0.84F, 0.88F, 0.90F});
    summaryPanel.texture = "ui/hud_compact_lifted";
    ui.label("match.summary.header", {665, 284, 855, 305}, Text::get("match_setup.summary"),
             1.45F, {0.38F, 0.88F, 1.0F});
    const auto summaryRow = [&ui](const std::string& id, float top, const std::string& icon,
                                  const std::string& label, const std::string& value) {
        if (icon.starts_with("ui/")) {
            UiElement& emblem = ui.panel(id + ".icon", {665, top - 3, 693, top + 25},
                                          {1.0F, 1.0F, 1.0F});
            emblem.texture = icon;
        } else {
            UiElement& iconElement = ui.label(id + ".icon", {666, top - 1, 692, top + 25}, "");
            iconElement.icon = icon;
        }
        ui.label(id + ".value", {701, top, 875, top + 22}, label + "  " + value, 1.0F,
                 {0.94F, 0.97F, 0.98F});
    };
    UiElement& preview = ui.panel("match.summary.preview", {885, 306, 990, 424},
                                  {1.0F, 1.0F, 1.0F});
    preview.texture = "ui/match_preview_filler";
    for (const UiRect edge : {UiRect{882, 303, 993, 305}, UiRect{882, 425, 993, 427},
                              UiRect{882, 305, 884, 425}, UiRect{991, 305, 993, 425}})
        ui.scrollList("match.preview.border." + std::to_string(ui.elements().size()), edge)
            .color = {0.12F, 0.54F, 0.68F};
    ui.label("match.summary.preview.caption", {885, 431, 990, 450},
             Text::get("match_setup.tactical_overview"), 0.95F, {0.48F, 0.72F, 0.80F});
    summaryRow("match.summary.player", 312,
               "ui/country_emblem_" + countries[playerOneCountry_].id,
               Text::get("match_setup.summary_player"),
               Text::get(countries[playerOneCountry_].nameKey));
    summaryRow("match.summary.opponent", 343,
               "ui/country_emblem_" + countries[playerTwoCountry_].id,
               Text::get("match_setup.summary_opponent"),
               Text::get(countries[playerTwoCountry_].nameKey));
    summaryRow("match.summary.map", 374, "building_sensor_tower",
               Text::get("match_setup.summary_map_label"),
               Text::get(mapSizes[mapSize_].nameKey));
    summaryRow("match.summary.resources", 405, "resource_materials",
               Text::get("match_setup.summary_resources_label"),
               Text::get(resourceStarts[startingResources_].nameKey));
    summaryRow("match.summary.deposits", 436, "resource_scrap",
               Text::get("match_setup.summary_deposits_label"),
               Text::get(resourceAbundance[abundance_].nameKey));

    UiElement& start = ui.button("match.start", {655, 500, 1010, 552},
                                 Text::get("match_setup.start"),
                                 {0.14F, 0.40F, 0.22F}, {0.22F, 0.68F, 0.34F});
    start.textScale = 2.0F;
    start.icon = "action_move";
    start.centerIconWithText = true;
    UiElement& back = ui.button("match.back", {735, 568, 935, 608},
                                Text::get("match_setup.back"),
                                {0.09F, 0.14F, 0.17F}, {0.18F, 0.34F, 0.42F});
    back.textScale = 1.50F;
    back.icon = "action_stop";
    back.centerIconWithText = true;
    for (UiElement& element : ui.elements())
        if (element.kind == UiElementKind::button && element.texture.empty())
            element.texture = "ui/menu_button";
    ui.scaleFromReference(width, height, config_.uiScale);
    return ui;
}

void MatchSetupState::cycle(std::size_t& value, std::size_t count, int direction) {
    value = static_cast<std::size_t>((static_cast<int>(value) + direction +
        static_cast<int>(count)) % static_cast<int>(count));
}

void MatchSetupState::activate(std::string_view id, int direction) {
    if (id.ends_with(".left")) {
        id.remove_suffix(5);
        direction = -1;
    } else if (id.ends_with(".right")) {
        id.remove_suffix(6);
        direction = 1;
    }
    const std::size_t countries = context_.definitions.countries().size();
    bool changed = true;
    if (id == "match.player_country") cycle(playerOneCountry_, countries, direction);
    else if (id == "match.opponent_country") cycle(playerTwoCountry_, countries, direction);
    else if (id == "match.map_size") cycle(mapSize_, mapSizes.size(), direction);
    else if (id == "match.terrain_layout")
        cycle(terrainLayout_, terrainDefinitions_.layouts().size(), direction);
    else if (id == "match.starting_resources")
        cycle(startingResources_, resourceStarts.size(), direction);
    else if (id == "match.abundance") cycle(abundance_, resourceAbundance.size(), direction);
    else if (id == "match.random_seed") {
        std::random_device random;
        const std::uint32_t seed = (static_cast<std::uint32_t>(random()) << 16U) ^
                                   static_cast<std::uint32_t>(random());
        seedText_ = std::to_string(seed);
    } else if (id == "match.start") {
        persist();
        beginTransition(StateRequest::startGame);
        return;
    } else if (id == "match.back") {
        persist();
        beginTransition(StateRequest::returnToMainMenu);
        return;
    } else changed = false;
    if (changed) persist();
}

void MatchSetupState::handleEvent(const SDL_Event& event) {
    if (transitionOut_ >= 0.0F) return;
    int width = config_.resolutionWidth, height = config_.resolutionHeight;
    std::uint32_t windowId = 0;
    if (event.type == SDL_EVENT_MOUSE_MOTION) windowId = event.motion.windowID;
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
             event.type == SDL_EVENT_MOUSE_BUTTON_UP) windowId = event.button.windowID;
    else if (event.type == SDL_EVENT_KEY_DOWN) windowId = event.key.windowID;
    if (SDL_Window* window = SDL_GetWindowFromID(windowId)) SDL_GetWindowSize(window, &width, &height);
    UiDocument ui = document(width, height);
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        controller_.pointerMoved({event.motion.x, event.motion.y});
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        (void)controller_.press(ui, {event.button.x, event.button.y});
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
        if (const auto id = controller_.release(ui, {event.button.x, event.button.y})) {
            context_.events.enqueue(AudioEvent{AudioCue::uiClick});
            const UiElement* element = ui.find(*id);
            const float center = element ? (element->bounds.left + element->bounds.right) * 0.5F
                                         : width * 0.5F;
            activate(*id, event.button.x < center ? -1 : 1);
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (controller_.focusedId() == "match.seed") {
            bool changed = false;
            if (event.key.key == SDLK_BACKSPACE && !seedText_.empty()) {
                seedText_.pop_back();
                changed = true;
            } else if (event.key.key >= SDLK_0 && event.key.key <= SDLK_9 && seedText_.size() < 10) {
                seedText_.push_back(static_cast<char>('0' + event.key.key - SDLK_0));
                changed = true;
            } else if (event.key.key == SDLK_ESCAPE) {
                persist();
                beginTransition(StateRequest::returnToMainMenu);
            }
            if (changed) persist();
            if (event.key.key != SDLK_TAB) return;
        }
        if (event.key.key == SDLK_TAB)
            controller_.moveFocus(ui, (SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
        else if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
            if (const auto id = controller_.activateFocused(ui))
                activate(*id, event.key.key == SDLK_LEFT ? -1 : 1);
        } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
            if (const auto id = controller_.activateFocused(ui)) activate(*id);
        } else if (event.key.key == SDLK_ESCAPE) beginTransition(StateRequest::returnToMainMenu);
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
            beginTransition(StateRequest::returnToMainMenu);
    }
}

void MatchSetupState::update(float deltaSeconds) {
    menuTime_ += deltaSeconds;
    if (transitionOut_ >= 0.0F) transitionOut_ += deltaSeconds;
    controller_.advance(deltaSeconds);
}

void MatchSetupState::render(Renderer& renderer) const {
    renderer.drawMenuBackground(std::sin(menuTime_ * 0.10F));
    UiDocument ui = document(renderer.viewportWidth(), renderer.viewportHeight());
    controller_.apply(ui);
    const float entrance = std::clamp(menuTime_ / 0.40F, 0.0F, 1.0F);
    const float offset = (1.0F - entrance) * 30.0F;
    for (UiElement& element : ui.elements()) {
        element.bounds.top += offset;
        element.bounds.bottom += offset;
    }
    renderer.drawUi(ui);
    const float fadeIn = 1.0F - std::clamp(menuTime_ / 0.50F, 0.0F, 1.0F);
    const float fadeOut = transitionOut_ < 0.0F ? 0.0F : std::clamp(transitionOut_ / 0.24F, 0.0F, 1.0F);
    renderer.drawScreenFade(std::max(fadeIn, fadeOut));
}

StateRequest MatchSetupState::takeRequest() {
    if (request_ == StateRequest::none || transitionOut_ < 0.24F) return StateRequest::none;
    const StateRequest result = request_;
    request_ = StateRequest::none;
    return result;
}

MatchSetupOptions MatchSetupState::currentSetup() const {
    std::uint32_t seed = 0x5EED1234U;
    const auto parsed = std::from_chars(seedText_.data(), seedText_.data() + seedText_.size(), seed);
    if (parsed.ec != std::errc{}) seed = 0x5EED1234U;
    const auto& countries = context_.definitions.countries();
    const auto& layouts = terrainDefinitions_.layouts();
    return {seed, countries[playerOneCountry_].id, countries[playerTwoCountry_].id,
            static_cast<std::uint32_t>(mapSizes[mapSize_].value),
            resourceStarts[startingResources_].value,
            resourceAbundance[abundance_].value,
            layouts[terrainLayout_].id.value};
}

MatchSetupOptions MatchSetupState::matchSetup() const { return currentSetup(); }

void MatchSetupState::persist() {
    try {
        MatchSetupProfileStore::write(config_.matchSetupPath(), currentSetup());
    } catch (const std::exception& error) {
        context_.logger.error("configuration",
            std::string{"Could not persist match setup: "} + error.what());
    }
}

} // namespace strategy
