#include "game/PlayState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "diagnostics/Logger.hpp"
#include "localization/Text.hpp"
#include "ui/EntityHudModel.hpp"
#include "ui/EntityHudLayout.hpp"
#include "ui/GameHudLayout.hpp"
#include "persistence/SaveGame.hpp"
#include "render/Renderer.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <iostream>

namespace strategy {
namespace {

bool isHomogeneousSelection(const World& world,
                            const std::vector<EntityId>& selected,
                            const std::string& archetype) {
    if (selected.empty())
        return false;
    return std::all_of(selected.begin(), selected.end(), [&](EntityId id) {
        const Entity* entity = world.findEntity(id);
        return entity && entity->archetype.value == archetype;
    });
}

std::vector<EntityId> actionTargets(EntityId selectedEntity,
                                    const std::vector<EntityId>& selectedUnits) {
    return selectedUnits.empty() ? std::vector<EntityId>{selectedEntity} : selectedUnits;
}

bool canAffordForAll(const Player* player,
                     const RecipeDefinition* recipe,
                     std::size_t targetCount) {
    if (!player || !recipe)
        return false;
    for (const auto& [resource, amount] : recipe->cost) {
        const auto available = player->resources.find(resource);
        if (available == player->resources.end() ||
            available->second < amount * static_cast<float>(targetCount))
            return false;
    }
    return true;
}

void appendSyntheticRoutes(EntityHudModel& hud,
                           const World& world,
                           const DefinitionRegistry& definitions,
                           PlayerId player,
                           const std::vector<EntityId>& targets) {
    bool synthetic = false;
    for (EntityId id : targets) {
        const Entity* entity = world.findEntity(id);
        if (!entity || !entity->gatherer) continue;
        if (entity->gatherer.carriedResource == "synthetic") synthetic = true;
        if (const Entity* source = world.findEntity(entity->gatherer.sourceTarget);
            source && source->resource && source->resource.type == "synthetic") synthetic = true;
    }
    if (!synthetic) return;
    for (const std::string output : {std::string{"alloy"}, std::string{"fuel"}}) {
        float bestRatio = 0.0F;
        bool available = false;
        for (const Entity& processor : world.entities()) {
            if (processor.authority.owner != player || !processor.processor ||
                !isOperational(processor)) continue;
            const auto* conversion = definitions.conversionFor(
                BuildingArchetypeId{processor.archetype.value}, ResourceId{"synthetic"});
            if (conversion && conversion->output.value == output) {
                available = true;
                bestRatio = std::max(bestRatio, conversion->outputPerInput);
            }
        }
        const ResourceDefinition* resource = definitions.resourceType(ResourceId{output});
        HudActionModel action;
        action.id = "delivery-output:" + output;
        action.icon = resource ? resource->icon : "status_asset_failed";
        action.name = Text::get(output == "alloy" ? "processor.route.synthetic_alloy"
                                                   : "processor.route.synthetic_fuel");
        action.description = "x" + std::to_string(bestRatio);
        action.cost = Text::get("entity_hud.free");
        action.enabled = available;
        if (!available) action.disabledReason = Text::get("entity_hud.no_compatible_processor");
        hud.actions.push_back(std::move(action));
    }
}

} // namespace

void PlayState::setMouseCaptured(bool captured) {
    if (inputWindowId_ == 0)
        return;
    if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_)) {
        SDL_SetWindowRelativeMouseMode(window, captured);
    }
}

PlayState::PlayState(StateContext& context, MatchSetupOptions setup)
    : GameState(context)
    , session_(context.definitions,
               setup.terrainSeed,
               std::move(setup.playerOneCountry),
               std::move(setup.playerTwoCountry),
               "unassigned",
               "unassigned",
               setup.mapChunksPerSide,
               setup.startingResourcesScale,
               setup.resourceAbundanceScale,
               TerrainLayoutId{std::move(setup.terrainLayout)})
    , config_(GameConfig::load(context.configPath)) {
    initializeStartingView();
}

PlayState::PlayState(StateContext& context, MatchSetupOptions setup,
                     GameSession preparedSession)
    : GameState(context)
    , session_(std::move(preparedSession))
    , config_(GameConfig::load(context.configPath)) {
    (void)setup;
    initializeStartingView();
}

void PlayState::initializeStartingView() {
    for (const Entity& entity : session_.world().entities()) {
        if (entity.authority.owner != localPlayer_) continue;
        if (entity.archetype.value == "command_hub")
            camera_.focusAt(entity.transform.position);
        if (entity.archetype.value == "construction_drone")
            selectedEntity_ = entity.id;
    }
}

PlayState::PlayState(StateContext& context, SaveData data)
    : GameState(context)
    , session_(context.definitions,
               data.terrainSeed,
               data.playerOneCountry,
               data.playerTwoCountry,
               data.playerOneSpecialization,
               data.playerTwoSpecialization,
               data.mapChunksPerSide, 1.0F, 1.0F,
               TerrainLayoutId{data.terrainLayout})
    , config_(GameConfig::load(context.configPath)) {
    session_.replaceWorld(std::move(data.entities), data.terrainSeed,
                          std::move(data.foundations), data.mapChunksPerSide,
                          TerrainLayoutId{data.terrainLayout});
    session_.restorePlayerProgress(1,
                                   std::move(data.resources[0]),
                                   std::move(data.discovered[0]),
                                   std::move(data.intelligence[0]));
    session_.restorePlayerProgress(2,
                                   std::move(data.resources[1]),
                                   std::move(data.discovered[1]),
                                   std::move(data.intelligence[1]));
}

EntityHudModel PlayState::buildConstructionHudModel(const Entity& selected,
                                                    const Player* player) const {
    EntityHudModel hud = EntityHudModelBuilder::build(
        session_.world(), selected.id, selectedUnits_, context_.definitions);
    appendSyntheticRoutes(hud, session_.world(), context_.definitions, localPlayer_,
                          actionTargets(selected.id, selectedUnits_));
    for (const std::string& building : context_.definitions.matchRules().buildPalette) {
        const RecipeDefinition* recipe =
            context_.definitions.recipe(RecipeId{"construct." + building});
        if (!recipe || recipe->product.kind != RecipeProductKind::building) continue;
        const EntityArchetype* product = context_.definitions.archetype(recipe->product.id);
        HudActionModel action;
        action.id = recipe->id;
        action.icon = product ? context_.definitions.presentationIcon(
                                    PresentationId{product->presentation})
                              : "status_asset_failed";
        action.name = product ? Text::get(product->nameKey) : recipe->product.id;
        for (const auto& [resource, amount] : recipe->cost)
            action.cost += resource + ": " + std::to_string(static_cast<int>(amount)) + "  ";
        action.description = Text::format(
            "entity_hud.drone_power",
            {std::to_string(static_cast<int>(recipe->constructionPower))});
        action.enabled = player && std::all_of(
            recipe->cost.begin(), recipe->cost.end(), [&](const auto& cost) {
                const auto found = player->resources.find(cost.first);
                return found != player->resources.end() && found->second >= cost.second;
            });
        if (!action.enabled)
            action.disabledReason = Text::get("entity_hud.insufficient_resources");
        action.active = constructionPlacementMode_ && recipe->id == constructionRecipeId_;
        hud.actions.push_back(std::move(action));
    }
    return hud;
}

