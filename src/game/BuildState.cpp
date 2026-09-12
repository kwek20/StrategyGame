#include "game/BuildState.hpp"

#include "localization/Text.hpp"
#include "diagnostics/Logger.hpp"
#include "persistence/SaveGame.hpp"
#include "render/Renderer.hpp"
#include "terrain/Terrain.hpp"
#include "ui/EntityHudModel.hpp"
#include "ui/EntityHudLayout.hpp"
#include "world/Collision.hpp"
#include "world/WorldGeneration.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <iostream>
namespace strategy {
BuildState::BuildState(StateContext& context, std::uint32_t terrainSeed)
    : GameState(context)
    , terrainSeed_(terrainSeed)
    , config_(GameConfig::load(context.configPath))
    , gameplay_(context.definitions)
    , status_(Text::get("status.ready")) {
    const Terrain terrain{terrainSeed};
    populateResources(world_, terrain, gameplay_, terrainSeed);
}

EntityHudModel BuildState::paletteHud() const {
    EntityHudModel hud;
    for (const std::string& id : gameplay_.matchRules().buildPalette) {
        const EntityArchetype* type = gameplay_.archetype(id);
        if (!type) continue;
        hud.actions.push_back({id,
            gameplay_.presentationIcon(PresentationId{type->presentation}),
            Text::get(type->nameKey), status_, {}, {}, true,
            id == gameplay_.matchRules().buildPalette.at(paletteIndex_)});
    }
    return hud;
}

void BuildState::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_ESCAPE) {
            request_ = StateRequest::returnToMainMenu;
            return;
        }
        if (event.key.key == SDLK_T) {
            team_ = team_ == 1 ? 2 : 1;
            status_ = Text::format("status.team",
                                   {Text::get(team_ == 1 ? "build.team.a" : "build.team.b")});
            return;
        }
        if (event.key.key == SDLK_1) {
            paletteIndex_ = 0;
            const auto* type = gameplay_.archetype(gameplay_.matchRules().buildPalette.at(0));
            status_ = Text::format("status.selected", {Text::get(type->nameKey)});
            return;
        }
        if (event.key.key == SDLK_2 && gameplay_.matchRules().buildPalette.size() > 1) {
            paletteIndex_ = 1;
            const auto* type = gameplay_.archetype(gameplay_.matchRules().buildPalette.at(1));
            status_ = Text::format("status.selected", {Text::get(type->nameKey)});
            return;
        }
        if (event.key.key == SDLK_BACKSPACE && !world_.entities().empty()) {
            world_.destroyEntity(world_.entities().back().id);
            status_ = Text::get("status.undo");
            return;
        }
        if (event.key.key == SDLK_F5) {
            try {
                SaveGame::write(config_.savePath(), terrainSeed_, world_);
                status_ = Text::format("status.saved", {config_.saveFile});
                context_.logger.info("persistence",
                                     "Saved map to " + config_.savePath().string());
            } catch (const std::exception& error) {
                status_ = Text::get("status.save_failed");
                context_.logger.error("persistence", error.what());
            }
            return;
        }
        if (event.key.key == SDLK_TAB) {
            EntityHudModel hud = paletteHud();
            UiDocument layout = EntityHudLayout::actions(
                hud, config_.resolutionWidth, config_.resolutionHeight, config_.uiScale);
            uiController_.moveFocus(layout,
                (SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
            return;
        }
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
            EntityHudModel hud = paletteHud();
            UiDocument layout = EntityHudLayout::actions(
                hud, config_.resolutionWidth, config_.resolutionHeight, config_.uiScale);
            if (const auto element = uiController_.activateFocused(layout))
                if (const auto id = EntityHudLayout::actionId(*element)) {
                    const auto& palette = gameplay_.matchRules().buildPalette;
                    const auto found = std::find(palette.begin(), palette.end(), *id);
                    if (found != palette.end())
                        paletteIndex_ = static_cast<std::size_t>(found - palette.begin());
                }
            return;
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        EntityHudModel hud = paletteHud();
        UiDocument layout = EntityHudLayout::actions(
            hud, config_.resolutionWidth, config_.resolutionHeight, config_.uiScale);
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
            uiController_.moveFocus(layout, 1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
            uiController_.moveFocus(layout, -1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            if (const auto element = uiController_.activateFocused(layout))
                if (const auto id = EntityHudLayout::actionId(*element)) {
                    const auto& palette = gameplay_.matchRules().buildPalette;
                    const auto found = std::find(palette.begin(), palette.end(), *id);
                    if (found != palette.end())
                        paletteIndex_ = static_cast<std::size_t>(found - palette.begin());
                }
        } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            request_ = StateRequest::returnToMainMenu;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        int width = 0, height = 0;
        if (SDL_Window* window = SDL_GetWindowFromID(event.button.windowID))
            SDL_GetWindowSize(window, &width, &height);
        EntityHudModel hud = paletteHud();
        UiDocument layout = EntityHudLayout::actions(hud, width, height, config_.uiScale);
        const glm::vec2 point{event.button.x, event.button.y};
        if (const auto activated = uiController_.press(layout, point)) {
            if (const auto id = EntityHudLayout::actionId(*activated)) {
                const auto& palette = gameplay_.matchRules().buildPalette;
                const auto found = std::find(palette.begin(), palette.end(), *id);
                if (found != palette.end()) {
                    paletteIndex_ = static_cast<std::size_t>(found - palette.begin());
                    const EntityArchetype* type = gameplay_.archetype(*id);
                    status_ = Text::format("status.selected", {Text::get(type->nameKey)});
                }
                return;
            }
        }
        if (layout.find("entity.panel")->bounds.contains(point)) return;
        pendingPlacement_ = glm::vec2{event.button.x, event.button.y};
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_MIDDLE) {
        orbiting_ = true;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_MIDDLE) {
        orbiting_ = false;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
        mousePanning_ = true;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
        mousePanning_ = false;
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && orbiting_) {
        camera_.orbit(event.motion.xrel, event.motion.yrel);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && mousePanning_) {
        camera_.dragPan(event.motion.xrel, event.motion.yrel);
        camera_.orbit(event.motion.xrel, event.motion.yrel);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        uiController_.pointerMoved({event.motion.x, event.motion.y});
        hoverPosition_ = glm::vec2{event.motion.x, event.motion.y};
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        camera_.zoom(event.wheel.y);
        return;
    }
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
        return;
    const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
    switch (event.key.key) {
    case SDLK_W:
        forward_ = pressed;
        break;
    case SDLK_S:
        backward_ = pressed;
        break;
    case SDLK_A:
        left_ = pressed;
        break;
    case SDLK_D:
        right_ = pressed;
        break;
    default:
        break;
    }
}
void BuildState::update(float deltaSeconds) {
    uiController_.advance(deltaSeconds);
    camera_.pan(float(forward_) - float(backward_), float(right_) - float(left_), deltaSeconds);
}
void BuildState::render(Renderer& renderer) const {
    const glm::vec3 focus = camera_.focus();
    const CameraView view =
        camera_.view(renderer.aspectRatio(), renderer.terrainHeightAt(focus.x, focus.z));
    if (pendingPlacement_) {
        const std::string& entityType = gameplay_.matchRules().buildPalette.at(paletteIndex_);
        const EntityArchetype* type = gameplay_.archetype(entityType);
        const glm::vec3 position =
            renderer.screenToTerrain(pendingPlacement_->x, pendingPlacement_->y, view);
        const float radius = collisionRadius(gameplay_, entityType);
        TerrainFootprint shape{FootprintShape::circle, radius, {radius, radius}};
        if (type->kind == EntityKind::building && type->footprint)
            shape = *type->footprint;
        const float entityRotation = team_ == 2 ? 180.0F : 0.0F;
        shape.rotationDegrees += entityRotation;
        const FootprintFit footprint = type->kind == EntityKind::building
                                           ? renderer.fitTerrainFootprint(position.x, position.z, shape)
                                           : renderer.fitTerrainFootprint(position.x, position.z, radius, 90.0F);
        const SpatialShape placementShape = spatialShape(
            gameplay_, EntityArchetypeId{entityType}, {position.x, position.z},
            entityRotation);
        if (!footprint.valid || overlapsObject(world_, gameplay_, placementShape)) {
            status_ = Text::get("status.blocked");
        } else {
            Entity& entity =
                world_.createEntity(Text::get(type->nameKey), entityType, team_);
            gameplay_.initializeEntity(entity);
            entity.transform.position = {position.x, 0.0F, position.z};
            entity.transform.rotationDegrees.y = entityRotation;
            if (entity.kind == EntityKind::building) {
                world_.foundations().push_back(TerrainFoundation{
                    entity.id, {position.x, footprint.height, position.z}, shape,
                    footprint.gradient});
                renderer.setTerrainFoundations(world_.foundations());
            }
            if (entity.unitControl)
                entity.unitControl.directlyControllable = true;
            status_ = Text::format("status.placed", {entity.name});
        }
        pendingPlacement_.reset();
    }
    if (hoverPosition_) {
        hoveredEntity_ = renderer.pickEntity(hoverPosition_->x, hoverPosition_->y, world_, view);
        hoverPosition_.reset();
    }
    renderer.setTerrainFoundations(world_.foundations());
    renderer.drawTerrain(view);
    renderer.drawWorld(world_, view);
    if (hoveredEntity_ != 0)
        renderer.drawEntityOutline(world_, hoveredEntity_, view);
    EntityHudModel hud = paletteHud();
    UiDocument layout = EntityHudLayout::actions(
        hud, renderer.viewportWidth(), renderer.viewportHeight(), config_.uiScale);
    uiController_.apply(layout);
    renderer.drawEntityHud(hud, layout);
}
} // namespace strategy
