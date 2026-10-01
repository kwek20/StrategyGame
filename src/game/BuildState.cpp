#include "game/BuildState.hpp"

#include "diagnostics/Logger.hpp"
#include "localization/Text.hpp"
#include "persistence/SaveGame.hpp"
#include "render/Renderer.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/WorldGeneration.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace strategy {
namespace {
std::string escapeRml(std::string value) {
    const std::pair<std::string_view, std::string_view> replacements[]{
        {"&", "&amp;"}, {"<", "&lt;"}, {">", "&gt;"}, {"\"", "&quot;"}};
    for (const auto& [from, to] : replacements) {
        std::size_t position = 0;
        while ((position = value.find(from, position)) != std::string::npos) {
            value.replace(position, from.size(), to);
            position += to.size();
        }
    }
    return value;
}
} // namespace

BuildState::BuildState(StateContext& context, std::uint32_t terrainSeed)
    : GameState(context)
    , terrainSeed_(terrainSeed)
    , config_(GameConfig::load(context.configPath))
    , gameplay_(context.definitions)
    , status_(Text::get("status.ready")) {
    if (!context_.renderer)
        throw std::runtime_error("Build mode requires a renderer");
    const Terrain terrain{terrainSeed};
    (void)populateResources(world_, terrain, gameplay_, terrainSeed);

    std::vector<std::string> controls;
    for (const std::string& id : gameplay_.matchRules().buildPalette)
        if (gameplay_.archetype(id))
            controls.push_back("build.palette." + id);
    controls.insert(controls.end(), {"build.team", "build.undo", "build.save", "build.exit"});
    screen_ = context_.renderer->pushUiScreen({"assets/ui/build_menu.rml",
                                               {{"build-title", Text::get("build.title")},
                                                {"team-label", Text::get("build.change_team")},
                                                {"undo-label", Text::get("build.undo")},
                                                {"save-label", Text::get("build.save")},
                                                {"exit-label", Text::get("build.exit")}},
                                               controls,
                                               controls,
                                               config_.uiScale});
    context_.renderer->setUiText("build-palette", paletteMarkup());
    context_.renderer->focusUi(0);
    refreshMenu();
}

BuildState::~BuildState() {
    context_.renderer->removeUiScreen(screen_);
}

std::string BuildState::paletteMarkup() const {
    std::string markup;
    for (const std::string& id : gameplay_.matchRules().buildPalette) {
        const EntityArchetype* type = gameplay_.archetype(id);
        if (!type)
            continue;
        const std::string icon = gameplay_.presentationIcon(PresentationId{type->presentation});
        markup += "<button id=\"build.palette." + escapeRml(id) + "\" class=\"build-slot\">";
        markup += "<img src=\"../icons/source/" + escapeRml(icon) + ".png\"/>";
        markup += "<span>" + escapeRml(Text::get(type->nameKey)) + "</span></button>";
    }
    return markup;
}

void BuildState::refreshMenu() const {
    const auto& palette = gameplay_.matchRules().buildPalette;
    context_.renderer->setUiText(
        "build-team",
        Text::format("status.team", {Text::get(team_ == 1 ? "build.team.a" : "build.team.b")}));
    context_.renderer->setUiText("build-status", status_);
    for (std::size_t index = 0; index < palette.size(); ++index)
        context_.renderer->setUiAttribute("build.palette." + palette[index],
                                          "class",
                                          index == paletteIndex_ ? "build-slot selected"
                                                                 : "build-slot");
}

