#include "game/PlayState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "diagnostics/Logger.hpp"
#include "localization/Text.hpp"
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

PlayState::PlayState(StateContext& context,
                     std::uint32_t terrainSeed,
                     std::string playerOneCountry,
                     std::string playerTwoCountry)
    : GameState(context)
    , session_(context.definitions,
               terrainSeed,
               std::move(playerOneCountry),
               std::move(playerTwoCountry))
    , config_(GameConfig::load(context.configPath)) {}

PlayState::PlayState(StateContext& context, SaveData data)
    : GameState(context)
    , session_(context.definitions,
               data.terrainSeed,
               data.playerOneCountry,
               data.playerTwoCountry,
               data.playerOneSpecialization,
               data.playerTwoSpecialization)
    , config_(GameConfig::load(context.configPath)) {
    session_.replaceWorld(std::move(data.entities), data.terrainSeed, std::move(data.foundations));
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
                config_.savePath(), session_.terrainSeed(), session_.world(), &session_.players());
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
            session_.replaceWorld(std::move(data.entities), data.terrainSeed, std::move(data.foundations));
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
    const Entity* droneForBuildHud = session_.world().findEntity(selectedEntity_);
    const bool homogeneousSelection =
        droneForBuildHud && isHomogeneousSelection(session_.world(), selectedUnits_,
                                                   droneForBuildHud->archetype.value);
    const bool buildHudVisible = viewMode_ == ViewMode::strategy && droneForBuildHud &&
                                 (selectedUnits_.empty() || homogeneousSelection) &&
                                 droneForBuildHud->authority.owner == localPlayer_ &&
                                 droneForBuildHud->archetype.value == "construction_drone";
    if (buildHudVisible && event.type == SDL_EVENT_MOUSE_MOTION) {
        int windowHeight = 0, windowWidth = 0;
        if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_)) SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        constructionButtonHovered_ = event.motion.x >= 30.0F && event.motion.x <= 220.0F &&
                                     event.motion.y >= windowHeight - 75.0F && event.motion.y <= windowHeight - 30.0F;
        constructionCursorScreen_ = glm::vec2{event.motion.x, event.motion.y};
    }
    if (buildHudVisible && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && constructionButtonHovered_) {
        constructionPlacementMode_ = !constructionPlacementMode_;
        return;
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
        std::vector<std::string> groups;
        for (EntityId id : selectedUnits_)
            if (const Entity* entity = session_.world().findEntity(id))
                if (std::find(groups.begin(), groups.end(), entity->archetype.value) == groups.end())
                    groups.push_back(entity->archetype.value);
        const std::size_t visible = std::min<std::size_t>(groups.size(), 6);
        const float panelHeight = 58.0F + static_cast<float>(visible) * 24.0F;
        int windowHeight = 0, width = 0;
        if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_))
            SDL_GetWindowSize(window, &width, &windowHeight);
        (void)width;
        const float top = static_cast<float>(windowHeight) - panelHeight - 18.0F;
        if (event.button.x >= 18.0F && event.button.x <= 410.0F && event.button.y >= top + 34.0F &&
            event.button.y < top + 34.0F + visible * 24.0F) {
            const std::size_t row = std::min<std::size_t>(
                static_cast<std::size_t>((event.button.y - (top + 34.0F)) / 24.0F), visible - 1);
            const std::string chosen = groups[row];
            const bool removeType = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            selectedUnits_.erase(
                std::remove_if(selectedUnits_.begin(),
                               selectedUnits_.end(),
                               [&](EntityId id) {
                                   const Entity* entity = session_.world().findEntity(id);
                                   return !entity || (removeType ? entity->archetype.value == chosen
                                                                 : entity->archetype.value != chosen);
                               }),
                selectedUnits_.end());
            selectedEntity_ = selectedUnits_.empty() ? 0 : selectedUnits_.front();
            if (selectedUnits_.size() == 1) {
                selectedUnits_.clear();
            }
            lastWorldClickEntity_ = 0;
            townHallButtonHovered_ = {false, false, false};
            return;
        }
    }
    const Entity* selectedHall = session_.world().findEntity(selectedEntity_);
    const bool ownsSelectedHall = selectedHall && selectedHall->authority.owner == localPlayer_ &&
                                  (selectedUnits_.empty() || homogeneousSelection);
    if (ownsSelectedHall &&
        (event.type == SDL_EVENT_MOUSE_MOTION ||
         (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT))) {
        const auto recipes = context_.definitions.recipesForProducer(selectedHall->archetype.value);
        const auto upgrades = context_.definitions.upgradesForResearcher(selectedHall->archetype.value);
        const std::vector<EntityId> targets = actionTargets(selectedEntity_, selectedUnits_);
        const Player* local = session_.players().find(localPlayer_);
        const auto recipeEnabled = [&](std::size_t index) {
            return index < recipes.size() && canAffordForAll(local, recipes[index], targets.size()) &&
                   std::all_of(targets.begin(), targets.end(), [&](EntityId id) {
                       return session_.canStartRecipe(localPlayer_, id, RecipeId{recipes[index]->id});
                   });
        };
        const auto upgradeEnabled = [&](std::size_t index) {
            if (index >= upgrades.size()) return false;
            const RecipeDefinition* research = context_.definitions.recipe(
                RecipeId{upgrades[index]->researchRecipe});
            return canAffordForAll(local, research, targets.size()) &&
                   std::all_of(targets.begin(), targets.end(), [&](EntityId id) {
                       return session_.canStartUpgrade(localPlayer_, id, upgrades[index]->id);
                   });
        };
        const std::size_t actionCount = std::min<std::size_t>(6, recipes.size() + upgrades.size());
        int width = 0, height = 0;
        if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_))
            SDL_GetWindowSize(window, &width, &height);
        (void)width;
        const float x = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
        const float y = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
        entityActionHovered_ = -1;
        productionQueueHovered_ = -1;
        if (selectedHall->production) {
            const std::size_t visibleQueue =
                std::min<std::size_t>(selectedHall->production.queue.size(), 9);
            const float queueTop = static_cast<float>(height) - 153.0F;
            for (std::size_t i = 0; i < visibleQueue; ++i) {
                const float left = 30.0F + static_cast<float>(i) * 54.0F;
                if (selectedHall->production.queue[i].kind == ProductionKind::improveTraining &&
                    x >= left && x <= left + 44.0F &&
                    y >= queueTop && y <= queueTop + 44.0F)
                    productionQueueHovered_ = static_cast<int>(i);
            }
        }
        for (std::size_t i = 0; i < actionCount; ++i) {
            const float left = 30.0F + static_cast<float>(i) * 96.0F;
            if (x >= left && x <= left + 82.0F && y >= height - 86.0F && y <= height - 30.0F)
                entityActionHovered_ = static_cast<int>(i);
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (productionQueueHovered_ >= 0) {
                session_.submit({localPlayer_, nextCommandSequence_++,
                                 CancelProductionCommand{
                                     selectedHall->id,
                                     static_cast<std::uint32_t>(productionQueueHovered_)}});
                return;
            }
            if (entityActionHovered_ >= 0 && static_cast<std::size_t>(entityActionHovered_) < recipes.size() &&
                recipeEnabled(static_cast<std::size_t>(entityActionHovered_))) {
                const auto submitRecipe = [&](EntityId id) {
                    session_.submit({localPlayer_, nextCommandSequence_++, StartRecipeCommand{id, recipes[entityActionHovered_]->id}});
                };
                if (selectedUnits_.empty()) submitRecipe(selectedHall->id);
                else for (EntityId id : selectedUnits_) submitRecipe(id);
                context_.events.enqueue(AudioEvent{AudioCue::trainUnit});
            } else if (entityActionHovered_ >= 0) {
                const std::size_t upgradeIndex = static_cast<std::size_t>(entityActionHovered_) - recipes.size();
                if (upgradeEnabled(upgradeIndex)) {
                    const auto submitUpgrade = [&](EntityId id) {
                        session_.submit({localPlayer_, nextCommandSequence_++, StartUpgradeCommand{id, upgrades[upgradeIndex]->id}});
                    };
                    if (selectedUnits_.empty()) submitUpgrade(selectedHall->id);
                    else for (EntityId id : selectedUnits_) submitUpgrade(id);
                }
            }
            if (entityActionHovered_ >= 0)
                return;
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

void PlayState::updatePauseHover(float mouseX, float mouseY) {
    const bool insideHorizontal = mouseX >= 60.0F && mouseX <= 300.0F;
    resumeHovered_ = insideHorizontal && mouseY >= 250.0F && mouseY <= 310.0F;
    settingsHovered_ = insideHorizontal && mouseY >= 330.0F && mouseY <= 390.0F;
    exitHovered_ = insideHorizontal && mouseY >= 410.0F && mouseY <= 470.0F;
}

void PlayState::handlePauseEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        updatePauseHover(event.motion.x, event.motion.y);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        updatePauseHover(event.button.x, event.button.y);
        if (resumeHovered_) {
            paused_ = false;
        } else if (settingsHovered_) {
            request_ = StateRequest::openSettings;
        } else if (exitHovered_) {
            request_ = StateRequest::returnToMainMenu;
        }
    }
}

