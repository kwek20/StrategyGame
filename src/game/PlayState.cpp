#include "game/PlayState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "diagnostics/Logger.hpp"
#include "localization/Text.hpp"
#include "persistence/SaveGame.hpp"
#include "render/Renderer.hpp"
#include "terrain/Terrain.hpp"
#include "ui/EntityHudModel.hpp"
#include "ui/PlacementFeedback.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"
#include "world/PowerPlacement.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>

namespace strategy {
namespace {

std::string displayNumber(float value) {
    std::ostringstream result;
    result << value;
    return result.str();
}

std::string escapeRml(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else if (c == '\"') result += "&quot;";
        else result += c;
    }
    return result;
}

std::string hudIcon(std::string_view icon) {
    return "../icons/source/" + escapeRml(icon) + ".png";
}

float barRatio(float current, float maximum) {
    return maximum <= 0.0F || !std::isfinite(current) || !std::isfinite(maximum)
               ? 0.0F : std::clamp(current / maximum, 0.0F, 1.0F);
}

int barPercent(float current, float maximum) {
    return static_cast<int>(barRatio(current, maximum) * 100.0F);
}

std::string resourceCost(const DefinitionRegistry& definitions,
                         const RecipeDefinition* recipe,
                         std::size_t count = 1,
                         std::vector<std::string>* icons = nullptr) {
    std::string result;
    if (recipe) {
        const std::map<std::string, float> sorted(recipe->cost.begin(), recipe->cost.end());
        for (const auto& [id, amount] : sorted) {
            if (amount == 0.0F || count == 0)
                continue;
            if (!result.empty())
                result += ", ";
            const auto* resource = definitions.resourceType(ResourceId{id});
            if (icons)
                icons->push_back(resource ? resource->icon : "resource_" + id);
            result += displayNumber(amount * static_cast<float>(count)) + " " +
                      (resource ? Text::get(resource->nameKey) : id);
        }
    }
    return result;
}

std::string upgradeNames(const DefinitionRegistry& definitions,
                         const std::vector<std::string>& ids) {
    std::string result;
    for (const auto& id : ids) {
        if (!result.empty())
            result += ", ";
        const auto* upgrade = definitions.upgrade(id);
        result += upgrade ? Text::get(upgrade->nameKey) : id;
    }
    return result.empty() ? Text::get("entity_hud.tooltip_none") : result;
}

std::string powerDescription(const DefinitionRegistry& definitions, const EntityArchetype* entity) {
    const auto* power =
        entity && entity->powerDevice ? definitions.powerDevice(*entity->powerDevice) : nullptr;
    return Text::format("entity_hud.tooltip_power",
                        {displayNumber(power ? power->consumption : 0.0F)});
}

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

const char* powerPriorityName(PowerPriority priority) {
    switch (priority) {
    case PowerPriority::low:
        return "LOW";
    case PowerPriority::medium:
        return "MEDIUM";
    case PowerPriority::high:
        return "HIGH";
    }
    return "MEDIUM";
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
        if (!entity || !entity->gatherer)
            continue;
        if (entity->gatherer.carriedResource == "synthetic")
            synthetic = true;
        if (const Entity* source = world.findEntity(entity->gatherer.sourceTarget);
            source && source->resource && source->resource.type == "synthetic")
            synthetic = true;
    }
    if (!synthetic)
        return;
    for (const std::string output : {std::string{"alloy"}, std::string{"fuel"}}) {
        float bestRatio = 0.0F;
        bool available = false;
        for (const Entity& processor : world.entities()) {
            if (processor.authority.owner != player || !processor.processor ||
                !isOperational(processor))
                continue;
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
        if (!available)
            action.disabledReason = Text::get("entity_hud.no_compatible_processor");
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
               TerrainLayoutId{std::move(setup.terrainLayout)},
               nullptr,
               setup.terrainMode)
    , config_(GameConfig::load(context.configPath)) {
    initializeStartingView();
}

PlayState::PlayState(StateContext& context, MatchSetupOptions setup, GameSession preparedSession)
    : GameState(context)
    , session_(std::move(preparedSession))
    , config_(GameConfig::load(context.configPath)) {
    (void)setup;
    initializeStartingView();
}

void PlayState::beginPresentationPreparation(Renderer& renderer) {
    preparation_ = Preparation::terrain;
    preparationProgress_ = 0;
    renderer.stageGeneratedTerrain(session_.terrain(),
                                   session_.terrainSeed(),
                                   session_.mapChunksPerSide(),
                                   session_.world().foundations());
    pendingTerrainSeed_.reset();
    pendingTerrainChunksPerSide_.reset();
    pendingTerrainLayout_.reset();
}

bool PlayState::advancePresentationPreparation(Renderer& renderer) {
    const Player* local = session_.players().find(localPlayer_);
    const auto focus = camera_.focus();
    const auto view =
        camera_.view(renderer.aspectRatio(), session_.terrain().heightAt(focus.x, focus.z));
    switch (preparation_) {
    case Preparation::terrain:
        preparationProgress_ = renderer.terrainUploadProgress() * .2F;
        if (!renderer.terrainUploadFinished())
            break;
        renderer.beginResourceSitePreparation(session_.world());
        preparation_ = Preparation::resources;
        break;
    case Preparation::resources:
        if (renderer.advanceResourceSitePreparation())
            preparation_ = Preparation::vegetation;
        preparationProgress_ = .2F + renderer.resourceSitePreparationProgress() * .55F;
        break;
    case Preparation::vegetation:
        renderer.drawVegetation(session_.vegetation(), view, local);
        preparationProgress_ = .8F;
        preparation_ = Preparation::world;
        break;
    case Preparation::world:
        renderer.drawWorld(session_.world(), view, local);
        preparationProgress_ = .85F;
        preparation_ = Preparation::hud;
        break;
    case Preparation::hud:
        ensureGameplayHud(renderer);
        renderer.activateUiScreen(hudScreen_);
        updateGameplayHud(renderer, local);
        renderer.drawRmlUi();
        preparationProgress_ = .9F;
        preparation_ = Preparation::frame;
        break;
    case Preparation::frame:
        renderer.activateUiScreen(hudScreen_);
        render(renderer);
        preparationProgress_ = .95F;
        preparation_ = Preparation::fence;
        break;
    case Preparation::fence:
        renderer.beginPresentationFence();
        preparation_ = Preparation::gpu;
        break;
    case Preparation::gpu:
        if (renderer.presentationFenceReady()) {
            preparationProgress_ = 1;
            preparation_ = Preparation::ready;
        }
        break;
    case Preparation::ready:
        break;
    }
    return preparation_ == Preparation::ready;
}

void PlayState::initializeStartingView() {
    for (const Entity& entity : session_.world().entities()) {
        if (entity.authority.owner != localPlayer_)
            continue;
        if (entity.archetype.value == "command_hub")
            camera_.focusAt(entity.transform.position);
        if (entity.archetype.value == "construction_drone")
            selectedEntity_ = entity.id;
    }
}

PlayState::PlayState(StateContext& context, SaveData data, WorldGenerationProgress* progress)
    : GameState(context)
    , session_(context.definitions,
               data.terrainSeed,
               data.playerOneCountry,
               data.playerTwoCountry,
               data.playerOneSpecialization,
               data.playerTwoSpecialization,
               data.mapChunksPerSide,
               1.0F,
               1.0F,
               TerrainLayoutId{data.terrainLayout},
               progress,
               data.terrainMode)
    , config_(GameConfig::load(context.configPath)) {
    session_.replaceWorld(std::move(data.entities),
                          data.terrainSeed,
                          std::move(data.foundations),
                          data.mapChunksPerSide,
                          TerrainLayoutId{data.terrainLayout},
                          data.terrainMode,
                          data.terrainRepairLevel,
                          progress);
    restoreSavedPlayerProgress(data);
}

void PlayState::restoreSavedPlayerProgress(SaveData& data) {
    for (std::size_t index = 0; index < data.resources.size(); ++index) {
        const PlayerId id = index + 1;
        if (auto* player = session_.players().find(id)) {
            player->countryId = index == 0 ? data.playerOneCountry : data.playerTwoCountry;
            player->specializationId = index == 0 ? data.playerOneSpecialization : data.playerTwoSpecialization;
        }
        session_.restorePlayerProgress(id, std::move(data.resources[index]),
                                       std::move(data.discovered[index]),
                                       std::move(data.intelligence[index]),
                                       std::move(data.researchedUpgrades[index]));
    }
}

PlayState::~PlayState() {
    if (pauseScreen_)
        context_.renderer->removeUiScreen(pauseScreen_);
    if (hudScreen_)
        context_.renderer->removeUiScreen(hudScreen_);
}

void PlayState::ensureGameplayHud(Renderer& renderer) const {
    if (hudScreen_)
        return;
    hudScreen_ = renderer.pushUiScreen({"assets/ui/gameplay_hud.rml",
                                        {},
                                        {"resources.power", "hud.satellite", "hud.menu"},
                                        {"resources.power", "hud.satellite", "hud.menu"},
                                        config_.uiScale});
}

void PlayState::updateGameplayHud(Renderer& renderer, const Player* player) const {
    ensureGameplayHud(renderer);
    std::vector<std::string> focus{"resources.power", "hud.satellite", "hud.menu"};
    std::vector<std::string> actions = focus;
    std::ostringstream resources;
    if (player) {
        for (const ResourceDefinition* resource : context_.definitions.enabledResources()) {
            if (resource->storage != ResourceStorageKind::stockpile)
                continue;
            const auto found = player->resources.find(resource->id);
            const float amount = found == player->resources.end() ? 0.0F : found->second;
            resources << "<div class='resource'><img src='" << hudIcon(resource->icon)
                      << "'/><span>" << static_cast<unsigned>(amount) << "</span></div>";
        }
    }
    float generation = 0.0F, demand = 0.0F, supplied = 0.0F, stored = 0.0F, capacity = 0.0F;
    std::size_t devices = 0;
    std::vector<std::string> deviceLines;
    for (const Entity& entity : session_.world().entities()) {
        if (!player || entity.authority.owner != player->id || !entity.power)
            continue;
        const EntityArchetype* archetype = context_.definitions.archetype(entity.archetype);
        if (!archetype || !archetype->powerDevice)
            continue;
        ++devices;
        generation += entity.power->enabled ? entity.power->generation : 0.0F;
        demand += entity.power->enabled && entity.power->consumptionEnabled ? entity.power->demand : 0.0F;
        supplied += entity.power->enabled ? entity.power->supplied : 0.0F;
        stored += entity.power->stored;
        capacity += entity.power->storageCapacity;
        std::ostringstream line;
        line << escapeRml(entity.name) << "  " << static_cast<int>(entity.power->supplied) << "/"
             << static_cast<int>(entity.power->demand) << " kW  "
             << powerPriorityName(entity.power->priority) << "  GRID " << entity.power->gridId;
        deviceLines.push_back(line.str());
    }
    renderer.setUiText("stockpile-resources", resources.str());
    renderer.setUiText("resources.power.label",
                       std::to_string(static_cast<int>(demand)) + "/" +
                           std::to_string(static_cast<int>(generation)) + " kW");
    const auto coverage = session_.satelliteCoverage(localPlayer_);
    renderer.setUiText("hud.satellite.label",
                       Text::get(coverage == SatelliteCoverage::highResolution ? "hud.satellite.high"
                                 : coverage == SatelliteCoverage::lowResolution ? "hud.satellite.low"
                                                                              : "hud.satellite"));

    std::ostringstream power;
    power << "<h2>POWER GRID</h2><div class='power-summary'>SUPPLIED " << static_cast<int>(supplied)
          << " / DEMAND " << static_cast<int>(demand) << " kW &nbsp; CAPACITY "
          << static_cast<int>(generation) << " kW</div><div class='power-summary'>STORAGE "
          << static_cast<int>(stored) << " / " << static_cast<int>(capacity) << " kWh &nbsp; DEVICES "
          << devices << "</div>";
    for (const auto& line : deviceLines)
        power << "<div class='power-device'>" << line << "</div>";
    renderer.setUiText("power-content", power.str());
    renderer.setUiAttribute("power-panel", "class", powerOverlayVisible_ ? "" : "hidden");

    std::ostringstream entity;
    std::ostringstream entityInfo;
    std::ostringstream entityQueue;
    std::optional<EntityHudModel> hud;
    const Entity* selected = session_.world().findEntity(possessedEntity_ ? possessedEntity_ : selectedEntity_);
    const bool sameType = selected && isHomogeneousSelection(session_.world(), selectedUnits_, selected->archetype.value);
    if (viewMode_ == ViewMode::unitControl && selected) {
        hud = EntityHudModelBuilder::build(session_.world(), selected->id, {}, context_.definitions);
        hud->footer =
            Text::format("keybind.direct_hint",
                         {config_.keybinds.label("possess"), config_.keybinds.label("pause")});
    } else if (selectedResourceField_ && selected) {
        hud = EntityHudModelBuilder::build(
            session_.world(), selectedEntity_, selectedUnits_, context_.definitions, true);
    } else if (!selectedUnits_.empty() && !sameType) {
        hud = EntityHudModelBuilder::build(session_.world(), selectedEntity_, selectedUnits_, context_.definitions);
    } else if (selected && selected->authority.owner == localPlayer_) {
        hud = selected->archetype.value == "construction_drone" ? buildConstructionHudModel(*selected, player)
                                                                  : buildEntityActionHudModel(*selected, player);
    } else if (selected && selected->resource) {
        hud = EntityHudModelBuilder::build(session_.world(), selectedEntity_, selectedUnits_, context_.definitions);
    }
    hudShortcuts_.clear();
    if (hud) {
        entityInfo << "<div class='entity-title'>" << escapeRml(hud->title)
                   << "</div><div class='entity-main'><img class='portrait' src='" << hudIcon(hud->portraitIcon)
                   << "'/><div class='bars-stats'>";
        for (const HudBarModel& bar : hud->bars) {
            const char* kind = bar.kind == HudBarKind::health ? "health" : bar.kind == HudBarKind::power ? "power" : "construction";
            entityInfo << "<div class='hud-bar progress-track'><progress class='progress-fill " << kind
                       << "' value='" << barRatio(bar.current, bar.maximum)
                       << "' max='1' direction='right'/><span class='progress-label'>" << escapeRml(bar.label)
                       << " " << displayNumber(bar.current) << " / " << displayNumber(bar.maximum) << "</span></div>";
        }
        entityInfo << "</div></div><div class='stats-list'>";
        for (const HudStatModel& stat : hud->stats)
            entityInfo << "<div class='stat'><span class='stat-label'>" << escapeRml(stat.label)
                       << "</span><span class='stat-value'>" << escapeRml(stat.value) << "</span></div>";
        entityInfo << "</div>";
        if (!hud->footer.empty())
            entityInfo << "<div class='footer'>" << escapeRml(hud->footer) << "</div>";
        std::size_t actionColumn = 0;
        const auto beginAction = [&]() {
            if (actionColumn == 0)
                entity << "<div class='action-row'>";
        };
        const auto endAction = [&]() {
            ++actionColumn;
            if (actionColumn == 4) {
                entity << "</div>";
                actionColumn = 0;
            }
        };
        for (const HudSelectionGroupModel& group : hud->selectionGroups) {
            const std::string id = "selection." + group.archetype;
            focus.push_back(id); actions.push_back(id);
            beginAction();
            entity << "<button id='" << escapeRml(id) << "' class='selection-group'><img src='"
                   << hudIcon(group.icon) << "'/><span class='selection-count'>" << group.count
                   << "</span><span class='action-name'>" << escapeRml(group.name) << "</span></button>";
            endAction();
        }
        if (actionColumn != 0) { entity << "</div>"; actionColumn = 0; }
        for (const bool construction : {false, true}) {
            const bool hasGroup = std::any_of(hud->actions.begin(), hud->actions.end(), [&](const auto& action) {
                return action.id.starts_with("construct.") == construction;
            });
            if (!hasGroup) continue;
            entity << "<div class='action-heading'>" << Text::get(construction ? "entity_hud.group_construction" : "entity_hud.group_commands") << "</div>";
            for (const HudActionModel& action : hud->actions) {
                if (action.id.starts_with("construct.") != construction) continue;
                const std::string id = "action." + action.id;
                if (action.enabled) { focus.push_back(id); actions.push_back(id); }
                beginAction();
                entity << "<button id='" << escapeRml(id) << "' class='action"
                       << (action.active ? " active" : "") << (action.enabled ? "" : " unavailable") << "'"
                       << "><img src='" << hudIcon(action.icon) << "'/><span class='action-name'>" << escapeRml(action.name) << "</span>";
                if (hudShortcuts_.size() < 12) {
                    const auto index = hudShortcuts_.size();
                    hudShortcuts_.push_back(action.enabled ? id : std::string{});
                    entity << "<span class='action-hotkey'>"
                           << escapeRml(
                                  config_.keybinds.label("action_" + std::to_string(index + 1)))
                           << "</span>";
                }
                entity << "<div class='tooltip'><b>"
                       << escapeRml(action.name) << "</b><br/>" << escapeRml(action.description);
                if (!action.cost.empty() && action.cost != Text::get("entity_hud.free")) entity << "<br/>COST: " << escapeRml(action.cost);
                if (!action.power.empty()) entity << "<br/>" << escapeRml(action.power);
                if (!action.requirements.empty()) entity << "<br/>" << escapeRml(action.requirements);
                if (!action.disabledReason.empty()) entity << "<br/>" << escapeRml(action.disabledReason);
                entity << "</div></button>";
                endAction();
            }
            if (actionColumn != 0) { entity << "</div>"; actionColumn = 0; }
        }
        if (!hud->queue.empty()) {
            entityQueue << "<div class='queue'><h3>QUEUE</h3>";
            for (std::size_t i = 0; i < hud->queue.size(); ++i) {
                const std::string id = "queue." + std::to_string(i);
                if (hud->queue[i].cancellable) { focus.push_back(id); actions.push_back(id); }
                entityQueue << "<button id='" << id << "'" << (hud->queue[i].cancellable ? "" : " disabled='disabled'")
                            << "><img src='" << hudIcon(hud->queue[i].icon) << "'/><span>"
                            << escapeRml(hud->queue[i].name) << " " << barPercent(hud->queue[i].progress, 1.0F)
                            << "%</span></button>";
            }
            entityQueue << "</div>";
        }
    }
    renderer.setUiText("entity-actions", entity.str());
    renderer.setUiText("entity-info", entityInfo.str());
    const EntityId hudEntity = hud && selected ? selected->id : 0;
    if (hudSelectedEntity_ != hudEntity || hudSelectedUnits_ != selectedUnits_) {
        renderer.resetUiScroll("entity-actions");
        renderer.resetUiScroll("entity-info");
        hudSelectedEntity_ = hudEntity;
        hudSelectedUnits_ = selectedUnits_;
    }
    renderer.setUiText("entity-queue", entityQueue.str());
    renderer.setUiAttribute("entity-shell", "class", !hud ? "hidden" :
        hud->actions.empty() && hud->selectionGroups.empty() ? "info-only" : "");
    renderer.setUiProperty("minimap-shell", "display", viewMode_ == ViewMode::unitControl ? "none" : "block");

    std::ostringstream alerts;
    for (const HudAlert& alert : hudAlerts_) alerts << "<div class='alert'>" << escapeRml(alert.text) << "</div>";
    renderer.setUiText("alerts", alerts.str());
    std::string placementMessage = pendingPlacementSequence_ ? Text::get("placement.confirming")
                                   : placementRejectionRemaining_ > 0 ? placementRejection_
                                   : !constructionPreviewValid_       ? constructionPreviewReason_
                                                                : Text::get("placement.ready");
    if (!constructionPreviewSummary_.empty())
        placementMessage += "\n" + constructionPreviewSummary_;
    placementMessage += "\n" + Text::format("placement.controls",
                                            {config_.keybinds.label("place"),
                                             config_.keybinds.label("repeat_build"),
                                             config_.keybinds.label("rotate_building"),
                                             config_.keybinds.label("cancel_placement"),
                                             config_.keybinds.label("camera_pan")});
    renderer.setUiText("placement-reason", escapeRml(placementMessage));
    renderer.setUiAttribute(
        "placement-reason",
        "class",
        !constructionPreviewValid_ || placementRejectionRemaining_ > 0 ? "invalid" : "valid");
    const auto placementBounds = renderer.uiBounds("placement-reason");
    const float panelWidth =
        placementBounds ? placementBounds->right - placementBounds->left : 340 * config_.uiScale;
    const float panelHeight =
        std::max(130 * config_.uiScale,
                 placementBounds ? placementBounds->bottom - placementBounds->top : 0.0F);
    const float x = std::clamp(
        pointerScreen_.x + 18, 8.0F, std::max(8.0F, renderer.viewportWidth() - panelWidth - 8));
    const float y = std::clamp(
        pointerScreen_.y + 18, 8.0F, std::max(8.0F, renderer.viewportHeight() - panelHeight - 8));
    renderer.setUiProperty("placement-reason", "left", std::to_string(static_cast<int>(x)) + "px");
    renderer.setUiProperty("placement-reason", "top", std::to_string(static_cast<int>(y)) + "px");
    renderer.setUiProperty("placement-reason",
                           "display",
                           constructionPlacementMode_ && constructionCursorScreen_ ? "block"
                                                                                   : "none");
    renderer.setUiControls(std::move(focus), std::move(actions));
}

void PlayState::setPaused(bool paused) {
    if (paused_ == paused)
        return;
    paused_ = paused;
    input_.clear();
    if (paused_) {
        pauseScreen_ =
            context_.renderer->pushUiScreen({"assets/ui/pause_menu.rml",
                                             {{"pause-title", Text::get("pause.title")},
                                              {"resume-label", Text::get("pause.resume")},
                                              {"settings-label", Text::get("menu.settings")},
                                              {"exit-label", Text::get("menu.exit")}},
                                             {"pause.resume", "pause.settings", "pause.exit"},
                                             {"pause.resume", "pause.settings", "pause.exit"},
                                             config_.uiScale});
    } else if (pauseScreen_) {
        context_.renderer->removeUiScreen(pauseScreen_);
        pauseScreen_ = {};
    }
}

void PlayState::cancelConstructionPlacement() {
    if (constructionPlacementMode_)
        constructionRotations_[constructionRecipeId_] = constructionRotationDegrees_;
    constructionPlacementMode_ = false;
    constructionBatchActive_ = false;
    pendingRepeatPlacement_ = false;
    pendingConstructionScreen_.reset();
    pendingConstructionIntent_.reset();
    pendingPlacementSequence_.reset();
    pendingUnexploredPlacement_ = false;
    placementRejection_.clear();
    placementRejectionRemaining_ = 0;
    constructionPreviewSummary_.clear();
    constructionTerrainCache_.clear();
    constructionGhost_.reset();
    constructionRotationDegrees_ = 0;
    constructionPowerAssist_ = {};
    pendingConstructionRangeLock_ = false;
}

glm::vec3 PlayState::assistedConstructionPosition(glm::vec3 position, bool lockRange) const {
    const auto* recipe = context_.definitions.recipe(RecipeId{constructionRecipeId_});
    if (!recipe || recipe->product.kind != RecipeProductKind::building)
        return position;
    const auto& buildingId = recipe->product.id;
    if (!constructionGhost_ || constructionGhost_->archetype.value != buildingId) {
        constructionGhost_.emplace();
        auto& ghost = *constructionGhost_;
        ghost.id = std::numeric_limits<EntityId>::max();
        ghost.archetype = EntityArchetypeId{buildingId};
        ghost.presentation = PresentationId{buildingId};
        ghost.authority.owner = localPlayer_;
        context_.definitions.initializeEntity(ghost);
        ghost.construction.emplace();
        ghost.construction.state = BuildingLifecycleState::planned;
    }
    auto& ghost = *constructionGhost_;
    ghost.transform.position = position;
    ghost.transform.rotationDegrees.y = constructionRotationDegrees_;
    position = constructionPowerAssist_.position(session_.world(), localPlayer_, ghost, lockRange);
    ghost.transform.position = position;
    return position;
}

EntityHudModel PlayState::buildConstructionHudModel(const Entity& selected,
                                                    const Player* player) const {
    EntityHudModel hud = EntityHudModelBuilder::build(
        session_.world(), selected.id, selectedUnits_, context_.definitions);
    if (!selected.unitControl.constructionQueue.empty())
        hud.stats.push_back({Text::get("keybind.queued_buildings"),
                             std::to_string(selected.unitControl.constructionQueue.size())});
    appendSyntheticRoutes(hud,
                          session_.world(),
                          context_.definitions,
                          localPlayer_,
                          actionTargets(selected.id, selectedUnits_));
    for (const std::string& building : context_.definitions.matchRules().buildPalette) {
        const RecipeDefinition* recipe =
            context_.definitions.recipe(RecipeId{"construct." + building});
        if (!recipe || recipe->product.kind != RecipeProductKind::building)
            continue;
        const EntityArchetype* product = context_.definitions.archetype(recipe->product.id);
        HudActionModel action;
        action.id = recipe->id;
        action.icon =
            product ? context_.definitions.presentationIcon(PresentationId{product->presentation})
                    : "status_asset_failed";
        action.name = product ? Text::get(product->nameKey) : recipe->product.id;
        action.cost = resourceCost(context_.definitions, recipe, 1, &action.costIcons);
        action.power = powerDescription(context_.definitions, product);
        action.description = product ? Text::get(product->descriptionKey) + "\n" : std::string{};
        action.description +=
            Text::format("entity_hud.drone_power",
                         {std::to_string(static_cast<int>(recipe->constructionPower))});
        action.enabled =
            player && std::all_of(recipe->cost.begin(), recipe->cost.end(), [&](const auto& cost) {
                const auto found = player->resources.find(cost.first);
                return found != player->resources.end() && found->second >= cost.second;
            });
        if (!action.enabled)
            action.disabledReason = Text::get("entity_hud.insufficient_resources");
        if (product) {
            action.requirements =
                Text::format("entity_hud.tooltip_requirements",
                             {upgradeNames(context_.definitions, product->requiredUpgrades)});
            const auto missing = session_.missingBuildingUpgrades(localPlayer_, product->id);
            if (!missing.empty()) {
                action.enabled = false;
                if (!action.disabledReason.empty())
                    action.disabledReason += "\n";
                action.disabledReason += Text::format(
                    "entity_hud.tooltip_missing", {upgradeNames(context_.definitions, missing)});
            }
        }
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
            action.icon =
                context_.definitions.presentationIcon(PresentationId{product->presentation});
        else if (recipe->product.kind == RecipeProductKind::resource)
            action.icon = "resource_" + recipe->product.id;
        else
            action.icon = "status_asset_failed";
        action.cost = resourceCost(context_.definitions, recipe, targets.size(), &action.costIcons);
        action.power = powerDescription(context_.definitions,
                                        product && product->kind == EntityKind::building
                                            ? product
                                            : context_.definitions.archetype(selected.archetype));
        if (player)
            action.description =
                Text::format("entity_hud.time_seconds",
                             {std::to_string(static_cast<int>(
                                 context_.definitions.productionDuration(player->countryId,
                                                                         player->specializationId,
                                                                         selected.archetype.value,
                                                                         recipe->product.id)))});
        if (product && !product->descriptionKey.empty())
            action.description = Text::get(product->descriptionKey) + "\n" + action.description;
        action.enabled = canAffordForAll(player, recipe, targets.size()) &&
                         std::all_of(targets.begin(), targets.end(), [&](EntityId id) {
                             return session_.canStartRecipe(localPlayer_, id, RecipeId{recipe->id});
                         });
        if (!action.enabled)
            action.disabledReason = Text::get("entity_hud.action_unavailable");
        if (product && product->kind == EntityKind::building) {
            action.requirements =
                Text::format("entity_hud.tooltip_requirements",
                             {upgradeNames(context_.definitions, product->requiredUpgrades)});
            const auto missing = session_.missingBuildingUpgrades(localPlayer_, product->id);
            if (!missing.empty())
                action.disabledReason = Text::format("entity_hud.tooltip_missing",
                                                     {upgradeNames(context_.definitions, missing)});
        }
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
        const bool playerResearch = upgrade->satelliteCoverage != SatelliteCoverage::none;
        const auto researchCount = playerResearch ? 1 : targets.size();
        action.cost =
            resourceCost(context_.definitions, research, researchCount, &action.costIcons);
        action.power = powerDescription(context_.definitions,
                                        context_.definitions.archetype(selected.archetype));
        action.requirements =
            Text::format("entity_hud.tooltip_requirements",
                         {upgradeNames(context_.definitions, upgrade->prerequisites)});
        if (research)
            action.description =
                Text::format("entity_hud.time_seconds",
                             {std::to_string(static_cast<int>(research->durationTicks *
                                                              GameSession::fixedTickSeconds))});
        if (!upgrade->descriptionKey.empty())
            action.description = Text::get(upgrade->descriptionKey) + "\n" + action.description;
        const auto canResearch = [&](EntityId id) {
            return session_.canStartUpgrade(localPlayer_, id, upgrade->id);
        };
        action.enabled = canAffordForAll(player, research, researchCount) &&
                         (playerResearch ? std::any_of(targets.begin(), targets.end(), canResearch)
                                         : std::all_of(targets.begin(), targets.end(), canResearch));
        if (!action.enabled)
            action.disabledReason = Text::get("entity_hud.action_unavailable");
        std::vector<std::string> missing;
        if (research && research->product.kind == RecipeProductKind::building) {
            const auto* product = context_.definitions.archetype(research->product.id);
            if (product && !product->requiredUpgrades.empty()) {
                action.requirements +=
                    "\n" +
                    Text::format("entity_hud.tooltip_requirements",
                                 {upgradeNames(context_.definitions, product->requiredUpgrades)});
                missing = session_.missingBuildingUpgrades(localPlayer_, product->id);
            }
        }
        for (const auto& required : upgrade->prerequisites)
            if (std::any_of(targets.begin(), targets.end(), [&](EntityId id) {
                    const auto* entity = session_.world().findEntity(id);
                    return !entity || session_.upgradeLevel(localPlayer_, id, required) == 0;
                }))
                missing.push_back(required);
        if (!missing.empty())
            action.disabledReason = Text::format("entity_hud.tooltip_missing",
                                                 {upgradeNames(context_.definitions, missing)});
        if (!canAffordForAll(player, research, researchCount)) {
            if (!action.disabledReason.empty())
                action.disabledReason += "\n";
            action.disabledReason += Text::get("entity_hud.insufficient_resources");
        }
        hud.actions.push_back(std::move(action));
    }
    if (selected.power) {
        HudActionModel connection;
        connection.id = "power.connection";
        connection.name = Text::get("power.action.connection");
        connection.description = Text::get("power.action.connection.description");
        connection.icon = "action_power_connect";
        connection.cost = Text::get("entity_hud.free");
        connection.enabled = targets.size() == 1;
        if (!connection.enabled)
            connection.disabledReason = Text::get("entity_hud.action_unavailable");
        hud.actions.push_back(std::move(connection));

        HudActionModel priority;
        priority.id = "power.priority";
        priority.name = Text::get("power.action.priority");
        priority.description = Text::format("power.action.priority.description",
                                            {powerPriorityName(selected.power.priority)});
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

        for (const bool consumption : {true, false}) {
            HudActionModel control;
            const bool enabled =
                consumption ? selected.power.consumptionEnabled : selected.power.outputEnabled;
            control.id = consumption ? "power.consumption" : "power.output";
            control.name = Text::get(consumption ? (enabled ? "power.action.consumption.disable"
                                                            : "power.action.consumption.enable")
                                                 : (enabled ? "power.action.output.disable"
                                                            : "power.action.output.enable"));
            control.description = Text::get(consumption ? "power.action.consumption.description"
                                                        : "power.action.output.description");
            control.icon = enabled ? "status_powered" : "status_unpowered";
            control.cost = Text::get("entity_hud.free");
            control.enabled = true;
            hud.actions.push_back(std::move(control));
        }
    }
    appendSyntheticRoutes(hud, session_.world(), context_.definitions, localPlayer_, targets);
    return hud;
}

std::optional<glm::vec2> PlayState::minimapWorldAt(float screenX, float screenY) const {
    if (!context_.renderer)
        return std::nullopt;
    const auto content = context_.renderer->uiBounds("minimap-content");
    if (!content || !content->contains(screenX, screenY))
        return std::nullopt;
    const float left = content->left, right = content->right;
    const float top = content->top, bottom = content->bottom;
    const float normalizedX = std::clamp((screenX - left) / (right - left), 0.0F, 1.0F);
    const float normalizedZ = std::clamp((screenY - top) / (bottom - top), 0.0F, 1.0F);
    return MapArea{session_.mapChunksPerSide()}.worldFromNormalized({normalizedX, normalizedZ});
}

bool PlayState::handleGameplayHudAction(const std::string& action) {
    if (action == "hud.menu") { setPaused(true); return true; }
    if (action == "resources.power") { powerOverlayVisible_ = !powerOverlayVisible_; return true; }
    if (action == "hud.satellite") {
        session_.submit({localPlayer_, nextCommandSequence_++, AdvanceSatelliteCommand{}});
        return true;
    }
    if (action.starts_with("selection.")) {
        selectedResourceField_ = {};
        const std::string chosen = action.substr(10);
        const bool removeType = config_.keybinds.held("selection_add", input_);
        selectedUnits_.erase(std::remove_if(selectedUnits_.begin(), selectedUnits_.end(), [&](EntityId id) {
            const Entity* entity = session_.world().findEntity(id);
            return !entity || (removeType ? entity->archetype.value == chosen : entity->archetype.value != chosen);
        }), selectedUnits_.end());
        selectedEntity_ = selectedUnits_.empty() ? 0 : selectedUnits_.front();
        if (selectedUnits_.size() == 1) selectedUnits_.clear();
        lastWorldClickEntity_ = 0;
        return true;
    }
    const Entity* selected = session_.world().findEntity(selectedEntity_);
    if (!selected || selected->authority.owner != localPlayer_)
        return false;
    if (action.starts_with("queue.")) {
        const auto index = static_cast<std::uint32_t>(std::stoul(action.substr(6)));
        session_.submit({localPlayer_, nextCommandSequence_++, CancelProductionCommand{selected->id, index}});
        return true;
    }
    if (!action.starts_with("action.")) return false;
    const std::string id = action.substr(7);
    const auto targets = actionTargets(selectedEntity_, selectedUnits_);
    if (selected->archetype.value == "construction_drone" && !id.starts_with("delivery-output:")) {
        cancelConstructionPlacement();
        constructionRecipeId_ = id;
        constructionRotationDegrees_ = constructionRotations_[id];
        constructionPlacementMode_ = true;
        return true;
    }
    if (id == "power.connection") {
        powerLinkMode_ = PowerLinkMode::toggle;
        powerLinkSource_ = selected->id;
        powerOverlayVisible_ = true;
        return true;
    }
    if (id == "power.priority") {
        const PowerPriority current = selected->power ? selected->power->priority : PowerPriority::medium;
        const PowerPriority next = current == PowerPriority::low ? PowerPriority::medium : current == PowerPriority::medium ? PowerPriority::high : PowerPriority::low;
        for (EntityId target : targets) session_.submit({localPlayer_, nextCommandSequence_++, SetPowerPriorityCommand{target, next}});
        return true;
    }
    if (id == "power.toggle") {
        const bool enabled = selected->power && !selected->power->enabled;
        for (EntityId target : targets) session_.submit({localPlayer_, nextCommandSequence_++, SetPowerEnabledCommand{target, enabled}});
        return true;
    }
    if (id == "power.consumption" || id == "power.output") {
        const bool consumption = id == "power.consumption";
        const bool enabled = selected->power && !(consumption ? selected->power->consumptionEnabled : selected->power->outputEnabled);
        for (EntityId target : targets)
            if (consumption) session_.submit({localPlayer_, nextCommandSequence_++, SetPowerConsumptionEnabledCommand{target, enabled}});
            else session_.submit({localPlayer_, nextCommandSequence_++, SetPowerOutputEnabledCommand{target, enabled}});
        return true;
    }
    if (id.starts_with("delivery-output:")) {
        const std::string output = id.substr(16);
        for (EntityId target : targets) session_.submit({localPlayer_, nextCommandSequence_++, SetDeliveryOutputCommand{target, output}});
        return true;
    }
    if (const RecipeDefinition* recipe = context_.definitions.recipe(RecipeId{id})) {
        for (EntityId target : targets) session_.submit({localPlayer_, nextCommandSequence_++, StartRecipeCommand{target, recipe->id}});
        context_.events.enqueue(AudioEvent{AudioCue::trainUnit});
    } else if (const UpgradeDefinition* upgrade = context_.definitions.upgrade(id)) {
        for (EntityId target : targets) {
            if (upgrade->satelliteCoverage != SatelliteCoverage::none &&
                !session_.canStartUpgrade(localPlayer_, target, upgrade->id))
                continue;
            session_.submit({localPlayer_, nextCommandSequence_++, StartUpgradeCommand{target, upgrade->id}});
            if (upgrade->satelliteCoverage != SatelliteCoverage::none)
                break;
        }
    }
    return true;
}

void PlayState::handleSelectionShortcut(std::string_view action) {
    const auto select = [&](std::vector<EntityId> ids, bool focus) {
        std::erase_if(ids, [&](EntityId id) {
            const auto* e = session_.world().findEntity(id);
            return !e || e->authority.owner != localPlayer_;
        });
        if (ids.empty())
            return;
        selectedResourceField_ = {};
        selectedEntity_ = ids.front();
        selectedUnits_ = std::move(ids);
        if (selectedUnits_.size() == 1)
            selectedUnits_.clear();
        if (focus)
            camera_.focusAt(session_.world().findEntity(selectedEntity_)->transform.position);
    };
    std::vector<EntityId> selected = selectedUnits_;
    if (selected.empty() && selectedEntity_)
        selected.push_back(selectedEntity_);
    if (action.starts_with("assign_group_")) {
        const auto index = std::stoul(std::string{action.substr(13)}) - 1;
        std::erase_if(selected, [&](auto id) {
            const auto* e = session_.world().findEntity(id);
            return !e || e->authority.owner != localPlayer_ ||
                   (!e->unitControl && !e->construction);
        });
        controlGroups_.at(index) = std::move(selected);
        return;
    }
    if (action.starts_with("group_")) {
        select(controlGroups_.at(std::stoul(std::string{action.substr(6)}) - 1), false);
        return;
    }
    if (action == "stop") {
        for (auto id : selected)
            session_.submit({localPlayer_, nextCommandSequence_++, StopUnitCommand{id}});
        return;
    }
    if (action == "focus_hub") {
        for (const auto& entity : session_.world().entities())
            if (entity.authority.owner == localPlayer_ && entity.archetype.value == "command_hub") {
                select({entity.id}, true);
                return;
            }
    }
    if (action == "idle_drone" || action == "all_idle_drones") {
        std::vector<EntityId> idle;
        for (const auto& e : session_.world().entities())
            if (e.authority.owner == localPlayer_ && e.archetype.value == "construction_drone" &&
                e.unitControl.order == UnitOrderKind::idle && !e.battery.returningToCharge &&
                e.unitControl.constructionQueue.empty())
                idle.push_back(e.id);
        std::sort(idle.begin(), idle.end());
        if (action == "all_idle_drones")
            select(idle, false);
        else if (!idle.empty()) {
            const auto next = std::upper_bound(idle.begin(), idle.end(), selectedEntity_);
            select({next == idle.end() ? idle.front() : *next}, true);
        }
    }
    if (action == "copy_building") {
        const auto* building = session_.world().findEntity(hoveredEntity_);
        if (!building || building->authority.owner != localPlayer_ || !building->construction ||
            !context_.definitions.recipe(RecipeId{"construct." + building->archetype.value}))
            return;
        const auto recipe = "construct." + building->archetype.value;
        const auto rotation = building->transform.rotationDegrees.y;
        EntityId builder{};
        float best = std::numeric_limits<float>::max();
        for (const auto& e : session_.world().entities())
            if (e.authority.owner == localPlayer_ && e.archetype.value == "construction_drone") {
                const float distance =
                    glm::length(e.transform.position - building->transform.position);
                if (distance < best) {
                    best = distance;
                    builder = e.id;
                }
            }
        if (builder) {
            cancelConstructionPlacement();
            select({builder}, false);
            constructionRecipeId_ = recipe;
            constructionRotationDegrees_ = rotation;
            constructionPlacementMode_ = true;
            constructionCursorScreen_ = pointerScreen_;
        }
    }
}

void PlayState::handleEvent(const SDL_Event& event) {
    input_.observe(event);
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        mousePanning_ = orbiting_ = draggingSelection_ = false;
    const InputContext inputContext = constructionPlacementMode_           ? InputContext::placement
                                      : viewMode_ == ViewMode::unitControl ? InputContext::direct
                                                                           : InputContext::strategy;
    const auto pressed = [&](std::string_view action) {
        return config_.keybinds.matches(action, event, inputContext);
    };
    const auto released = [&](std::string_view action) {
        return config_.keybinds.matches(action, event, inputContext, true);
    };
    if (paused_) {
        if (pressed("pause"))
            setPaused(false);
        else
            handlePauseEvent(event);
        return;
    }
    const bool mouseEvent =
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP;
    const glm::vec2 eventPoint =
        mouseEvent ? glm::vec2{event.button.x, event.button.y} : pointerScreen_;
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        inputWindowId_ = event.key.windowID;
    else if (event.type == SDL_EVENT_MOUSE_MOTION)
        inputWindowId_ = event.motion.windowID;
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        inputWindowId_ = event.button.windowID;
    else if (event.type == SDL_EVENT_MOUSE_WHEEL)
        inputWindowId_ = event.wheel.windowID;
    if (pressed("debug")) {
        detailedDebug_ = !detailedDebug_;
        if (detailedDebug_) {
            terrainDebug_ = false;
            waterDebugMode_ = 0;
            powerDebug_ = false;
        }
        return;
    }
    if (pressed("terrain_debug")) {
        terrainDebug_ = !terrainDebug_;
        if (terrainDebug_) {
            detailedDebug_ = false;
            waterDebugMode_ = 0;
            powerDebug_ = false;
        }
        return;
    }
    if (pressed("water_debug")) {
        waterDebugMode_ = (waterDebugMode_ + 1) % 4;
        if (waterDebugMode_ != 0) {
            detailedDebug_ = false;
            terrainDebug_ = false;
            powerDebug_ = false;
        }
        return;
    }
    if (pressed("power_debug")) {
        powerDebug_ = !powerDebug_;
        if (powerDebug_) {
            detailedDebug_ = false;
            terrainDebug_ = false;
            waterDebugMode_ = 0;
        }
        return;
    }
    if (pressed("quick_save")) {
        try {
            SaveGame::write(config_.savePath(),
                            session_.terrainSeed(),
                            session_.world(),
                            &session_.players(),
                            session_.mapChunksPerSide(),
                            session_.terrainLayout().value,
                            session_.terrain().mode(),
                            session_.terrain().repairLevel());
            context_.logger.info("persistence", "Saved game to " + config_.savePath().string());
            context_.events.enqueue(AudioEvent{AudioCue::saveGame});
        } catch (const std::exception& error) {
            context_.logger.error("persistence", error.what());
        }
        return;
    }
    if (pressed("quick_load")) {
        try {
            SaveData data = SaveGame::read(config_.savePath());
            pendingTerrainSeed_ = data.terrainSeed;
            pendingTerrainChunksPerSide_ = data.mapChunksPerSide;
            pendingTerrainLayout_ = TerrainLayoutId{data.terrainLayout};
            session_.replaceWorld(std::move(data.entities),
                                  data.terrainSeed,
                                  std::move(data.foundations),
                                  data.mapChunksPerSide,
                                  TerrainLayoutId{data.terrainLayout},
                                  data.terrainMode,
                                  data.terrainRepairLevel);
            restoreSavedPlayerProgress(data);
            cancelConstructionPlacement();
            possessedEntity_ = 0;
            viewMode_ = ViewMode::strategy;
            setMouseCaptured(false);
            context_.logger.info("persistence", "Loaded game from " + config_.savePath().string());
            context_.events.enqueue(AudioEvent{AudioCue::loadGame});
        } catch (const std::exception& error) {
            context_.logger.error("persistence", error.what());
        }
        return;
    }
    if (pressed("cancel_construction")) {
        if (Entity* selected = session_.world().findEntity(selectedEntity_);
            selected && selected->authority.owner == localPlayer_ && selected->construction &&
            !isOperational(*selected)) {
            session_.submit(
                {localPlayer_, nextCommandSequence_++, CancelConstructionCommand{selected->id}});
            selectedEntity_ = 0;
            selectedUnits_.clear();
        }
        return;
    }
    if (pressed("possess")) {
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
    if (pressed("pause") || pressed("cancel_placement")) {
        if (powerLinkMode_ != PowerLinkMode::none) {
            powerLinkMode_ = PowerLinkMode::none;
            powerLinkSource_ = 0;
            return;
        }
        if (constructionPlacementMode_) {
            cancelConstructionPlacement();
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
        setPaused(!paused_);
        orbiting_ = false;
        mousePanning_ = false;
        return;
    }
    if ((pressed("select") || pressed("place")) && viewMode_ == ViewMode::strategy) {
        if (const auto world = minimapWorldAt(eventPoint.x, eventPoint.y)) {
            camera_.focusAt({world->x, camera_.focus().y, world->y});
            draggingSelection_ = false;
            return;
        }
    }
    if (context_.renderer) {
        context_.renderer->handleUiEvent(event);
        if (const auto action = context_.renderer->takeUiAction(); action && handleGameplayHudAction(*action))
            return;
        if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
             event.type == SDL_EVENT_MOUSE_WHEEL) &&
            context_.renderer->pointerOverUi() && !mousePanning_ && !orbiting_)
            return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && constructionPlacementMode_)
        constructionCursorScreen_ = glm::vec2{event.motion.x, event.motion.y};
    if (pressed("place") && viewMode_ == ViewMode::strategy && constructionPlacementMode_) {
        if (pendingConstructionIntent_ || pendingPlacementSequence_)
            return;
        constructionCursorScreen_ = eventPoint;
        pendingConstructionScreen_ = constructionCursorScreen_;
        pendingConstructionRangeLock_ = config_.keybinds.held("range_lock", input_);
        pendingRepeatPlacement_ = config_.keybinds.held("repeat_build", input_);
        std::vector<EntityId> builders = selectedUnits_;
        if (builders.empty())
            builders.push_back(selectedEntity_);
        pendingConstructionIntent_ =
            PlaceBuildingCommand{builders.front(),
                                 constructionRecipeId_,
                                 {},
                                 builders,
                                 constructionRotationDegrees_,
                                 pendingRepeatPlacement_ || constructionBatchActive_};
        draggingSelection_ = false;
        return;
    }
    if (pressed("select") && viewMode_ == ViewMode::strategy) {
        draggingSelection_ = true;
        selectionStart_ = eventPoint;
        selectionEnd_ = selectionStart_;
        return;
    }
    if (released("select") && viewMode_ == ViewMode::strategy && draggingSelection_) {
        draggingSelection_ = false;
        selectionEnd_ = eventPoint;
        if (glm::length(selectionEnd_ - selectionStart_) >= 6.0F)
            pendingSelectionRectangle_ = glm::vec4{selectionStart_, selectionEnd_};
        else {
            pendingSelection_ = selectionEnd_;
            pendingSelectionAdditive_ = config_.keybinds.held("selection_add", input_);
            pendingSelectionDoubleClick_ = mouseEvent && event.button.clicks >= 2;
        }
        return;
    }
    if (pressed("camera_orbit")) {
        orbiting_ = true;
        return;
    }
    if (released("camera_orbit")) {
        orbiting_ = false;
        return;
    }
    if (!mouseEvent && pressed("rotate_building")) {
        constructionRotationDegrees_ = std::remainder(constructionRotationDegrees_ + 45.F, 360.F);
        return;
    }
    if (!mouseEvent && pressed("order")) {
        pendingMoveScreen_ = eventPoint;
        return;
    }
    if (pressed("camera_pan") || (mouseEvent && (pressed("order") || pressed("rotate_building")))) {
        mousePanning_ = pressed("camera_pan");
        rightDragDistance_ = 0;
        return;
    }
    if (released("camera_pan") ||
        (mouseEvent && (released("order") || released("rotate_building")))) {
        mousePanning_ = false;
        if (rightDragDistance_ < 4) {
            if (released("rotate_building"))
                constructionRotationDegrees_ =
                    std::remainder(constructionRotationDegrees_ + 45.F, 360.F);
            else if (released("order")) {
                if (const auto world = minimapWorldAt(eventPoint.x, eventPoint.y)) {
                    pendingOrderTarget_ = 0;
                    pendingMoveDestination_ = glm::vec3{world->x, 0, world->y};
                } else
                    pendingMoveScreen_ = eventPoint;
            }
        }
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
    for (int i = 0; i < 12; ++i)
        if (pressed("action_" + std::to_string(i + 1))) {
            updateGameplayHud(*context_.renderer, session_.players().find(localPlayer_));
            if (static_cast<std::size_t>(i) < hudShortcuts_.size() && !hudShortcuts_[i].empty())
                handleGameplayHudAction(hudShortcuts_[i]);
            return;
        }
    for (const auto& action : inputActions())
        if ((action.id == "focus_hub" || action.id == "idle_drone" ||
             action.id == "all_idle_drones" || action.id == "copy_building" ||
             action.id == "stop" || action.id.starts_with("group_") ||
             action.id.starts_with("assign_group_")) &&
            pressed(action.id)) {
            handleSelectionShortcut(action.id);
            return;
        }
    if (pressed("zoom_in") || pressed("zoom_out")) {
        const float amount =
            (pressed("zoom_in") ? 1.F : -1.F) *
            (event.type == SDL_EVENT_MOUSE_WHEEL ? std::max(1.F, std::abs(event.wheel.y)) : 1.F);
        if (viewMode_ == ViewMode::unitControl)
            thirdPersonCamera_.zoom(amount);
        else
            camera_.zoom(amount);
    }
}

void PlayState::handlePauseEvent(const SDL_Event& event) {
    const auto activate = [&](std::string_view id) {
        if (id == "pause.resume") {
            setPaused(false);
        } else if (pauseTransition_ < 0.0F && id == "pause.settings") {
            pausePendingRequest_ = StateRequest::openSettings;
            pauseTransition_ = 0.0F;
        } else if (pauseTransition_ < 0.0F && id == "pause.exit") {
            pausePendingRequest_ = StateRequest::returnToMainMenu;
            pauseTransition_ = 0.0F;
        }
    };
    if (pauseTransition_ >= 0.0F)
        return;

    context_.renderer->handleUiEvent(event);
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_TAB || event.key.key == SDLK_DOWN)
            context_.renderer->focusUi((SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : 1);
        else if (event.key.key == SDLK_UP)
            context_.renderer->focusUi(-1);
        else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE)
            context_.renderer->activateFocusedUiItem();
        else if (event.key.key == SDLK_ESCAPE)
            setPaused(false);
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
            context_.renderer->focusUi(1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
            context_.renderer->focusUi(-1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)
            context_.renderer->activateFocusedUiItem();
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
            setPaused(false);
    }
    if (const auto action = context_.renderer->takeUiAction()) {
        context_.events.enqueue(AudioEvent{AudioCue::uiClick});
        activate(*action);
    }
}

void PlayState::update(float deltaSeconds) {
    if (settingsRevision_ != context_.settingsRevision) {
        config_ = GameConfig::load(context_.configPath);
        settingsRevision_ = context_.settingsRevision;
        input_.clear();
        orbiting_ = mousePanning_ = false;
    }
    const bool direct = viewMode_ == ViewMode::unitControl;
    forward_ = config_.keybinds.held(direct ? "unit_forward" : "forward", input_);
    backward_ = config_.keybinds.held(direct ? "unit_backward" : "backward", input_);
    left_ = config_.keybinds.held(direct ? "unit_left" : "left", input_);
    right_ = config_.keybinds.held(direct ? "unit_right" : "right", input_);
    running_ = config_.keybinds.held(direct ? "unit_run" : "camera_fast", input_) &&
               !constructionPlacementMode_;
    placementRejectionRemaining_ = std::max(0.0F, placementRejectionRemaining_ - deltaSeconds);
    entryFadeRemaining_ = std::max(0.0F, entryFadeRemaining_ - deltaSeconds);
    for (HudAlert& alert : hudAlerts_)
        alert.remaining -= deltaSeconds;
    std::erase_if(hudAlerts_, [](const HudAlert& alert) { return alert.remaining <= 0.0F; });
    if (paused_) {
        if (pauseTransition_ >= 0.0F) {
            pauseTransition_ += deltaSeconds;
            if (pauseTransition_ >= 0.24F && request_ == StateRequest::none)
                request_ = pausePendingRequest_;
        }
        return;
    }
    if (pickedEntity_) {
        const EntityId clicked = *pickedEntity_;
        pickedEntity_.reset();
        if (powerLinkMode_ != PowerLinkMode::none) {
            if (clicked != 0 && clicked != powerLinkSource_) {
                const auto* source = session_.world().findEntity(powerLinkSource_);
                const bool linked = source && source->power &&
                                    std::find(source->power.connections.begin(),
                                              source->power.connections.end(),
                                              clicked) != source->power.connections.end();
                if (!linked)
                    session_.submit({localPlayer_,
                                     nextCommandSequence_++,
                                     ConnectPowerCommand{powerLinkSource_, clicked}});
                else
                    session_.submit({localPlayer_,
                                     nextCommandSequence_++,
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
            selectedResourceField_ = {};
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
            selectedResourceField_ =
                ResourceFieldSelection::fromClick(session_.world(), clicked, confirmedDoubleClick);
            if (selectedResourceField_)
                selectedUnits_ = selectedResourceField_.members(session_.world());
            if (clicked != 0)
                context_.events.enqueue(
                    AudioEvent{session_.world().findEntity(clicked) &&
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
        selectedResourceField_ = {};
        selectedUnits_ = std::move(*pickedEntities_);
        pickedEntities_.reset();
        selectedEntity_ = selectedUnits_.empty() ? 0 : selectedUnits_.front();
        if (selectedUnits_.size() == 1)
            selectedUnits_.clear();
        lastWorldClickEntity_ = 0;
    }
    const Entity* selectionContext = session_.world().findEntity(selectedEntity_);
    const bool homogeneousDrones =
        selectionContext &&
        isHomogeneousSelection(session_.world(), selectedUnits_, "construction_drone");
    if (!selectionContext || selectionContext->archetype.value != "construction_drone" ||
        (!selectedUnits_.empty() && !homogeneousDrones)) {
        cancelConstructionPlacement();
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
                        session_.submit({localPlayer_,
                                         nextCommandSequence_++,
                                         DeliverResourceCommand{id, target->id}});
                    else
                        session_.submit({localPlayer_,
                                         nextCommandSequence_++,
                                         SetPreferredProcessorCommand{id, target->id}});
                } else if (target && target->resource)
                    session_.submit({localPlayer_,
                                     nextCommandSequence_++,
                                     GatherResourceCommand{id, target->id}});
                else if (target && target->construction && !isOperational(*target))
                    session_.submit(
                        {localPlayer_,
                         nextCommandSequence_++,
                         ConstructCommand{
                             id, target->id, config_.keybinds.held("repeat_build", input_)}});
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
    if (pendingUnexploredPlacement_) {
        placementRejection_ = Text::get("placement.unexplored");
        placementRejectionRemaining_ = 4;
        pendingUnexploredPlacement_ = false;
    }
    if (pendingConstructionIntent_ && !pendingConstructionScreen_) {
        if (!pendingPlacementSequence_) {
            const auto sequence = nextCommandSequence_++;
            if (session_.submit({localPlayer_, sequence, *pendingConstructionIntent_}))
                pendingPlacementSequence_ = sequence;
            else {
                placementRejection_ = Text::get("placement.rejected");
                placementRejectionRemaining_ = 4;
            }
        }
        pendingConstructionIntent_.reset();
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
    for (const auto& event : session_.consumePlacementEvents()) {
        if (event.player != localPlayer_ || !pendingPlacementSequence_ ||
            event.sequence != *pendingPlacementSequence_)
            continue;
        pendingPlacementSequence_.reset();
        if (event.accepted()) {
            if (pendingRepeatPlacement_) {
                constructionBatchActive_ = true;
                // Continue a Ctrl+Shift pole chain from the blueprint just accepted.
                constructionPowerAssist_ = {};
                if (pendingConstructionRangeLock_)
                    constructionPowerAssist_.anchor = event.building;
            } else
                cancelConstructionPlacement();
        } else {
            placementRejection_ = Text::get(placementFailureKey(event));
            placementRejectionRemaining_ = 4;
        }
    }
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
            std::erase_if(hudAlerts_,
                          [&](const HudAlert& alert) { return alert.source == event.target; });
            context_.events.enqueue(AudioEvent{AudioCue::conversionComplete});
            break;
        case ResourceEventKind::waitingForPower:
            if (std::none_of(hudAlerts_.begin(), hudAlerts_.end(), [&](const HudAlert& alert) {
                    return alert.source == event.target;
                }))
                hudAlerts_.push_back(
                    {event.target, Text::get("processor.alert.waiting_power"), 8.0F});
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
        if (event.player != localPlayer_)
            continue;
        std::string message;
        switch (event.kind) {
        case PowerEventKind::connectionCreated:
            message = Text::get("power.alert.connected");
            break;
        case PowerEventKind::connectionRemoved:
            message = Text::get("power.alert.disconnected");
            break;
        case PowerEventKind::shortage:
            message = Text::get("power.alert.shortage");
            break;
        case PowerEventKind::recovered:
            message = Text::get("power.alert.recovered");
            break;
        case PowerEventKind::shutdown:
            message = Text::get("power.alert.shutdown");
            break;
        case PowerEventKind::commandRejected:
            powerLinkMode_ = PowerLinkMode::toggle;
            powerLinkSource_ = event.source;
            switch (event.reason) {
            case PowerFailureReason::outOfRange:
                message = Text::get("power.alert.out_of_range");
                break;
            case PowerFailureReason::connectionLimit:
                message = Text::get("power.alert.connection_limit");
                break;
            case PowerFailureReason::disabled:
                message = Text::get("power.alert.disabled");
                break;
            case PowerFailureReason::alreadyConnected:
                message = Text::get("power.alert.already_connected");
                break;
            case PowerFailureReason::enemyTarget:
                message = Text::get("power.alert.enemy_target");
                break;
            case PowerFailureReason::notOperational:
                message = Text::get("power.alert.not_operational");
                break;
            case PowerFailureReason::notConnected:
                powerLinkMode_ = PowerLinkMode::toggle;
                message = Text::get("power.alert.not_connected");
                break;
            default:
                message = Text::get("power.alert.invalid_target");
                break;
            }
            break;
        }
        hudAlerts_.push_back({event.source, std::move(message), 3.5F});
    }
}

void PlayState::sanitizeEntityReferences() {
    const World& world = session_.world();
    const auto missing = [&](EntityId id) { return id != 0 && world.findEntity(id) == nullptr; };

    if (selectedResourceField_) {
        selectedUnits_ = selectedResourceField_.members(world);
        if (std::find(selectedUnits_.begin(), selectedUnits_.end(), selectedEntity_) ==
            selectedUnits_.end())
            selectedEntity_ = selectedUnits_.empty() ? 0 : selectedUnits_.front();
        if (selectedUnits_.empty())
            selectedResourceField_ = {};
    }
    std::erase_if(selectedUnits_, missing);
    if (missing(selectedEntity_))
        selectedEntity_ = selectedUnits_.empty() ? 0 : selectedUnits_.front();
    if (selectedUnits_.size() == 1 && !selectedResourceField_) {
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

    std::erase_if(hudAlerts_, [&](const HudAlert& alert) { return missing(alert.source); });
}

void PlayState::render(Renderer& renderer) const {
    if (pendingTerrainSeed_) {
        renderer.stageGeneratedTerrain(session_.terrain(),
                                       session_.terrainSeed(),
                                       session_.mapChunksPerSide(),
                                       session_.world().foundations());
        pendingTerrainSeed_.reset();
        pendingTerrainChunksPerSide_.reset();
        pendingTerrainLayout_.reset();
    }
    const glm::vec3 focus = camera_.focus();
    const Player* local = session_.players().find(localPlayer_);
    // Satellite research updates authoritative exploration/vision in the simulation.
    const Player* visibilityPlayer = local;
    CameraView view =
        camera_.view(renderer.aspectRatio(), session_.terrain().heightAt(focus.x, focus.z));
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
    if (pendingConstructionScreen_ && pendingConstructionIntent_) {
        pendingConstructionIntent_->position =
            assistedConstructionPosition(renderer.screenToTerrain(pendingConstructionScreen_->x,
                                                                  pendingConstructionScreen_->y,
                                                                  view,
                                                                  &session_.terrain()),
                                         pendingConstructionRangeLock_);
        if (local) {
            const auto p = pendingConstructionIntent_->position;
            const auto cell =
                MapArea{session_.mapChunksPerSide()}.gridCell({p.x, p.z}, Player::explorationCells);
            const auto index = static_cast<std::size_t>(cell.y * Player::explorationCells + cell.x);
            if (index >= local->discovered.size() || !local->discovered[index]) {
                pendingConstructionIntent_.reset();
                pendingUnexploredPlacement_ = true;
            }
        }
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
    const auto drawScene = [&] {
        renderer.flushTerrainUpdates();
        renderer.prepareResourceSites(session_.world());
        renderer.drawTerrain(view, visibilityPlayer, terrainDebug_, waterDebugMode_);
        particlePresenter_.sync(
            renderer, session_.world(), visibilityPlayer, session_.mapChunksPerSide());
        renderer.drawVegetation(session_.vegetation(), view, visibilityPlayer);
        renderer.drawWorld(
            session_.world(), view, visibilityPlayer, powerOverlayVisible_ || powerDebug_);
        renderer.drawParticles(view);
    };
    if (constructionPlacementMode_ && constructionCursorScreen_) {
        const RecipeDefinition* selectedRecipe =
            context_.definitions.recipe(RecipeId{constructionRecipeId_});
        if (!selectedRecipe || selectedRecipe->product.kind != RecipeProductKind::building) {
            renderer.setPlacementTerrainPreview(nullptr);
            renderer.setTerrainFoundations(session_.world().foundations());
            drawScene();
            return;
        }
        const std::string& buildingId = selectedRecipe->product.id;
        const glm::vec3 position =
            assistedConstructionPosition(renderer.screenToTerrain(constructionCursorScreen_->x,
                                                                  constructionCursorScreen_->y,
                                                                  view,
                                                                  &session_.terrain()),
                                         config_.keybinds.held("range_lock", input_));
        bool currentlyVisible = true;
        bool previouslyExplored = true;
        const EntityArchetype* buildingDefinition = context_.definitions.archetype(buildingId);
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
        const EntityId builder = selectedUnits_.empty() ? selectedEntity_ : selectedUnits_.front();
        const auto analysisStarted = FrameProfiler::Clock::now();
        const auto plan = session_.planBuildingPlacement(
            localPlayer_,
            {builder, constructionRecipeId_, position, {}, constructionRotationDegrees_},
            &constructionTerrainCache_,
            currentlyVisible);
        renderer.recordProfile("placement.analysis",
                               FrameProfiler::millisecondsSince(analysisStarted));
        const auto& buildingFootprint = plan.footprint;
        // A live cost/occupancy rejection must not snap the ghost back to a different
        // height or angle. Keep its geometric pose independent of permission to commit.
        const auto& terrainPlacement =
            !plan.terrain.fit.valid && previouslyExplored && buildingDefinition &&
                    plan.failure != PlacementFailure::outsideMap
                ? constructionTerrainCache_.evaluate(session_.terrain(),
                                                     {position.x, position.z},
                                                     buildingFootprint,
                                                     buildingDefinition->placement)
                : plan.terrain;
        constructionPreviewValid_ = true;
        constructionPreviewReason_.clear();
        const auto invalidate = [&](const char* key) {
            if (constructionPreviewValid_)
                constructionPreviewReason_ = Text::get(key);
            constructionPreviewValid_ = false;
        };
        if (!previouslyExplored) invalidate("placement.unexplored");
        if (!plan.valid())
            invalidate(placementFailureKey(
                PlacementEvent{localPlayer_, 0, 0, plan.failure, terrainPlacement.failure}));
        if (constructionPreviewValid_ == false && plan.blocker && currentlyVisible)
            if (const auto* blocker = session_.world().findEntity(plan.blocker))
                constructionPreviewReason_ += " · " + blocker->name;
        constructionPreviewSummary_.clear();
        if (previouslyExplored && terrainPlacement.footprintArea > 0) {
            std::ostringstream support, summary;
            support << std::fixed << std::setprecision(1)
                    << std::floor(1000 * terrainPlacement.supportedArea /
                                  terrainPlacement.footprintArea) /
                           10;
            summary << std::fixed << std::setprecision(1) << terrainPlacement.foundationDepth;
            const float maximumDepth =
                buildingDefinition ? buildingDefinition->placement.maximumFoundationDepth : 6;
            constructionPreviewSummary_ =
                Text::format("placement.support_summary",
                             {support.str(), summary.str(), displayNumber(maximumDepth)});
        }
        if (previouslyExplored && terrainPlacement.fit.valid) {
            std::ostringstream angle;
            angle << std::fixed << std::setprecision(1) << terrainPlacement.fit.slopeDegrees;
            const char* strategy =
                terrainPlacement.strategy == TerrainPlacementResult::Strategy::fit
                    ? "placement.strategy.fit"
                : terrainPlacement.strategy == TerrainPlacementResult::Strategy::excavation
                    ? "placement.strategy.excavation"
                    : "placement.strategy.scaffold";
            constructionPreviewSummary_ =
                Text::format("placement.tilt_summary", {angle.str(), Text::get(strategy)}) + "\n" +
                constructionPreviewSummary_;
        }
        renderer.setTerrainFoundations(session_.world().foundations());
        renderer.setPlacementTerrainPreview(constructionPreviewValid_ && terrainPlacement.leveling
                                                ? &*terrainPlacement.leveling
                                                : nullptr);
        drawScene();
        auto& ghost = *constructionGhost_;
        ghost.transform.position = {position.x, 0.F, position.z};
        ghost.transform.rotationDegrees.y = constructionRotationDegrees_;
        std::optional<TerrainFoundation> previewPlane;
        if (session_.terrain().mode() == TerrainMode::v2 && terrainPlacement.fit.valid)
            previewPlane.emplace(ghost.id,
                                 glm::vec3{position.x, terrainPlacement.fit.height, position.z},
                                 buildingFootprint,
                                 terrainPlacement.fit.gradient);
        ghost.construction.placementValid = constructionPreviewValid_;
        renderer.drawPlacementGhost(ghost, view, previewPlane ? &*previewPlane : nullptr);
        if (previouslyExplored)
            renderer.drawBuildingPlacementPreview({position.x, position.z},
                                                  buildingFootprint,
                                                  buildingDefinition ? buildingDefinition->placement
                                                                     : TerrainPlacementProfile{},
                                                  terrainPlacement,
                                                  constructionPreviewValid_,
                                                  view);
        if (plan.blocker && currentlyVisible)
            renderer.drawEntityOutline(session_.world(), plan.blocker, view, visibilityPlayer);
        if (ghost.power) {
            const auto targets = findPowerPlacementTargets(session_.world(), localPlayer_, ghost);
            const Entity* endpoint = targets.inRange;
            const bool connected = endpoint != nullptr;
            const Entity* nearest = endpoint ? endpoint : targets.nearest;
            const Entity* anchor = session_.world().findEntity(constructionPowerAssist_.anchor);
            const auto number = [](float value) {
                std::ostringstream text;
                text << std::fixed << std::setprecision(1) << value;
                return text.str();
            };
            std::string status = Text::get("placement.power.none");
            if (nearest) {
                const float range = powerPlacementRange(ghost, *nearest, localPlayer_);
                const float distance =
                    glm::length(glm::vec2{position.x - nearest->transform.position.x,
                                          position.z - nearest->transform.position.z});
                const auto* definition = context_.definitions.archetype(nearest->archetype);
                const std::string name =
                    definition ? Text::get(definition->nameKey) : nearest->name;
                const auto slots =
                    nearest->power.maximumConnections - nearest->power.connections.size();
                status = connected ? Text::format("placement.power.connected",
                                                  {name,
                                                   number(std::max(0.0F, range - distance)),
                                                   std::to_string(slots)})
                                   : Text::format("placement.power.out_of_range",
                                                  {name, number(std::max(0.0F, distance - range))});
                renderer.drawPowerPlacementConnection(
                    view, ghost, *nearest, anchor ? *anchor : *nearest);
            }
            constructionPreviewSummary_ += "\n" + status;
            constructionPreviewSummary_ +=
                "\n" + Text::format(constructionPowerAssist_.lockLost ? "placement.power.lock_lost"
                                    : anchor                          ? "placement.power.locked"
                                                                      : "placement.power.lock_hint",
                                    {config_.keybinds.label("range_lock")});
            if (connected)
                constructionPreviewSummary_ += "\n" + Text::get("placement.power.supply_hint");
        }
    } else {
        renderer.setPlacementTerrainPreview(nullptr);
        renderer.setTerrainFoundations(session_.world().foundations());
        drawScene();
    }
    if (viewMode_ == ViewMode::strategy)
        renderer.drawOrderMarkers(session_.world(), selectedEntity_, selectedUnits_, view);
    if (!selectedUnits_.empty())
        for (EntityId selected : selectedUnits_)
            renderer.drawEntityOutline(session_.world(), selected, view, visibilityPlayer);
    else if (selectedEntity_ != 0)
        renderer.drawEntityOutline(session_.world(), selectedEntity_, view, visibilityPlayer);
    if (hoveredEntity_ != 0 && viewMode_ == ViewMode::strategy) {
        renderer.drawEntityOutline(session_.world(), hoveredEntity_, view, visibilityPlayer);
        const auto* hovered = session_.world().findEntity(hoveredEntity_);
        const bool inspectingHoveredNode = !selectedResourceField_ && selectedUnits_.empty() &&
                                           hoveredEntity_ == selectedEntity_;
        if (!inspectingHoveredNode && hovered && hovered->resource && hovered->resource.fieldId != 0)
            for (const auto& node : session_.world().entities())
                if (node.id != hoveredEntity_ && node.resource && node.resource.remaining > 0 &&
                    node.resource.fieldId == hovered->resource.fieldId &&
                    node.resource.type == hovered->resource.type) {
                    if (visibilityPlayer) {
                        const auto cell = MapArea{session_.mapChunksPerSide()}.gridCell(
                            {node.transform.position.x, node.transform.position.z},
                            Player::explorationCells);
                        if (!visibilityPlayer->discovered[cell.y * Player::explorationCells + cell.x])
                            continue;
                    }
                    renderer.drawEntityOutline(session_.world(), node.id, view, visibilityPlayer);
                }
    }
    if (powerDebug_)
        renderer.drawPowerRanges(session_.world(), view, localPlayer_);
    if (powerOverlayVisible_ || powerDebug_)
        renderer.drawPowerConnections(session_.world(), view, localPlayer_, powerDebug_);
    if (terrainDebug_)
        renderer.drawResourceFieldDebug(session_.resourceLayout(), view);
    if (draggingSelection_ && viewMode_ == ViewMode::strategy)
        renderer.drawSelectionBox(selectionStart_, selectionEnd_);
    if (detailedDebug_) {
        const EntityId detailEntity = possessedEntity_ ? possessedEntity_ : selectedEntity_;
        renderer.drawVisionRanges(view, session_.world().findEntity(detailEntity));
    }
    if (!paused_) {
        updateGameplayHud(renderer, local);
        if (viewMode_ == ViewMode::unitControl && possessedEntity_ != 0)
            renderer.drawCrosshair();
        else if (const auto minimap = renderer.uiBounds("minimap-content"))
            renderer.drawStrategyHud(session_.world(), selectedEntity_, visibilityPlayer, *minimap);
        renderer.drawRmlUi();
    }
    // Terrain inspection is a UI pass and must remain after every world/overlay pass.
    // Keeping it at the top of the visual stack also prevents entity outlines from
    // showing through its opaque panel.
    if (detailedDebug_ && !paused_)
        renderer.drawDetailedDebugHud(view,
            session_.world().findEntity(possessedEntity_ ? possessedEntity_ : selectedEntity_),
            visibilityPlayer, session_.terrainSeed(), session_.tick(), session_.world().size());
    if (terrainDebug_ && !paused_) {
        const glm::vec3 cursor = renderer.screenToTerrain(pointerScreen_.x, pointerScreen_.y, view);
        renderer.drawTerrainDebugHud(cursor, session_.resourceLayout());
    }
    if (waterDebugMode_ != 0 && !paused_) {
        const glm::vec3 cursor = renderer.screenToTerrain(pointerScreen_.x, pointerScreen_.y, view);
        renderer.drawWaterDebugHud(cursor, waterDebugMode_);
    }
    if (powerDebug_ && !paused_) {
        const glm::vec3 cursor = renderer.screenToTerrain(pointerScreen_.x, pointerScreen_.y, view);
        renderer.drawPowerDebugHud(session_.world(), localPlayer_, cursor);
    }
    if (paused_) {
        renderer.drawRmlUi();
        if (pauseTransition_ >= 0.0F)
            renderer.drawScreenFade(std::clamp(pauseTransition_ / 0.24F, 0.0F, 1.0F));
    }
    if (entryFadeRemaining_ > 0.0F)
        renderer.drawScreenFade(std::clamp(entryFadeRemaining_ / 0.35F, 0.0F, 1.0F));
}

StateRequest PlayState::takeRequest() {
    const StateRequest result = request_;
    request_ = StateRequest::none;
    if (result == StateRequest::openSettings) {
        pauseTransition_ = -1.0F;
        pausePendingRequest_ = StateRequest::none;
    }
    return result;
}

} // namespace strategy