void BuildState::activateMenu(std::string_view id) {
    const auto& palette = gameplay_.matchRules().buildPalette;
    if (id.starts_with("build.palette.")) {
        const std::string archetype{id.substr(std::string_view{"build.palette."}.size())};
        const auto found = std::find(palette.begin(), palette.end(), archetype);
        if (found != palette.end()) {
            paletteIndex_ = static_cast<std::size_t>(found - palette.begin());
            if (const EntityArchetype* type = gameplay_.archetype(archetype))
                status_ = Text::format("status.selected", {Text::get(type->nameKey)});
        }
    } else if (id == "build.team") {
        team_ = team_ == 1 ? 2 : 1;
        status_ =
            Text::format("status.team", {Text::get(team_ == 1 ? "build.team.a" : "build.team.b")});
    } else if (id == "build.undo") {
        if (!world_.entities().empty()) {
            world_.destroyEntity(world_.entities().back().id);
            status_ = Text::get("status.undo");
        }
    } else if (id == "build.save") {
        try {
            SaveGame::write(config_.savePath(), terrainSeed_, world_);
            status_ = Text::format("status.saved", {config_.saveFile});
            context_.logger.info("persistence", "Saved map to " + config_.savePath().string());
        } catch (const std::exception& error) {
            status_ = Text::get("status.save_failed");
            context_.logger.error("persistence", error.what());
        }
    } else if (id == "build.exit") {
        request_ = StateRequest::returnToMainMenu;
    }
    refreshMenu();
}

void BuildState::handleEvent(const SDL_Event& event) {
    context_.renderer->handleUiEvent(event);
    if (const auto action = context_.renderer->takeUiAction()) {
        activateMenu(*action);
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_ESCAPE) {
            activateMenu("build.exit");
            return;
        }
        if (event.key.key == SDLK_T) {
            activateMenu("build.team");
            return;
        }
        if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
            const std::size_t index = static_cast<std::size_t>(event.key.key - SDLK_1);
            if (index < gameplay_.matchRules().buildPalette.size())
                activateMenu("build.palette." + gameplay_.matchRules().buildPalette[index]);
            return;
        }
        if (event.key.key == SDLK_BACKSPACE) {
            activateMenu("build.undo");
            return;
        }
        if (event.key.key == SDLK_F5) {
            activateMenu("build.save");
            return;
        }
        if (event.key.key == SDLK_TAB) {
            context_.renderer->focusUi((SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
            return;
        }
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
            context_.renderer->activateFocusedUiItem();
            if (const auto action = context_.renderer->takeUiAction())
                activateMenu(*action);
            return;
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
            context_.renderer->focusUi(1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
            context_.renderer->focusUi(-1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
            context_.renderer->activateFocusedUiItem();
            if (const auto action = context_.renderer->takeUiAction())
                activateMenu(*action);
        } else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            activateMenu("build.exit");
        return;
    }
    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.button == SDL_BUTTON_LEFT && context_.renderer->pointerOverUi())
        return;
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
        if (!context_.renderer->pointerOverUi())
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
    case SDLK_LSHIFT:
        leftShift_ = pressed;
        acceleratedCamera_ = leftShift_ || rightShift_;
        break;
    case SDLK_RSHIFT:
        rightShift_ = pressed;
        acceleratedCamera_ = leftShift_ || rightShift_;
        break;
    default:
        break;
    }
}

void BuildState::update(float deltaSeconds) {
    camera_.pan(float(forward_) - float(backward_),
                float(right_) - float(left_),
                deltaSeconds,
                acceleratedCamera_);
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
        TerrainPlacementResult terrainPlacement;
        if (type->kind == EntityKind::building)
            terrainPlacement =
                renderer.evaluateTerrainPlacement(position.x, position.z, shape, type->placement);
        else {
            terrainPlacement.fit =
                renderer.fitTerrainFootprint(position.x, position.z, radius, 90.0F);
            if (!terrainPlacement.fit.valid)
                terrainPlacement.failure = TerrainPlacementFailure::excessiveSlope;
        }
        const FootprintFit& footprint = terrainPlacement.fit;
        const SpatialShape placementShape = spatialShape(
            gameplay_, EntityArchetypeId{entityType}, {position.x, position.z}, entityRotation);
        if (!terrainPlacement.valid() || overlapsObject(world_, gameplay_, placementShape)) {
            status_ = Text::get("status.blocked");
        } else {
            Entity& entity = world_.createEntity(Text::get(type->nameKey), entityType, team_);
            gameplay_.initializeEntity(entity);
            entity.transform.position = {position.x, 0.0F, position.z};
            entity.transform.rotationDegrees.y = entityRotation;
            if (entity.kind == EntityKind::building) {
                world_.foundations().push_back(
                    TerrainFoundation{entity.id,
                                      {position.x, footprint.height, position.z},
                                      shape,
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
    refreshMenu();
    renderer.drawRmlUi();
}
} // namespace strategy
