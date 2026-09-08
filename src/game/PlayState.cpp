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
               setup.resourceAbundanceScale)
    , config_(GameConfig::load(context.configPath)) {}

PlayState::PlayState(StateContext& context, SaveData data)
    : GameState(context)
    , session_(context.definitions,
               data.terrainSeed,
               data.playerOneCountry,
               data.playerTwoCountry,
               data.playerOneSpecialization,
               data.playerTwoSpecialization,
               data.mapChunksPerSide)
    , config_(GameConfig::load(context.configPath)) {
    session_.replaceWorld(std::move(data.entities), data.terrainSeed,
                          std::move(data.foundations), data.mapChunksPerSide);
    session_.restorePlayerProgress(1,
                                   data.wood[0],
                                   data.stone[0],
                                   data.gold[0],
                                   std::move(data.resources[0]),
                                   std::move(data.discovered[0]),
                                   std::move(data.intelligence[0]));
    session_.restorePlayerProgress(2,
                                   data.wood[1],
                                   data.stone[1],
                                   data.gold[1],
                                   std::move(data.resources[1]),
                                   std::move(data.discovered[1]),
                                   std::move(data.intelligence[1]));
}

EntityHudModel PlayState::buildConstructionHudModel(const Entity& selected,
                                                    const Player* player) const {
    EntityHudModel hud = EntityHudModelBuilder::build(
        session_.world(), selected.id, selectedUnits_, context_.definitions);
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
        if (action.cost.empty()) action.cost = Text::get("entity_hud.free");
        action.enabled = canAffordForAll(player, research, targets.size()) &&
                         std::all_of(targets.begin(), targets.end(), [&](EntityId id) {
                             return session_.canStartUpgrade(localPlayer_, id, upgrade->id);
                         });
        if (!action.enabled) action.disabledReason = Text::get("entity_hud.action_unavailable");
        hud.actions.push_back(std::move(action));
    }
    return hud;
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
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F5) {
        try {
            SaveGame::write(
                config_.savePath(), session_.terrainSeed(), session_.world(), &session_.players(),
                session_.mapChunksPerSide());
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
            session_.replaceWorld(std::move(data.entities), data.terrainSeed,
                                  std::move(data.foundations), data.mapChunksPerSide);
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
        if (constructionPlacementMode_) {
            constructionPlacementMode_ = false;
            pendingConstructionScreen_.reset();
            pendingConstructionPosition_.reset();
            return;
        }
        if (viewMode_ == ViewMode::unitControl && possessedEntity_ != 0) {
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
    const auto hudResources = context_.definitions.enabledResources();
    const std::size_t localResourceCount = std::count_if(
        hudResources.begin(), hudResources.end(), [](const ResourceDefinition* resource) {
            return resource->storage != ResourceStorageKind::network;
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
            localResourceCount, powerDeviceCount, powerOverlayVisible_, width, height,
            config_.uiScale);
        if (uiController_.press(resourceUi, {event.button.x, event.button.y}) == "resources.power") {
            powerOverlayVisible_ = !powerOverlayVisible_;
            return;
        }
        if (const UiElement* panel = resourceUi.find("power.panel");
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
                constructionRecipeId_ = *actionId;
                constructionPlacementMode_ = true;
                return;
            }
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && layout.find("entity.panel") &&
            layout.find("entity.panel")->bounds.contains(point)) return;
    }
    // Right-click always cancels an active placement preview, including invalid (red) previews.
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
        hoverPosition_ = glm::vec2{event.motion.x, event.motion.y};
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
        case SDLK_RSHIFT:
            running_ = pressed;
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
    uiController_.advance(deltaSeconds);
    if (paused_) {
        return;
    }
    if (pickedEntity_) {
        const EntityId clicked = *pickedEntity_;
        pickedEntity_.reset();
        if (pickedEntityAdditive_) {
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
                if (target && target->resource)
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
        camera_.pan(forward, right, deltaSeconds);
    }
    session_.update(deltaSeconds);
}

void PlayState::render(Renderer& renderer) const {
    if (pendingTerrainSeed_) {
        renderer.regenerateTerrain(*pendingTerrainSeed_,
            pendingTerrainChunksPerSide_.value_or(Terrain::chunksPerSide));
        pendingTerrainSeed_.reset();
        pendingTerrainChunksPerSide_.reset();
    }
    renderer.setTerrainFoundations(session_.world().foundations());
    const glm::vec3 focus = camera_.focus();
    const Player* local = session_.players().find(localPlayer_);
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
            pendingMoveScreen_->x, pendingMoveScreen_->y, session_.world(), view, local, true);
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
            pendingSelection_->x, pendingSelection_->y, session_.world(), view, local, true);
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
            hoverPosition_->x, hoverPosition_->y, session_.world(), view, local, false);
        hoverPosition_.reset();
    }
    renderer.drawTerrain(view, local);
    renderer.drawWorld(session_.world(), view, local);
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
        const FootprintFit footprint = renderer.fitTerrainFootprint(
            position.x, position.z, buildingFootprint);
        if (local) {
            constexpr float extent = Terrain::cellCount * Terrain::spacing;
            const int gx = std::clamp(static_cast<int>((position.x / extent + 0.5F) * Player::explorationCells),
                                      0, Player::explorationCells - 1);
            const int gz = std::clamp(static_cast<int>((position.z / extent + 0.5F) * Player::explorationCells),
                                      0, Player::explorationCells - 1);
            const auto index = static_cast<std::size_t>(gz * Player::explorationCells + gx);
            currentlyVisible = index < local->visible.size() && local->visible[index] != 0;
            previouslyExplored = index < local->discovered.size() && local->discovered[index] != 0;
        }
        constructionPreviewValid_ = previouslyExplored;
        const float mapHalfExtent =
            static_cast<float>(session_.mapChunksPerSide() * Terrain::chunkCellCount) *
            Terrain::spacing * 0.5F;
        constructionPreviewValid_ = constructionPreviewValid_ &&
            std::abs(position.x) + buildingRadius <= mapHalfExtent &&
            std::abs(position.z) + buildingRadius <= mapHalfExtent;
        // Hidden enemy construction is resolved authoritatively by PlaceBuildingCommand.
        // Do not leak it through a red preview in previously explored fog.
        if (currentlyVisible)
            constructionPreviewValid_ = constructionPreviewValid_ &&
                !overlapsObject(session_.world(), context_.definitions,
                                {position.x, position.z}, buildingRadius);
        constructionPreviewValid_ = constructionPreviewValid_ && footprint.valid;
        if (local)
            if (selectedRecipe)
                for (const auto& [resource, amount] : selectedRecipe->cost)
                    if (!local->resources.contains(resource) || local->resources.at(resource) < amount)
                        constructionPreviewValid_ = false;
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
    if (viewMode_ == ViewMode::strategy) {
        const Entity* selected = session_.world().findEntity(selectedEntity_);
        const bool sameType = selected && isHomogeneousSelection(
            session_.world(), selectedUnits_, selected->archetype.value);
        if ((selectedUnits_.empty() || sameType) && selected &&
            selected->authority.owner == localPlayer_ &&
            selected->archetype.value == "construction_drone") {
            EntityHudModel hud = buildConstructionHudModel(*selected, local);
            UiDocument layout = EntityHudLayout::actions(
                hud, renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
            uiController_.apply(layout);
            renderer.drawEntityHud(hud, layout);
        }
    }
    if (!selectedUnits_.empty())
        for (EntityId selected : selectedUnits_)
            renderer.drawEntityOutline(session_.world(), selected, view, local);
    else if (selectedEntity_ != 0)
        renderer.drawEntityOutline(session_.world(), selectedEntity_, view, local);
    if (hoveredEntity_ != 0 && viewMode_ == ViewMode::strategy)
        renderer.drawEntityOutline(session_.world(), hoveredEntity_, view, local);
    if (draggingSelection_ && viewMode_ == ViewMode::strategy)
        renderer.drawSelectionBox(selectionStart_, selectionEnd_);
    if (detailedDebug_) {
        const EntityId detailEntity = possessedEntity_ ? possessedEntity_ : selectedEntity_;
        renderer.drawVisionRanges(view, session_.world().findEntity(detailEntity));
        renderer.drawDetailedDebugHud(view,
                                      session_.world().findEntity(detailEntity),
                                      local,
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
        renderer.drawStrategyHud(session_.world(), selectedEntity_, local, minimap);
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
                return resource->storage != ResourceStorageKind::network;
            });
        const std::size_t deviceCount = std::count_if(
            session_.world().entities().begin(), session_.world().entities().end(),
            [&](const Entity& entity) {
                const EntityArchetype* archetype = context_.definitions.archetype(entity.archetype);
                return entity.authority.owner == localPlayer_ && archetype && archetype->powerDevice;
            });
        UiDocument resourceUi = GameHudLayout::resources(
            localCount, deviceCount, powerOverlayVisible_,
            renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
        uiController_.apply(resourceUi);
        renderer.drawResourceHud(*local, session_.world(), context_.definitions,
                                 powerOverlayVisible_, resourceUi);
    }
    if (paused_) {
        UiDocument document = pauseUi(renderer.viewportWidth(), renderer.viewportHeight());
        uiController_.apply(document);
        renderer.drawUi(document);
    }
}

} // namespace strategy