void PlayState::update(float deltaSeconds) {
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
                else if (target && target->construction && !target->construction.complete)
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
                             PlaceBuildingCommand{drone, "construct.command_hub",
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
        renderer.regenerateTerrain(*pendingTerrainSeed_);
        pendingTerrainSeed_.reset();
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
        const glm::vec3 position = renderer.screenToTerrain(constructionCursorScreen_->x, constructionCursorScreen_->y, view);
        bool currentlyVisible = true;
        bool previouslyExplored = true;
        const EntityArchetype* buildingDefinition = context_.definitions.archetype("command_hub");
        const float buildingRadius = collisionRadius(context_.definitions, "command_hub");
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
        // Hidden enemy construction is resolved authoritatively by PlaceBuildingCommand.
        // Do not leak it through a red preview in previously explored fog.
        if (currentlyVisible)
            constructionPreviewValid_ = constructionPreviewValid_ &&
                !overlapsObject(session_.world(), context_.definitions,
                                {position.x, position.z}, buildingRadius);
        constructionPreviewValid_ = constructionPreviewValid_ && footprint.valid;
        if (local)
            if (const RecipeDefinition* recipe = context_.definitions.recipe(RecipeId{"construct.command_hub"}))
                for (const auto& [resource, amount] : recipe->cost)
                    if (!local->resources.contains(resource) || local->resources.at(resource) < amount)
                        constructionPreviewValid_ = false;
        World preview;
        Entity& ghost = preview.createEntity("Command Hub", "command_hub", localPlayer_);
        context_.definitions.initializeEntity(ghost);
        ghost.transform.position = {position.x, 0.0F, position.z};
        ghost.construction.emplace();
        ghost.construction.complete = false;
        ghost.construction.placementValid = constructionPreviewValid_;
        renderer.drawWorld(preview, view, nullptr);
    }
    if (local)
        renderer.drawResourceHud(*local);
    if (viewMode_ == ViewMode::strategy)
        renderer.drawOrderMarkers(session_.world(), selectedEntity_, selectedUnits_, view);
    if (viewMode_ == ViewMode::strategy) {
        const Entity* selected = session_.world().findEntity(selectedEntity_);
        const bool sameType = selected && isHomogeneousSelection(
            session_.world(), selectedUnits_, selected->archetype.value);
        if ((selectedUnits_.empty() || sameType) && selected &&
            selected->authority.owner == localPlayer_ &&
            selected->archetype.value == "construction_drone")
            renderer.drawBuildHud(localPlayer_, constructionPlacementMode_ ? "PLACE COMMAND HUB" : "CONSTRUCTION", session_.world().size(), "COMMAND HUB  |  COST: 100 MATERIALS", constructionButtonHovered_, constructionPlacementMode_);
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
        if (const Entity* controlled = session_.world().findEntity(possessedEntity_))
            renderer.drawUnitHud(*controlled);
    } else {
        renderer.drawStrategyHud(session_.world(), selectedEntity_, local);
        const Entity* selected = session_.world().findEntity(selectedEntity_);
        const bool sameType = selected && isHomogeneousSelection(
            session_.world(), selectedUnits_, selected->archetype.value);
        if (!selectedUnits_.empty() && !sameType)
            renderer.drawUnitSelectionHud(session_.world(), selectedUnits_);
        if (selected && selected->authority.owner == localPlayer_ &&
            (selectedUnits_.empty() || sameType)) {
            std::vector<std::string> labels, costs;
            std::vector<bool> enabled;
            const std::vector<EntityId> targets = actionTargets(selectedEntity_, selectedUnits_);
            const auto recipes = context_.definitions.recipesForProducer(selected->archetype.value);
            for (const RecipeDefinition* recipe : recipes) {
                const EntityArchetype* product = context_.definitions.archetype(recipe->product.id);
                labels.push_back(product ? Text::get(product->nameKey) : recipe->product.id);
                std::string cost;
                for (const auto& [resource, amount] : recipe->cost)
                    cost += resource + ": " + std::to_string(static_cast<int>(amount)) + " ";
                if (local) cost += "time: " + std::to_string(static_cast<int>(context_.definitions.productionDuration(local->countryId, local->specializationId, selected->archetype.value, recipe->product.id))) + "s";
                costs.push_back(cost.empty() ? "free" : cost);
                enabled.push_back(canAffordForAll(local, recipe, targets.size()) &&
                                  std::all_of(targets.begin(), targets.end(), [&](EntityId id) {
                                      return session_.canStartRecipe(
                                          localPlayer_, id, RecipeId{recipe->id});
                                  }));
            }
            for (const UpgradeDefinition* upgrade : context_.definitions.upgradesForResearcher(selected->archetype.value)) {
                labels.push_back(Text::get(upgrade->nameKey));
                std::string cost;
                if (const RecipeDefinition* recipe = context_.definitions.recipe(RecipeId{upgrade->researchRecipe}))
                    for (const auto& [resource, amount] : recipe->cost)
                        cost += resource + ": " + std::to_string(static_cast<int>(amount)) + " ";
                costs.push_back(cost.empty() ? "free" : cost);
                const RecipeDefinition* research =
                    context_.definitions.recipe(RecipeId{upgrade->researchRecipe});
                enabled.push_back(canAffordForAll(local, research, targets.size()) &&
                                  std::all_of(targets.begin(), targets.end(), [&](EntityId id) {
                                      return session_.canStartUpgrade(localPlayer_, id, upgrade->id);
                                  }));
            }
            std::vector<std::string> queueLabels;
            if (selected->production)
                for (const ProductionOrder& order : selected->production.queue) {
                    std::string label;
                    if (order.kind == ProductionKind::improveTraining) {
                        if (const UpgradeDefinition* upgrade =
                                context_.definitions.upgrade(order.upgradeId))
                            label = Text::get(upgrade->nameKey);
                        else
                            label = order.upgradeId;
                    }
                    queueLabels.push_back(std::move(label));
                }
            renderer.drawEntityActionHud(*selected,
                                         labels,
                                         costs,
                                         enabled,
                                         entityActionHovered_,
                                         queueLabels,
                                         productionQueueHovered_);
        }
    }
    if (paused_) {
        renderer.drawPauseMenu(resumeHovered_, settingsHovered_, exitHovered_);
    }
}

} // namespace strategy