EntityHudModel PlayState::buildEntityActionHudModel(const Entity& selected,
                                                    const Player* player) const {
    EntityHudModel hud = EntityHudModelBuilder::build(
        session_.world(), selected.id, selectedUnits_, context_.definitions);
    const std::vector<EntityId> targets = actionTargets(selected.id, selectedUnits_);
    for (const RecipeDefinition* recipe :
         context_.definitions.recipesForProducer(selected.archetype.value)) {
        const EntityArchetype* product = context_.definitions.archetype(recipe->product.id);
        HudActionModel action;
        action.id = recipe->id;
        action.name = product ? Text::get(product->nameKey) : recipe->product.id;
        if (product)
            action.icon = context_.definitions.presentationIcon(
                PresentationId{product->presentation});
        else if (recipe->product.kind == RecipeProductKind::resource)
            action.icon = "resource_" + recipe->product.id;
        else
            action.icon = "status_asset_failed";
        for (const auto& [resource, amount] : recipe->cost)
            action.cost += resource + ": " + std::to_string(static_cast<int>(amount)) + " ";
        if (player)
            action.description = Text::format(
                "entity_hud.time_seconds",
                {std::to_string(static_cast<int>(context_.definitions.productionDuration(
                    player->countryId, player->specializationId, selected.archetype.value,
                    recipe->product.id)))});
        if (action.cost.empty()) action.cost = Text::get("entity_hud.free");
        action.enabled = canAffordForAll(player, recipe, targets.size()) &&
                         std::all_of(targets.begin(), targets.end(), [&](EntityId id) {
                             return session_.canStartRecipe(localPlayer_, id,
                                                            RecipeId{recipe->id});
                         });
        if (!action.enabled) action.disabledReason = Text::get("entity_hud.action_unavailable");
        hud.actions.push_back(std::move(action));
    }
    for (const UpgradeDefinition* upgrade :
         context_.definitions.upgradesForResearcher(selected.archetype.value)) {
        HudActionModel action;
        action.id = upgrade->id;
        action.name = Text::get(upgrade->nameKey);
        action.icon = upgrade->icon;
        const RecipeDefinition* research =
            context_.definitions.recipe(RecipeId{upgrade->researchRecipe});
        if (research)
            for (const auto& [resource, amount] : research->cost)
                action.cost += resource + ": " + std::to_string(static_cast<int>(amount)) + " ";
        if (research)
            action.description = Text::format(
                "entity_hud.time_seconds",
                {std::to_string(static_cast<int>(
                    research->durationTicks * GameSession::fixedTickSeconds))});
        if (action.cost.empty()) action.cost = Text::get("entity_hud.free");
        action.enabled = canAffordForAll(player, research, targets.size()) &&
                         std::all_of(targets.begin(), targets.end(), [&](EntityId id) {
                             return session_.canStartUpgrade(localPlayer_, id, upgrade->id);
                         });
        if (!action.enabled) action.disabledReason = Text::get("entity_hud.action_unavailable");
        hud.actions.push_back(std::move(action));
    }
    if (selected.power) {
        HudActionModel connect;
        connect.id = "power.connect";
        connect.name = Text::get("power.action.connect");
        connect.description = Text::get("power.action.connect.description");
        connect.icon = "action_power_connect";
        connect.cost = Text::get("entity_hud.free");
        connect.enabled = targets.size() == 1 && isOperational(selected) && selected.power.enabled &&
                          selected.power.connections.size() < selected.power.maximumConnections;
        if (!connect.enabled) connect.disabledReason = Text::get("entity_hud.action_unavailable");
        hud.actions.push_back(std::move(connect));

        HudActionModel disconnect;
        disconnect.id = "power.disconnect";
        disconnect.name = Text::get("power.action.disconnect");
        disconnect.description = Text::get("power.action.disconnect.description");
        disconnect.icon = "action_power_disconnect";
        disconnect.cost = Text::get("entity_hud.free");
        disconnect.enabled = targets.size() == 1 && !selected.power.connections.empty();
        if (!disconnect.enabled) disconnect.disabledReason = Text::get("entity_hud.action_unavailable");
        hud.actions.push_back(std::move(disconnect));

        HudActionModel priority;
        priority.id = "power.priority";
        priority.name = Text::get("power.action.priority");
        priority.description = Text::format("power.action.priority.description",
                                            {std::to_string(selected.power.priority)});
        priority.icon = "action_power_priority";
        priority.cost = Text::get("entity_hud.free");
        priority.enabled = true;
        hud.actions.push_back(std::move(priority));

        HudActionModel toggle;
        toggle.id = "power.toggle";
        toggle.name = selected.power.enabled ? Text::get("power.action.disable")
                                             : Text::get("power.action.enable");
        toggle.description = Text::get("power.action.toggle.description");
        toggle.icon = selected.power.enabled ? "status_powered" : "status_unpowered";
        toggle.cost = Text::get("entity_hud.free");
        toggle.enabled = true;
        hud.actions.push_back(std::move(toggle));
    }
    appendSyntheticRoutes(hud, session_.world(), context_.definitions, localPlayer_, targets);
    return hud;
}

std::optional<glm::vec2> PlayState::minimapWorldAt(float screenX, float screenY) const {
    int width = config_.resolutionWidth;
    int height = config_.resolutionHeight;
    if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_))
        SDL_GetWindowSize(window, &width, &height);

    const UiDocument minimap = GameHudLayout::minimap(width, height, config_.uiScale);
    const UiElement* map = minimap.find("strategy.minimap");
    if (!map || !map->bounds.contains({screenX, screenY}))
        return std::nullopt;

    // These insets match the title and map-content area drawn by drawStrategyHud.
    const float left = map->bounds.left + 8.0F;
    const float right = map->bounds.right - 8.0F;
    const float top = map->bounds.top + 28.0F;
    const float bottom = map->bounds.bottom - 8.0F;
    const float normalizedX = std::clamp((screenX - left) / (right - left), 0.0F, 1.0F);
    const float normalizedZ = std::clamp((screenY - top) / (bottom - top), 0.0F, 1.0F);
    return MapArea{session_.mapChunksPerSide()}.worldFromNormalized(
        {normalizedX, normalizedZ});
}

