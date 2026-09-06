#include "game/BuildState.hpp"

#include "localization/Text.hpp"
#include "diagnostics/Logger.hpp"
#include "persistence/SaveGame.hpp"
#include "render/Renderer.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/WorldGeneration.hpp"

#include <SDL3/SDL.h>
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
            entityType_ = "worker";
            status_ = Text::format("status.selected", {Text::get("entity.worker")});
            return;
        }
        if (event.key.key == SDLK_2) {
            entityType_ = "town_center";
            status_ = Text::format("status.selected", {Text::get("entity.town_center")});
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
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
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
    camera_.pan(float(forward_) - float(backward_), float(right_) - float(left_), deltaSeconds);
}
void BuildState::render(Renderer& renderer) const {
    const glm::vec3 focus = camera_.focus();
    const CameraView view =
        camera_.view(renderer.aspectRatio(), renderer.terrainHeightAt(focus.x, focus.z));
    if (pendingPlacement_) {
        const glm::vec3 position =
            renderer.screenToTerrain(pendingPlacement_->x, pendingPlacement_->y, view);
        if (overlapsObject(world_,
                           gameplay_,
                           {position.x, position.z},
                           collisionRadius(gameplay_, entityType_))) {
            status_ = Text::get("status.blocked");
        } else {
            Entity& entity = world_.createEntity(
                Text::get(entityType_ == "worker" ? "entity.worker" : "entity.town_center"),
                entityType_,
                team_);
            gameplay_.initializeEntity(entity);
            entity.transform.position = {position.x, 0.0F, position.z};
            entity.transform.rotationDegrees.y = team_ == 2 ? 180.0F : 0.0F;
            entity.unitControl.directlyControllable = entityType_ == "worker";
            status_ = Text::format("status.placed", {entity.name});
        }
        pendingPlacement_.reset();
    }
    if (hoverPosition_) {
        hoveredEntity_ = renderer.pickEntity(hoverPosition_->x, hoverPosition_->y, world_, view);
        hoverPosition_.reset();
    }
    renderer.drawTerrain(view);
    renderer.drawWorld(world_, view);
    if (hoveredEntity_ != 0)
        renderer.drawEntityOutline(world_, hoveredEntity_, view);
    renderer.drawBuildHud(team_, entityType_, world_.size(), status_);
}
} // namespace strategy
