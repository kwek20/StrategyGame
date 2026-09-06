#include "game/PlayState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "diagnostics/Logger.hpp"
#include "persistence/SaveGame.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <iostream>

namespace strategy {

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
    session_.replaceWorld(std::move(data.entities), data.terrainSeed);
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
            session_.replaceWorld(std::move(data.entities), data.terrainSeed);
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
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
        viewMode_ == ViewMode::strategy && !selectedUnits_.empty()) {
        std::vector<std::string> groups;
        for (EntityId id : selectedUnits_)
            if (const Entity* entity = session_.world().findEntity(id))
                if (std::find(groups.begin(), groups.end(), entity->modelKey) == groups.end())
                    groups.push_back(entity->modelKey);
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
                                   return !entity || (removeType ? entity->modelKey == chosen
                                                                 : entity->modelKey != chosen);
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
                                  selectedHall->modelKey.rfind("town_center", 0) == 0 &&
                                  (selectedUnits_.empty() || selectedUnits_.size() == 1);
    if (ownsSelectedHall &&
        (event.type == SDL_EVENT_MOUSE_MOTION ||
         (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT))) {
        int width = 0, height = 0;
        if (SDL_Window* window = SDL_GetWindowFromID(inputWindowId_))
            SDL_GetWindowSize(window, &width, &height);
        (void)width;
        const float x = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
        const float y = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
        constexpr std::array<float, 3> lefts{30.0F, 230.0F, 430.0F};
        constexpr std::array<float, 3> rights{220.0F, 420.0F, 620.0F};
        for (std::size_t i = 0; i < 3; ++i)
            townHallButtonHovered_[i] =
                x >= lefts[i] && x <= rights[i] && y >= height - 75.0F && y <= height - 30.0F;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (townHallButtonHovered_[0] && selectedHall->buildingUpgrades &&
                selectedHall->buildingUpgrades.level < 3) {
                session_.submit({localPlayer_,
                                 nextCommandSequence_++,
                                 UpgradeTownHallCommand{selectedHall->id}});
                context_.events.enqueue(AudioEvent{AudioCue::buildingUpgrade});
            } else if (townHallButtonHovered_[1]) {
                session_.submit({localPlayer_,
                                 nextCommandSequence_++,
                                 ImproveTrainingCommand{selectedHall->id}});
                context_.events.enqueue(AudioEvent{AudioCue::trainingUpgrade});
            } else if (townHallButtonHovered_[2]) {
                if (const RecipeDefinition* recipe =
                        context_.definitions.productionRecipe(selectedHall->modelKey, "worker")) {
                    session_.submit({localPlayer_,
                                     nextCommandSequence_++,
                                     StartRecipeCommand{selectedHall->id, recipe->id}});
                    context_.events.enqueue(AudioEvent{AudioCue::trainUnit});
                }
            }
            if (townHallButtonHovered_[0] || townHallButtonHovered_[1] || townHallButtonHovered_[2])
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
    if (pendingMoveDestination_) {
        const glm::vec3 destination = *pendingMoveDestination_;
        pendingMoveDestination_.reset();
        const Entity* target = session_.world().findEntity(pendingOrderTarget_);
        context_.events.enqueue(AudioEvent{AudioCue::moveOrder});
        const auto order = [&](EntityId id) {
            if (const Entity* entity = session_.world().findEntity(id);
                entity && entity->authority.owner == localPlayer_ && entity->unitControl &&
                entity->unitControl.directlyControllable) {
                if (target && target->resource)
                    session_.submit({localPlayer_,
                                     nextCommandSequence_++,
                                     GatherResourceCommand{id, target->id}});
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
    if (local)
        renderer.drawResourceHud(*local);
    if (viewMode_ == ViewMode::strategy)
        renderer.drawOrderMarkers(session_.world(), selectedEntity_, selectedUnits_, view);
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
        if (!selectedUnits_.empty())
            renderer.drawUnitSelectionHud(session_.world(), selectedUnits_);
        if (const Entity* hall = session_.world().findEntity(selectedEntity_);
            hall && hall->authority.owner == localPlayer_ && hall->production &&
            hall->buildingUpgrades &&
            (selectedUnits_.empty() || selectedUnits_.size() == 1))
            renderer.drawTownHallHud(*hall, townHallButtonHovered_);
    }
    if (paused_) {
        renderer.drawPauseMenu(resumeHovered_, settingsHovered_, exitHovered_);
    }
}

} // namespace strategy