void PlayState::handleEvent(const SDL_Event& event) {
    const auto bound = [this](const char* action, SDL_Keycode fallback) {
        const auto found = config_.keybinds.find(action);
        return found == config_.keybinds.end() ? fallback : static_cast<SDL_Keycode>(found->second);
    };
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        inputWindowId_ = event.key.windowID;
    else if (event.type == SDL_EVENT_MOUSE_MOTION)
        inputWindowId_ = event.motion.windowID;
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        inputWindowId_ = event.button.windowID;
    else if (event.type == SDL_EVENT_MOUSE_WHEEL)
        inputWindowId_ = event.wheel.windowID;
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.key == bound("debug", SDLK_F3)) {
        detailedDebug_ = !detailedDebug_;
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.key == bound("terrain_debug", SDLK_F4)) {
        terrainDebug_ = !terrainDebug_;
        if (terrainDebug_) waterDebugMode_ = 0;
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.key == bound("water_debug", SDLK_F6)) {
        waterDebugMode_ = (waterDebugMode_ + 1) % 4;
        if (waterDebugMode_ != 0) terrainDebug_ = false;
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F5) {
        try {
            SaveGame::write(
                config_.savePath(), session_.terrainSeed(), session_.world(), &session_.players(),
                session_.mapChunksPerSide(), session_.terrainLayout().value);
            context_.logger.info("persistence",
                                 "Saved game to " + config_.savePath().string());
            context_.events.enqueue(AudioEvent{AudioCue::saveGame});
        } catch (const std::exception& error) {
            context_.logger.error("persistence", error.what());
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F9) {
        try {
            SaveData data = SaveGame::read(config_.savePath());
            pendingTerrainSeed_ = data.terrainSeed;
            pendingTerrainChunksPerSide_ = data.mapChunksPerSide;
            pendingTerrainLayout_ = TerrainLayoutId{data.terrainLayout};
            session_.replaceWorld(std::move(data.entities), data.terrainSeed,
                                  std::move(data.foundations), data.mapChunksPerSide,
                                  TerrainLayoutId{data.terrainLayout});
            possessedEntity_ = 0;
            viewMode_ = ViewMode::strategy;
            setMouseCaptured(false);
            context_.logger.info("persistence",
                                 "Loaded game from " + config_.savePath().string());
            context_.events.enqueue(AudioEvent{AudioCue::loadGame});
        } catch (const std::exception& error) {
            context_.logger.error("persistence", error.what());
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_DELETE) {
        if (Entity* selected = session_.world().findEntity(selectedEntity_);
            selected && selected->authority.owner == localPlayer_ && selected->construction &&
            !isOperational(*selected)) {
            session_.submit({localPlayer_, nextCommandSequence_++,
                             CancelConstructionCommand{selected->id}});
            selectedEntity_ = 0;
            selectedUnits_.clear();
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_P) {
        if (possessedEntity_ != 0) {
            session_.submit(
                {localPlayer_, nextCommandSequence_++, ReleaseUnitCommand{possessedEntity_}});
            possessedEntity_ = 0;
            viewMode_ = ViewMode::strategy;
            setMouseCaptured(false);
        } else {
            for (const Entity& entity : session_.world().entities()) {
                if (entity.authority.owner == localPlayer_ && entity.unitControl &&
                    entity.unitControl.directlyControllable) {
                    possessedEntity_ = entity.id;
                    session_.submit(
                        {localPlayer_, nextCommandSequence_++, PossessUnitCommand{entity.id}});
                    viewMode_ = ViewMode::unitControl;
                    setMouseCaptured(true);
                    context_.events.enqueue(AudioEvent{AudioCue::possessUnit});
                    break;
                }
            }
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == bound("pause", SDLK_ESCAPE) &&
        !event.key.repeat) {
        if (powerLinkMode_ != PowerLinkMode::none) {
            powerLinkMode_ = PowerLinkMode::none;
            powerLinkSource_ = 0;
            return;
        }
        if (constructionPlacementMode_) {
            constructionPlacementMode_ = false;
            pendingConstructionScreen_.reset();
            pendingConstructionPosition_.reset();
            return;
        }
        if (viewMode_ == ViewMode::unitControl && possessedEntity_ != 0) {
            if (const Entity* controlled = session_.world().findEntity(possessedEntity_))
                camera_.focusAt(controlled->transform.position);
            session_.submit(
                {localPlayer_, nextCommandSequence_++, ReleaseUnitCommand{possessedEntity_}});
            possessedEntity_ = 0;
            viewMode_ = ViewMode::strategy;
            orbiting_ = false;
            mousePanning_ = false;
            setMouseCaptured(false);
            return;
        }
        paused_ = !paused_;
        orbiting_ = false;
        mousePanning_ = false;
        return;
    }
    if (paused_) {
        handlePauseEvent(event);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_LEFT && viewMode_ == ViewMode::strategy) {
        if (const auto world = minimapWorldAt(event.button.x, event.button.y)) {
            camera_.focusAt({world->x, camera_.focus().y, world->y});
            draggingSelection_ = false;
            return;
        }
    }
    const auto hudResources = context_.definitions.enabledResources();
    const std::size_t localResourceCount = std::count_if(
        hudResources.begin(), hudResources.end(), [](const ResourceDefinition* resource) {
            return resource->storage == ResourceStorageKind::stockpile;
        });
    const std::size_t powerDeviceCount = std::count_if(
        session_.world().entities().begin(), session_.world().entities().end(),
        [&](const Entity& entity) {
            const EntityArchetype* archetype = context_.definitions.archetype(entity.archetype);
            return entity.authority.owner == localPlayer_ && archetype && archetype->powerDevice;
        });
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        int width = 0, height = 0;
        if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_))
            SDL_GetWindowSize(window, &width, &height);
        UiDocument resourceUi = GameHudLayout::resources(
            localResourceCount, powerDeviceCount, powerOverlayVisible_, satelliteRevealActive_,
            width, height,
            config_.uiScale);
        const std::optional<std::string> hudAction =
            uiController_.press(resourceUi, {event.button.x, event.button.y});
        if (hudAction == "hud.menu") {
            paused_ = true;
            forward_ = backward_ = left_ = right_ = running_ = false;
            orbiting_ = false;
            mousePanning_ = false;
            return;
        }
        if (hudAction == "resources.power") {
            powerOverlayVisible_ = !powerOverlayVisible_;
            return;
        }
        if (hudAction == "hud.satellite" && satelliteImageryAvailable_) {
            satelliteRevealActive_ = !satelliteRevealActive_;
            return;
        }
        if (const UiElement* panel = resourceUi.find("power.panel");
            panel && panel->bounds.contains({event.button.x, event.button.y}))
            return;
        if (const UiElement* panel = resourceUi.find("resources.panel");
            panel && panel->bounds.contains({event.button.x, event.button.y}))
            return;
        if (const UiElement* panel = resourceUi.find("hud.top_bar");
            panel && panel->bounds.contains({event.button.x, event.button.y}))
            return;
    }
    const Entity* droneForBuildHud = session_.world().findEntity(selectedEntity_);
    const bool homogeneousSelection =
        droneForBuildHud && isHomogeneousSelection(session_.world(), selectedUnits_,
                                                   droneForBuildHud->archetype.value);
    const bool buildHudVisible = viewMode_ == ViewMode::strategy && droneForBuildHud &&
                                 (selectedUnits_.empty() || homogeneousSelection) &&
                                 droneForBuildHud->authority.owner == localPlayer_ &&
                                 droneForBuildHud->archetype.value == "construction_drone";
    if (event.type == SDL_EVENT_MOUSE_MOTION)
        uiController_.pointerMoved({event.motion.x, event.motion.y});
    if (buildHudVisible && (event.type == SDL_EVENT_MOUSE_MOTION ||
                            (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                             event.button.button == SDL_BUTTON_LEFT))) {
        int windowHeight = 0, windowWidth = 0;
        if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_)) SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        const Player* player = session_.players().find(localPlayer_);
        EntityHudModel hud = buildConstructionHudModel(*droneForBuildHud, player);
        UiDocument layout = EntityHudLayout::actions(
            hud, windowWidth, windowHeight, config_.uiScale);
        const glm::vec2 point = event.type == SDL_EVENT_MOUSE_MOTION
            ? glm::vec2{event.motion.x, event.motion.y}
            : glm::vec2{event.button.x, event.button.y};
        uiController_.apply(layout);
        if (event.type == SDL_EVENT_MOUSE_MOTION)
            constructionCursorScreen_ = point;
        else if (const auto activated = uiController_.press(layout, point)) {
            if (const auto actionId = EntityHudLayout::actionId(*activated)) {
                if (actionId->starts_with("delivery-output:")) {
                    const std::string output = actionId->substr(std::string{"delivery-output:"}.size());
                    for (EntityId id : actionTargets(selectedEntity_, selectedUnits_))
                        session_.submit({localPlayer_, nextCommandSequence_++,
                                         SetDeliveryOutputCommand{id, output}});
                    return;
                }
                constructionRecipeId_ = *actionId;
                constructionPlacementMode_ = true;
                return;
            }
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && layout.find("entity.panel") &&
            layout.find("entity.panel")->bounds.contains(point)) return;
    }
    // Right-click always cancels an active placement preview, including invalid (red) previews.
    if (powerLinkMode_ != PowerLinkMode::none &&
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_RIGHT) {
        powerLinkMode_ = PowerLinkMode::none;
        powerLinkSource_ = 0;
        return;
    }
    if (constructionPlacementMode_ && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_RIGHT) {
        constructionPlacementMode_ = false;
        pendingConstructionScreen_.reset();
        pendingConstructionPosition_.reset();
        constructionCursorScreen_.reset();
        return;
    }
    if (constructionPlacementMode_ && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        pendingConstructionScreen_ = glm::vec2{event.button.x, event.button.y};
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
        viewMode_ == ViewMode::strategy && !selectedUnits_.empty() &&
        !homogeneousSelection) {
        int windowHeight = 0, width = 0;
        if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_))
            SDL_GetWindowSize(window, &width, &windowHeight);
        const EntityHudModel hud = EntityHudModelBuilder::build(
            session_.world(), selectedEntity_, selectedUnits_, context_.definitions);
        UiDocument layout = EntityHudLayout::selection(
            hud, width, windowHeight, config_.uiScale);
        const glm::vec2 point{event.button.x, event.button.y};
        const auto activated = uiController_.press(layout, point);
        if (activated) {
            const auto chosen = EntityHudLayout::selectionArchetype(*activated);
            if (!chosen) return;
            const bool removeType = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            selectedUnits_.erase(
                std::remove_if(selectedUnits_.begin(),
                               selectedUnits_.end(),
                               [&](EntityId id) {
                                   const Entity* entity = session_.world().findEntity(id);
                                   return !entity || (removeType ? entity->archetype.value == *chosen
                                                                 : entity->archetype.value != *chosen);
                               }),
                selectedUnits_.end());
            selectedEntity_ = selectedUnits_.empty() ? 0 : selectedUnits_.front();
            if (selectedUnits_.size() == 1) {
                selectedUnits_.clear();
            }
            lastWorldClickEntity_ = 0;
            return;
        }
        if (layout.find("selection.panel")->bounds.contains(point)) return;
    }
    const Entity* selectedHall = session_.world().findEntity(selectedEntity_);
    const bool ownsSelectedHall = selectedHall && selectedHall->authority.owner == localPlayer_ &&
                                  (selectedUnits_.empty() || homogeneousSelection);
    if (ownsSelectedHall &&
        (event.type == SDL_EVENT_MOUSE_MOTION ||
         (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT))) {
        const Player* local = session_.players().find(localPlayer_);
        int width = 0, height = 0;
        if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_))
            SDL_GetWindowSize(window, &width, &height);
        EntityHudModel hud = buildEntityActionHudModel(*selectedHall, local);
        UiDocument layout = hud.actions.empty() && hud.queue.empty()
            ? EntityHudLayout::directControl(hud, width, height, config_.uiScale)
            : EntityHudLayout::actions(hud, width, height, config_.uiScale);
        const glm::vec2 point = event.type == SDL_EVENT_MOUSE_MOTION
            ? glm::vec2{event.motion.x, event.motion.y}
            : glm::vec2{event.button.x, event.button.y};
        uiController_.apply(layout);
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            const auto activated = uiController_.press(layout, point);
            if (!activated && layout.find("entity.panel") &&
                layout.find("entity.panel")->bounds.contains(point)) {
                // Disabled controls still consume the click instead of selecting the world below.
                return;
            } else if (activated && EntityHudLayout::queueIndex(*activated)) {
                const std::size_t queueIndex = *EntityHudLayout::queueIndex(*activated);
                session_.submit({localPlayer_, nextCommandSequence_++,
                                 CancelProductionCommand{
                                     selectedHall->id,
                                     static_cast<std::uint32_t>(queueIndex)}});
                return;
            } else if (activated) {
                const auto actionId = EntityHudLayout::actionId(*activated);
                if (!actionId) return;
                const std::vector<EntityId> targets = actionTargets(selectedEntity_, selectedUnits_);
                if (*actionId == "power.connect" || *actionId == "power.disconnect") {
                    powerLinkMode_ = *actionId == "power.connect" ? PowerLinkMode::connect
                                                                  : PowerLinkMode::disconnect;
                    powerLinkSource_ = selectedHall->id;
                    powerOverlayVisible_ = true;
                    return;
                }
                if (*actionId == "power.priority") {
                    const int nextPriority = selectedHall->power && selectedHall->power.priority >= 100
                                                 ? 10
                                                 : selectedHall->power->priority + 10;
                    for (EntityId id : targets)
                        session_.submit({localPlayer_, nextCommandSequence_++,
                                         SetPowerPriorityCommand{id, nextPriority}});
                    return;
                }
                if (*actionId == "power.toggle") {
                    const bool enabled = selectedHall->power && !selectedHall->power->enabled;
                    for (EntityId id : targets)
                        session_.submit({localPlayer_, nextCommandSequence_++,
                                         SetPowerEnabledCommand{id, enabled}});
                    return;
                }
                if (actionId->starts_with("delivery-output:")) {
                    const std::string output = actionId->substr(std::string{"delivery-output:"}.size());
                    for (EntityId id : targets)
                        session_.submit({localPlayer_, nextCommandSequence_++,
                                         SetDeliveryOutputCommand{id, output}});
                    return;
                }
                if (const RecipeDefinition* recipe =
                        context_.definitions.recipe(RecipeId{*actionId})) {
                    for (EntityId id : targets)
                        session_.submit({localPlayer_, nextCommandSequence_++,
                                         StartRecipeCommand{id, recipe->id}});
                    context_.events.enqueue(AudioEvent{AudioCue::trainUnit});
                } else if (const UpgradeDefinition* upgrade =
                               context_.definitions.upgrade(*actionId)) {
                    const auto submitUpgrade = [&](EntityId id) {
                        session_.submit({localPlayer_, nextCommandSequence_++,
                                         StartUpgradeCommand{id, upgrade->id}});
                    };
                    for (EntityId id : targets) submitUpgrade(id);
                }
                return;
            }
        }
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
        viewMode_ == ViewMode::strategy) {
        draggingSelection_ = true;
        selectionStart_ = {event.button.x, event.button.y};
        selectionEnd_ = selectionStart_;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT &&
        viewMode_ == ViewMode::strategy && draggingSelection_) {
        draggingSelection_ = false;
        selectionEnd_ = {event.button.x, event.button.y};
        if (glm::length(selectionEnd_ - selectionStart_) >= 6.0F)
            pendingSelectionRectangle_ = glm::vec4{selectionStart_, selectionEnd_};
        else {
            pendingSelection_ = selectionEnd_;
            pendingSelectionAdditive_ = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            pendingSelectionDoubleClick_ = event.button.clicks >= 2;
        }
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_MIDDLE &&
        viewMode_ == ViewMode::strategy) {
        orbiting_ = true;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_MIDDLE &&
        viewMode_ == ViewMode::strategy) {
        orbiting_ = false;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT &&
        viewMode_ == ViewMode::strategy) {
        if (const auto world = minimapWorldAt(event.button.x, event.button.y)) {
            pendingOrderTarget_ = 0;
            pendingMoveDestination_ = glm::vec3{world->x, 0.0F, world->y};
            return;
        }
        mousePanning_ = true;
        rightDragDistance_ = 0.0F;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT &&
        viewMode_ == ViewMode::strategy) {
        mousePanning_ = false;
        if (rightDragDistance_ < 4.0F)
            pendingMoveScreen_ = glm::vec2{event.button.x, event.button.y};
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && viewMode_ == ViewMode::unitControl) {
        thirdPersonCamera_.orbit(event.motion.xrel, event.motion.yrel);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && orbiting_) {
        camera_.orbit(event.motion.xrel, event.motion.yrel);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && mousePanning_ && viewMode_ == ViewMode::strategy) {
        rightDragDistance_ += std::abs(event.motion.xrel) + std::abs(event.motion.yrel);
        camera_.dragPan(event.motion.xrel, event.motion.yrel);
        camera_.orbit(event.motion.xrel, event.motion.yrel);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && draggingSelection_ &&
        viewMode_ == ViewMode::strategy) {
        selectionEnd_ = {event.motion.x, event.motion.y};
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && viewMode_ == ViewMode::strategy) {
        pointerScreen_ = glm::vec2{event.motion.x, event.motion.y};
        hoverPosition_ = pointerScreen_;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        if (viewMode_ == ViewMode::unitControl)
            thirdPersonCamera_.zoom(event.wheel.y);
        else
            camera_.zoom(event.wheel.y);
        return;
    }
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
        return;
    }

    const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
    if (event.key.key == bound("forward", SDLK_W))
        forward_ = pressed;
    else if (event.key.key == bound("backward", SDLK_S))
        backward_ = pressed;
    else if (event.key.key == bound("left", SDLK_A))
        left_ = pressed;
    else if (event.key.key == bound("right", SDLK_D))
        right_ = pressed;
    else
        switch (event.key.key) {
        case SDLK_LSHIFT:
            leftShift_ = pressed;
            running_ = leftShift_ || rightShift_;
            break;
        case SDLK_RSHIFT:
            rightShift_ = pressed;
            running_ = leftShift_ || rightShift_;
            break;
        default:
            break;
        }
}

UiDocument PlayState::pauseUi(int width, int height) const {
    UiDocument document;
    document.modal("pause.panel", {30.0F, 70.0F, 335.0F, 505.0F},
                   {0.035F, 0.055F, 0.075F});
    document.label("pause.title", {91.0F, 106.0F, 0.0F, 0.0F},
                   Text::get("pause.title"), 4.0F);
    document.button("pause.resume", {60.0F, 250.0F, 300.0F, 310.0F},
                    Text::get("pause.resume"), {0.16F, 0.36F, 0.18F},
                    {0.28F, 0.62F, 0.24F}).textScale = 3.0F;
    document.button("pause.settings", {60.0F, 330.0F, 300.0F, 390.0F},
                    Text::get("menu.settings"), {0.14F, 0.25F, 0.36F},
                    {0.30F, 0.48F, 0.65F}).textScale = 3.0F;
    document.button("pause.exit", {60.0F, 410.0F, 300.0F, 470.0F},
                    Text::get("menu.exit"), {0.40F, 0.16F, 0.14F},
                    {0.72F, 0.25F, 0.20F}).textScale = 3.0F;
    document.scaleFromReference(width, height, config_.uiScale);
    return document;
}

void PlayState::handlePauseEvent(const SDL_Event& event) {
    const auto activate = [&](std::string_view id) {
        if (id == "pause.resume") paused_ = false;
        else if (id == "pause.settings") request_ = StateRequest::openSettings;
        else if (id == "pause.exit") request_ = StateRequest::returnToMainMenu;
    };
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        uiController_.pointerMoved({event.motion.x, event.motion.y});
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        int width = 1280, height = 720;
        if (SDL_Window* window = SDL_GetWindowFromID(event.button.windowID))
            SDL_GetWindowSize(window, &width, &height);
        UiDocument document = pauseUi(width, height);
        const auto activated = uiController_.press(
            document, {event.button.x, event.button.y});
        if (activated) activate(*activated);
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_TAB) {
        UiDocument document = pauseUi(config_.resolutionWidth, config_.resolutionHeight);
        uiController_.moveFocus(document,
            (SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN &&
        (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE)) {
        UiDocument document = pauseUi(config_.resolutionWidth, config_.resolutionHeight);
        if (const auto id = uiController_.activateFocused(document)) activate(*id);
        return;
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        UiDocument document = pauseUi(config_.resolutionWidth, config_.resolutionHeight);
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
            uiController_.moveFocus(document, 1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
            uiController_.moveFocus(document, -1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            if (const auto id = uiController_.activateFocused(document)) activate(*id);
        } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            paused_ = false;
    }
}

void PlayState::update(float deltaSeconds) {
    for (HudAlert& alert : hudAlerts_) alert.remaining -= deltaSeconds;
    std::erase_if(hudAlerts_, [](const HudAlert& alert) { return alert.remaining <= 0.0F; });
    uiController_.advance(deltaSeconds);
    if (paused_) {
        return;
    }
    if (pickedEntity_) {
        const EntityId clicked = *pickedEntity_;
        pickedEntity_.reset();
        if (powerLinkMode_ != PowerLinkMode::none) {
            if (clicked != 0 && clicked != powerLinkSource_) {
                if (powerLinkMode_ == PowerLinkMode::connect)
                    session_.submit({localPlayer_, nextCommandSequence_++,
                                     ConnectPowerCommand{powerLinkSource_, clicked}});
                else
                    session_.submit({localPlayer_, nextCommandSequence_++,
                                     DisconnectPowerCommand{powerLinkSource_, clicked}});
            }
            powerLinkMode_ = PowerLinkMode::none;
            powerLinkSource_ = 0;
            pickedEntityAdditive_ = false;
            pickedEntityDoubleClick_ = false;
        } else if (pickedEntityAdditive_) {
            if (clicked == 0) {
                pickedEntityAdditive_ = false;
                pickedEntityDoubleClick_ = false;
                return;
            }
            if (selectedUnits_.empty() && selectedEntity_ != 0)
                selectedUnits_.push_back(selectedEntity_);
            auto found = std::find(selectedUnits_.begin(), selectedUnits_.end(), clicked);
            if (found == selectedUnits_.end())
                selectedUnits_.push_back(clicked);
            else
                selectedUnits_.erase(found);
            selectedEntity_ = selectedUnits_.empty() ? 0 : selectedUnits_.front();
            pickedEntityAdditive_ = false;
        } else {
            const bool confirmedDoubleClick =
                clicked != 0 && pickedEntityDoubleClick_ && clicked == lastWorldClickEntity_;
            selectedEntity_ = clicked;
            selectedUnits_.clear();
            if (clicked != 0)
                context_.events.enqueue(AudioEvent{
                    session_.world().findEntity(clicked) &&
                            session_.world().findEntity(clicked)->production
                        ? AudioCue::buildingOpen
                        : AudioCue::selection});
            if (const Entity* selected = session_.world().findEntity(selectedEntity_);
                confirmedDoubleClick && selected && selected->authority.owner == localPlayer_ &&
                selected->unitControl && selected->unitControl.directlyControllable) {
                possessedEntity_ = selectedEntity_;
                session_.submit(
                    {localPlayer_, nextCommandSequence_++, PossessUnitCommand{possessedEntity_}});
                viewMode_ = ViewMode::unitControl;
                hoveredEntity_ = 0;
                setMouseCaptured(true);
            }
            lastWorldClickEntity_ = clicked;
        }
        pickedEntityDoubleClick_ = false;
    }
    if (pickedEntities_) {
        selectedUnits_ = std::move(*pickedEntities_);
        pickedEntities_.reset();
        selectedEntity_ = selectedUnits_.empty() ? 0 : selectedUnits_.front();
        if (selectedUnits_.size() == 1)
            selectedUnits_.clear();
        lastWorldClickEntity_ = 0;
    }
    const Entity* selectionContext = session_.world().findEntity(selectedEntity_);
    const bool homogeneousDrones = selectionContext &&
        isHomogeneousSelection(session_.world(), selectedUnits_, "construction_drone");
    if (!selectionContext || selectionContext->archetype.value != "construction_drone" ||
        (!selectedUnits_.empty() && !homogeneousDrones)) {
        constructionPlacementMode_ = false;
        pendingConstructionScreen_.reset();
        pendingConstructionPosition_.reset();
    }
    if (pendingMoveDestination_) {
        const glm::vec3 destination = *pendingMoveDestination_;
        pendingMoveDestination_.reset();
        const Entity* target = session_.world().findEntity(pendingOrderTarget_);
        context_.events.enqueue(AudioEvent{AudioCue::moveOrder});
        const auto order = [&](EntityId id) {
            if (const Entity* entity = session_.world().findEntity(id);
                entity && entity->authority.owner == localPlayer_ && entity->unitControl &&
                entity->authority.directController == 0) {
                if (target && target->processor && target->authority.owner == localPlayer_ &&
                    entity->gatherer) {
                    if (entity->gatherer.carriedAmount > 0.0F)
                        session_.submit({localPlayer_, nextCommandSequence_++,
                                         DeliverResourceCommand{id, target->id}});
                    else
                        session_.submit({localPlayer_, nextCommandSequence_++,
                                         SetPreferredProcessorCommand{id, target->id}});
                } else if (target && target->resource)
                    session_.submit({localPlayer_,
                                     nextCommandSequence_++,
                                     GatherResourceCommand{id, target->id}});
                else if (target && target->construction && !isOperational(*target))
                    session_.submit({localPlayer_, nextCommandSequence_++, ConstructCommand{id, target->id}});
                else if (target && target->health && target->authority.owner != 0 &&
                         target->authority.owner != localPlayer_)
                    session_.submit({localPlayer_,
                                     nextCommandSequence_++,
                                     AttackEntityCommand{id, target->id}});
                else
                    session_.submit(
                        {localPlayer_, nextCommandSequence_++, MoveUnitCommand{id, destination}});
            }
        };
        if (!selectedUnits_.empty())
            for (EntityId id : selectedUnits_)
                order(id);
        else if (selectedEntity_ != 0)
            order(selectedEntity_);
        pendingOrderTarget_ = 0;
    }
    if (pendingConstructionPosition_) {
        EntityId drone = selectedEntity_;
        if (!selectedUnits_.empty()) drone = selectedUnits_.front();
        if (constructionPreviewValid_)
        if (const Entity* builder = session_.world().findEntity(drone); builder && builder->flight) {
            std::vector<EntityId> builders;
            if (selectedUnits_.empty()) builders.push_back(drone);
            else
                for (EntityId id : selectedUnits_)
                    if (const Entity* selected = session_.world().findEntity(id);
                        selected && selected->authority.owner == localPlayer_ && selected->flight)
                        builders.push_back(id);
            session_.submit({localPlayer_, nextCommandSequence_++,
                             PlaceBuildingCommand{drone, constructionRecipeId_,
                                                  *pendingConstructionPosition_,
                                                  std::move(builders)}});
        }
        pendingConstructionPosition_.reset();
        if (constructionPreviewValid_)
            constructionPlacementMode_ = false;
    }
    const float forward = static_cast<float>(forward_) - static_cast<float>(backward_);
    const float right = static_cast<float>(right_) - static_cast<float>(left_);
    if (possessedEntity_ != 0) {
        session_.submit({localPlayer_,
                         nextCommandSequence_++,
                         DirectUnitInputCommand{possessedEntity_,
                                                thirdPersonCamera_.groundMovement(forward, right),
                                                running_,
                                                thirdPersonCamera_.characterFacingDegrees()}});
    } else {
        camera_.pan(forward, right, deltaSeconds, running_);
    }
    session_.update(deltaSeconds);
    sanitizeEntityReferences();
    for (const ResourceEvent& event : session_.consumeResourceEvents()) {
        context_.events.enqueue(event);
        switch (event.kind) {
        case ResourceEventKind::gatheringStarted:
            context_.events.enqueue(AudioEvent{AudioCue::gatherOrder});
            break;
        case ResourceEventKind::deliveryCompleted:
            context_.events.enqueue(AudioEvent{AudioCue::deliveryComplete});
            break;
        case ResourceEventKind::conversionCompleted:
            std::erase_if(hudAlerts_, [&](const HudAlert& alert) {
                return alert.source == event.target;
            });
            context_.events.enqueue(AudioEvent{AudioCue::conversionComplete});
            break;
        case ResourceEventKind::waitingForPower:
            if (std::none_of(hudAlerts_.begin(), hudAlerts_.end(), [&](const HudAlert& alert) {
                    return alert.source == event.target;
                }))
                hudAlerts_.push_back({event.target,
                                      Text::get("processor.alert.waiting_power"), 8.0F});
            context_.events.enqueue(AudioEvent{AudioCue::resourceWarning});
            break;
        case ResourceEventKind::destinationLost:
        case ResourceEventKind::sourceDepleted:
        case ResourceEventKind::sourceInaccessible:
        case ResourceEventKind::destinationInaccessible:
            context_.events.enqueue(AudioEvent{AudioCue::resourceWarning});
            break;
        default:
            break;
        }
    }
    for (const PowerEvent& event : session_.consumePowerEvents()) {
        if (event.player != localPlayer_) continue;
        std::string message;
        switch (event.kind) {
        case PowerEventKind::connectionCreated: message = Text::get("power.alert.connected"); break;
        case PowerEventKind::connectionRemoved: message = Text::get("power.alert.disconnected"); break;
        case PowerEventKind::shortage: message = Text::get("power.alert.shortage"); break;
        case PowerEventKind::recovered: message = Text::get("power.alert.recovered"); break;
        case PowerEventKind::shutdown: message = Text::get("power.alert.shutdown"); break;
        case PowerEventKind::commandRejected:
            powerLinkMode_ = PowerLinkMode::connect;
            powerLinkSource_ = event.source;
            switch (event.reason) {
            case PowerFailureReason::outOfRange: message = Text::get("power.alert.out_of_range"); break;
            case PowerFailureReason::connectionLimit: message = Text::get("power.alert.connection_limit"); break;
            case PowerFailureReason::notConnected:
                powerLinkMode_ = PowerLinkMode::disconnect;
                message = Text::get("power.alert.not_connected");
                break;
            default: message = Text::get("power.alert.invalid_target"); break;
            }
            break;
        }
        hudAlerts_.push_back({event.source, std::move(message), 3.5F});
    }
}

void PlayState::sanitizeEntityReferences() {
    const World& world = session_.world();
    const auto missing = [&](EntityId id) {
        return id != 0 && world.findEntity(id) == nullptr;
    };

    std::erase_if(selectedUnits_, missing);
    if (missing(selectedEntity_))
        selectedEntity_ = selectedUnits_.empty() ? 0 : selectedUnits_.front();
    if (selectedUnits_.size() == 1) {
        selectedEntity_ = selectedUnits_.front();
        selectedUnits_.clear();
    }

    if (missing(hoveredEntity_))
        hoveredEntity_ = 0;
    if (pickedEntity_ && missing(*pickedEntity_))
        pickedEntity_.reset();
    if (pickedEntities_)
        std::erase_if(*pickedEntities_, missing);
    if (missing(lastWorldClickEntity_))
        lastWorldClickEntity_ = 0;
    if (missing(pendingOrderTarget_))
        pendingOrderTarget_ = 0;

    if (missing(powerLinkSource_)) {
        powerLinkSource_ = 0;
        powerLinkMode_ = PowerLinkMode::none;
    }

    if (missing(possessedEntity_)) {
        possessedEntity_ = 0;
        viewMode_ = ViewMode::strategy;
        orbiting_ = false;
        mousePanning_ = false;
        forward_ = backward_ = left_ = right_ = running_ = false;
        setMouseCaptured(false);
    }

    std::erase_if(hudAlerts_, [&](const HudAlert& alert) {
        return missing(alert.source);
    });
}

void PlayState::render(Renderer& renderer) const {
    if (pendingTerrainSeed_) {
        renderer.regenerateTerrain(*pendingTerrainSeed_,
            pendingTerrainChunksPerSide_.value_or(Terrain::chunksPerSide),
            pendingTerrainLayout_.value_or(TerrainLayoutId{"continental"}));
        pendingTerrainSeed_.reset();
        pendingTerrainChunksPerSide_.reset();
        pendingTerrainLayout_.reset();
    }
    renderer.setTerrainFoundations(session_.world().foundations());
    const glm::vec3 focus = camera_.focus();
    const Player* local = session_.players().find(localPlayer_);
    // Satellite imagery is a non-authoritative visibility view. It never modifies saved
    // discovery data, simulation vision, checksums, or the owning player's resource state.
    std::optional<Player> satelliteView;
    const Player* visibilityPlayer = local;
    if (local && satelliteImageryAvailable_ && satelliteRevealActive_) {
        satelliteView = *local;
        std::fill(satelliteView->discovered.begin(), satelliteView->discovered.end(), 255);
        std::fill(satelliteView->visible.begin(), satelliteView->visible.end(), 255);
        visibilityPlayer = &*satelliteView;
    }
    CameraView view =
        camera_.view(renderer.aspectRatio(), renderer.terrainHeightAt(focus.x, focus.z));
    if (viewMode_ == ViewMode::unitControl && possessedEntity_ != 0) {
        if (const Entity* entity = session_.world().findEntity(possessedEntity_)) {
            const float ground = renderer.terrainHeightAt(entity->transform.position.x,
                                                          entity->transform.position.z);
            view = thirdPersonCamera_.view(renderer.aspectRatio(),
                                           {entity->transform.position.x,
                                            ground + entity->transform.position.y,
                                            entity->transform.position.z});
            view = renderer.constrainThirdPersonCamera(view, session_.world(), possessedEntity_);
        }
    }
    if (pendingMoveScreen_) {
        pendingOrderTarget_ = renderer.pickEntity(
            pendingMoveScreen_->x, pendingMoveScreen_->y, session_.world(), view,
            visibilityPlayer, true);
        pendingMoveDestination_ =
            renderer.screenToTerrain(pendingMoveScreen_->x, pendingMoveScreen_->y, view);
        pendingMoveScreen_.reset();
    }
    if (pendingConstructionScreen_) {
        pendingConstructionPosition_ = renderer.screenToTerrain(pendingConstructionScreen_->x, pendingConstructionScreen_->y, view);
        pendingConstructionScreen_.reset();
    }
    if (pendingSelection_) {
        const EntityId selected = renderer.pickEntity(
            pendingSelection_->x, pendingSelection_->y, session_.world(), view,
            visibilityPlayer, true);
        pickedEntity_ = selected;
        pickedEntityAdditive_ = pendingSelectionAdditive_;
        pickedEntityDoubleClick_ = pendingSelectionDoubleClick_;
        pendingSelectionAdditive_ = false;
        pendingSelectionDoubleClick_ = false;
        pendingSelection_.reset();
    }
    if (pendingSelectionRectangle_) {
        pickedEntities_ = renderer.unitsInScreenRectangle(
            {pendingSelectionRectangle_->x, pendingSelectionRectangle_->y},
            {pendingSelectionRectangle_->z, pendingSelectionRectangle_->w},
            session_.world(),
            view,
            localPlayer_);
        pendingSelectionRectangle_.reset();
    }
    if (hoverPosition_ && viewMode_ == ViewMode::strategy) {
        hoveredEntity_ = renderer.pickEntity(
            hoverPosition_->x, hoverPosition_->y, session_.world(), view,
            visibilityPlayer, false);
        hoverPosition_.reset();
    }
    renderer.drawTerrain(view, visibilityPlayer, terrainDebug_, waterDebugMode_);
    particlePresenter_.sync(renderer, session_.world(), visibilityPlayer,
                            session_.mapChunksPerSide());
    renderer.drawVegetation(session_.vegetation(), view, visibilityPlayer);
    renderer.drawWorld(session_.world(), view, visibilityPlayer, powerOverlayVisible_);
    renderer.drawParticles(view);
    if (constructionPlacementMode_ && constructionCursorScreen_) {
        const RecipeDefinition* selectedRecipe =
            context_.definitions.recipe(RecipeId{constructionRecipeId_});
        if (!selectedRecipe || selectedRecipe->product.kind != RecipeProductKind::building)
            return;
        const std::string& buildingId = selectedRecipe->product.id;
        const glm::vec3 position = renderer.screenToTerrain(constructionCursorScreen_->x, constructionCursorScreen_->y, view);
        bool currentlyVisible = true;
        bool previouslyExplored = true;
        const EntityArchetype* buildingDefinition = context_.definitions.archetype(buildingId);
        const float buildingRadius = collisionRadius(context_.definitions, buildingId);
        const TerrainFootprint buildingFootprint = buildingDefinition && buildingDefinition->footprint
                                                       ? *buildingDefinition->footprint
                                                       : TerrainFootprint{FootprintShape::circle,
                                                                          buildingRadius,
                                                                          {buildingRadius, buildingRadius}};
        const SpatialShape placementShape = spatialShape(
            context_.definitions, EntityArchetypeId{buildingId},
            {position.x, position.z}, 0.0F);
        const glm::vec2 placementBounds = axisAlignedHalfExtents(placementShape);
        const TerrainPlacementResult terrainPlacement = renderer.evaluateTerrainPlacement(
            position.x, position.z, buildingFootprint,
            buildingDefinition ? buildingDefinition->placement : TerrainPlacementProfile{});
        if (local) {
            const glm::ivec2 cell = MapArea{session_.mapChunksPerSide()}.gridCell(
                {position.x, position.z}, Player::explorationCells);
            const auto index = static_cast<std::size_t>(
                cell.y * Player::explorationCells + cell.x);
            currentlyVisible = index < visibilityPlayer->visible.size() &&
                               visibilityPlayer->visible[index] != 0;
            previouslyExplored = index < visibilityPlayer->discovered.size() &&
                                 visibilityPlayer->discovered[index] != 0;
        }
        constructionPreviewValid_ = true;
        constructionPreviewReason_.clear();
        const auto invalidate = [&](const char* key) {
            if (constructionPreviewValid_) constructionPreviewReason_ = Text::get(key);
            constructionPreviewValid_ = false;
        };
        if (!previouslyExplored) invalidate("placement.unexplored");
        const MapArea placementMap{session_.mapChunksPerSide()};
        if (std::abs(position.x) + placementBounds.x > placementMap.halfExtent() ||
            std::abs(position.z) + placementBounds.y > placementMap.halfExtent())
            invalidate("placement.outside_map");
        // Hidden enemy construction is resolved authoritatively by PlaceBuildingCommand.
        // Do not leak it through a red preview in previously explored fog.
        if (currentlyVisible &&
            overlapsObject(session_.world(), context_.definitions, placementShape))
            invalidate("placement.collision");
        if (!terrainPlacement.valid()) {
            const char* key = terrainPlacement.failure == TerrainPlacementFailure::excessiveSlope
                                  ? "placement.slope"
                              : terrainPlacement.failure == TerrainPlacementFailure::shoreRequired
                                  ? "placement.shore"
                                  : "placement.terrain";
            invalidate(key);
        }
        if (local)
            if (selectedRecipe)
                for (const auto& [resource, amount] : selectedRecipe->cost)
                    if (!local->resources.contains(resource) || local->resources.at(resource) < amount)
                        invalidate("placement.resources");
        World preview;
        Entity& ghost = preview.createEntity(buildingId, buildingId, localPlayer_);
        context_.definitions.initializeEntity(ghost);
        ghost.transform.position = {position.x, 0.0F, position.z};
        ghost.construction.emplace();
        ghost.construction.state = BuildingLifecycleState::planned;
        ghost.construction.placementValid = constructionPreviewValid_;
        renderer.drawWorld(preview, view, nullptr);
    }
    if (viewMode_ == ViewMode::strategy)
        renderer.drawOrderMarkers(session_.world(), selectedEntity_, selectedUnits_, view);
    if (!selectedUnits_.empty())
        for (EntityId selected : selectedUnits_)
            renderer.drawEntityOutline(session_.world(), selected, view, visibilityPlayer);
    else if (selectedEntity_ != 0)
        renderer.drawEntityOutline(session_.world(), selectedEntity_, view, visibilityPlayer);
    if (hoveredEntity_ != 0 && viewMode_ == ViewMode::strategy)
        renderer.drawEntityOutline(session_.world(), hoveredEntity_, view, visibilityPlayer);
    if (powerOverlayVisible_)
        renderer.drawPowerConnections(session_.world(), view, localPlayer_);
    if (terrainDebug_)
        renderer.drawResourceFieldDebug(session_.resourceLayout(), view);
    if (draggingSelection_ && viewMode_ == ViewMode::strategy)
        renderer.drawSelectionBox(selectionStart_, selectionEnd_);
    if (detailedDebug_) {
        const EntityId detailEntity = possessedEntity_ ? possessedEntity_ : selectedEntity_;
        renderer.drawVisionRanges(view, session_.world().findEntity(detailEntity));
        renderer.drawDetailedDebugHud(view,
                                      session_.world().findEntity(detailEntity),
                                      visibilityPlayer,
                                      session_.terrainSeed(),
                                      session_.tick(),
                                      session_.world().size());
    }
    if (viewMode_ == ViewMode::unitControl && possessedEntity_ != 0) {
        if (const Entity* controlled = session_.world().findEntity(possessedEntity_)) {
            EntityHudModel hud = EntityHudModelBuilder::build(
                session_.world(), possessedEntity_, {}, context_.definitions);
            hud.footer = Text::get("unit.escape");
            UiDocument layout = EntityHudLayout::directControl(
                hud, renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
            renderer.drawEntityHud(hud, layout);
            renderer.drawCrosshair();
        }
    } else {
        UiDocument minimap = GameHudLayout::minimap(
            renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
        renderer.drawStrategyHud(session_.world(), selectedEntity_, visibilityPlayer, minimap);
        const Entity* selected = session_.world().findEntity(selectedEntity_);
        const bool sameType = selected && isHomogeneousSelection(
            session_.world(), selectedUnits_, selected->archetype.value);
        if (!selectedUnits_.empty() && !sameType) {
            EntityHudModel hud = EntityHudModelBuilder::build(
                session_.world(), selectedEntity_, selectedUnits_, context_.definitions);
            UiDocument layout = EntityHudLayout::selection(
                hud, renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
            uiController_.apply(layout);
            renderer.drawEntityHud(hud, layout);
        }
        if (selected && selected->authority.owner == localPlayer_ &&
            selected->archetype.value == "construction_drone" &&
            (selectedUnits_.empty() || sameType)) {
            EntityHudModel hud = buildConstructionHudModel(*selected, local);
            UiDocument layout = EntityHudLayout::actions(
                hud, renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
            uiController_.apply(layout);
            renderer.drawEntityHud(hud, layout);
        }
        if (selected && selected->resource &&
            (selectedUnits_.empty() || sameType)) {
            EntityHudModel hud = EntityHudModelBuilder::build(
                session_.world(), selectedEntity_, selectedUnits_, context_.definitions);
            UiDocument layout = EntityHudLayout::directControl(
                hud, renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
            renderer.drawEntityHud(hud, layout);
        }
        if (selected && selected->authority.owner == localPlayer_ &&
            selected->archetype.value != "construction_drone" &&
            (selectedUnits_.empty() || sameType)) {
            EntityHudModel hud = buildEntityActionHudModel(*selected, local);
            UiDocument layout = hud.actions.empty() && hud.queue.empty()
                ? EntityHudLayout::directControl(
                      hud, renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale)
                : EntityHudLayout::actions(
                      hud, renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
            uiController_.apply(layout);
            renderer.drawEntityHud(hud, layout);
        }
    }
    // The resource bar and its expanded power panel are top-level HUD. Draw them
    // after world outlines and entity panels so selection geometry cannot bleed
    // through the opaque overlay.
    if (local)
    {
        const auto resources = context_.definitions.enabledResources();
        const std::size_t localCount = std::count_if(
            resources.begin(), resources.end(), [](const ResourceDefinition* resource) {
                return resource->storage == ResourceStorageKind::stockpile;
            });
        const std::size_t deviceCount = std::count_if(
            session_.world().entities().begin(), session_.world().entities().end(),
            [&](const Entity& entity) {
                const EntityArchetype* archetype = context_.definitions.archetype(entity.archetype);
                return entity.authority.owner == localPlayer_ && archetype && archetype->powerDevice;
            });
        UiDocument resourceUi = GameHudLayout::resources(
            localCount, deviceCount, powerOverlayVisible_, satelliteRevealActive_,
            renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
        uiController_.apply(resourceUi);
        renderer.drawResourceHud(*local, session_.world(), context_.definitions,
                                 powerOverlayVisible_, resourceUi);
    }
    if (!hudAlerts_.empty()) {
        std::vector<std::string> messages;
        for (const HudAlert& alert : hudAlerts_) messages.push_back(alert.text);
        renderer.drawUi(GameHudLayout::alerts(messages, renderer.viewportWidth(),
                                              renderer.viewportHeight(), config_.uiScale));
    }
    if (constructionPlacementMode_ && !constructionPreviewValid_ &&
        !constructionPreviewReason_.empty()) {
        const float width = 310.0F;
        const float left = std::clamp(pointerScreen_.x + 18.0F, 8.0F,
                                      static_cast<float>(renderer.viewportWidth()) - width - 8.0F);
        const float top = std::clamp(pointerScreen_.y + 18.0F, 8.0F,
                                     static_cast<float>(renderer.viewportHeight()) - 42.0F);
        UiDocument placementUi;
        placementUi.tooltip("placement.reason", {left, top, left + width, top + 34.0F},
                            constructionPreviewReason_);
        renderer.drawUi(placementUi);
    }
    // Terrain inspection is a UI pass and must remain after every world/overlay pass.
    // Keeping it at the top of the visual stack also prevents entity outlines from
    // showing through its opaque panel.
    if (terrainDebug_) {
        const glm::vec3 cursor = renderer.screenToTerrain(pointerScreen_.x, pointerScreen_.y, view);
        renderer.drawTerrainDebugHud(cursor, session_.resourceLayout());
    }
    if (waterDebugMode_ != 0) {
        const glm::vec3 cursor = renderer.screenToTerrain(pointerScreen_.x, pointerScreen_.y, view);
        renderer.drawWaterDebugHud(cursor, waterDebugMode_);
    }
    if (paused_) {
        UiDocument document = pauseUi(renderer.viewportWidth(), renderer.viewportHeight());
        uiController_.apply(document);
        renderer.drawUi(document);
    }
}

} // namespace strategy
