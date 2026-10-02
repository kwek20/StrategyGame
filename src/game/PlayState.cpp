#include "game/PlayState.hpp"

#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "diagnostics/Logger.hpp"
#include "localization/Text.hpp"
#include "persistence/SaveGame.hpp"
#include "render/Renderer.hpp"
#include "terrain/Terrain.hpp"
#include "ui/EntityHudModel.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <iostream>
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
               TerrainLayoutId{std::move(setup.terrainLayout)})
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

PlayState::PlayState(StateContext& context, SaveData data)
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
               TerrainLayoutId{data.terrainLayout})
    , config_(GameConfig::load(context.configPath)) {
    session_.replaceWorld(std::move(data.entities),
                          data.terrainSeed,
                          std::move(data.foundations),
                          data.mapChunksPerSide,
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
    renderer.setUiText("hud.satellite.label", satelliteRevealActive_ ? "SAT ON" : "SAT");

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
    std::ostringstream entityQueue;
    std::optional<EntityHudModel> hud;
    const Entity* selected = session_.world().findEntity(possessedEntity_ ? possessedEntity_ : selectedEntity_);
    const bool sameType = selected && isHomogeneousSelection(session_.world(), selectedUnits_, selected->archetype.value);
    if (viewMode_ == ViewMode::unitControl && selected) {
        hud = EntityHudModelBuilder::build(session_.world(), selected->id, {}, context_.definitions);
        hud->footer = Text::get("unit.escape");
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
        std::ostringstream entityInfo;
        entityInfo << "<div class='entity-info'><div class='entity-title'>" << escapeRml(hud->title)
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
        entityInfo << "</div>";
        entity << "<div class='entity-actions'>";
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
                   << hudIcon(group.icon) << "'/><span>" << group.count << "</span></button>";
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
                    entity << "<span class='action-hotkey'>Alt+" << "1234567890-="[index] << "</span>";
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
        entity << "</div>";
        entity << entityInfo.str();
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
    renderer.setUiText("entity-content", entity.str());
    renderer.setUiText("entity-queue", entityQueue.str());
    renderer.setUiAttribute("entity-shell", "class", hud ? "" : "hidden");

    std::ostringstream alerts;
    for (const HudAlert& alert : hudAlerts_) alerts << "<div class='alert'>" << escapeRml(alert.text) << "</div>";
    renderer.setUiText("alerts", alerts.str());
    renderer.setUiText("placement-reason", constructionPlacementMode_ && !constructionPreviewValid_ ? escapeRml(constructionPreviewReason_) : "");
    renderer.setUiProperty("placement-reason", "left", std::to_string(static_cast<int>(pointerScreen_.x + 18.0F)) + "px");
    renderer.setUiProperty("placement-reason", "top", std::to_string(static_cast<int>(pointerScreen_.y + 18.0F)) + "px");
    renderer.setUiProperty("placement-reason", "display", constructionPlacementMode_ && !constructionPreviewValid_ && !constructionPreviewReason_.empty() ? "block" : "none");
    renderer.setUiControls(std::move(focus), std::move(actions));
}

void PlayState::setPaused(bool paused) {
    if (paused_ == paused)
        return;
    paused_ = paused;
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

EntityHudModel PlayState::buildConstructionHudModel(const Entity& selected,
                                                    const Player* player) const {
    EntityHudModel hud = EntityHudModelBuilder::build(
        session_.world(), selected.id, selectedUnits_, context_.definitions);
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
        action.cost =
            resourceCost(context_.definitions, research, targets.size(), &action.costIcons);
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
        action.enabled = canAffordForAll(player, research, targets.size()) &&
                         std::all_of(targets.begin(), targets.end(), [&](EntityId id) {
                             return session_.canStartUpgrade(localPlayer_, id, upgrade->id);
                         });
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
                    return !entity || !entity->upgrades ||
                           !entity->upgrades.levels.contains(required) ||
                           entity->upgrades.levels.at(required) == 0;
                }))
                missing.push_back(required);
        if (!missing.empty())
            action.disabledReason = Text::format("entity_hud.tooltip_missing",
                                                 {upgradeNames(context_.definitions, missing)});
        if (!canAffordForAll(player, research, targets.size())) {
            if (!action.disabledReason.empty())
                action.disabledReason += "\n";
            action.disabledReason += Text::get("entity_hud.insufficient_resources");
        }
        hud.actions.push_back(std::move(action));
    }
    if (selected.power) {
        HudActionModel connect;
        connect.id = "power.connect";
        connect.name = Text::get("power.action.connect");
        connect.description = Text::get("power.action.connect.description");
        connect.icon = "action_power_connect";
        connect.cost = Text::get("entity_hud.free");
        connect.enabled = targets.size() == 1 && isOperational(selected) &&
                          selected.power.enabled &&
                          selected.power.connections.size() < selected.power.maximumConnections;
        if (!connect.enabled)
            connect.disabledReason = Text::get("entity_hud.action_unavailable");
        hud.actions.push_back(std::move(connect));

        HudActionModel disconnect;
        disconnect.id = "power.disconnect";
        disconnect.name = Text::get("power.action.disconnect");
        disconnect.description = Text::get("power.action.disconnect.description");
        disconnect.icon = "action_power_disconnect";
        disconnect.cost = Text::get("entity_hud.free");
        disconnect.enabled = targets.size() == 1 && !selected.power.connections.empty();
        if (!disconnect.enabled)
            disconnect.disabledReason = Text::get("entity_hud.action_unavailable");
        hud.actions.push_back(std::move(disconnect));

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
        if (selected.power.storageCapacity > 0.0F) {
            HudActionModel discharge;
            discharge.id = "power.discharge";
            discharge.name = selected.power.dischargeEnabled
                                 ? Text::get("power.action.discharge.disable")
                                 : Text::get("power.action.discharge.enable");
            discharge.description = Text::get("power.action.discharge.description");
            discharge.icon =
                selected.power.dischargeEnabled ? "status_powered" : "status_unpowered";
            discharge.cost = Text::get("entity_hud.free");
            discharge.enabled = true;
            hud.actions.push_back(std::move(discharge));
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
    if (action == "hud.satellite" && satelliteImageryAvailable_) { satelliteRevealActive_ = !satelliteRevealActive_; return true; }
    if (action.starts_with("selection.")) {
        const std::string chosen = action.substr(10);
        const bool removeType = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
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
        constructionRecipeId_ = id; constructionPlacementMode_ = true; return true;
    }
    if (id == "power.connect" || id == "power.disconnect") {
        powerLinkMode_ = id == "power.connect" ? PowerLinkMode::connect : PowerLinkMode::disconnect;
        powerLinkSource_ = selected->id; powerOverlayVisible_ = true; return true;
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
    if (id == "power.discharge") {
        const bool enabled = selected->power && !selected->power->dischargeEnabled;
        for (EntityId target : targets) session_.submit({localPlayer_, nextCommandSequence_++, SetPowerDischargeEnabledCommand{target, enabled}});
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
        for (EntityId target : targets) session_.submit({localPlayer_, nextCommandSequence_++, StartUpgradeCommand{target, upgrade->id}});
    }
    return true;
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
        if (terrainDebug_) {
            waterDebugMode_ = 0;
            powerDebug_ = false;
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.key == bound("water_debug", SDLK_F6)) {
        waterDebugMode_ = (waterDebugMode_ + 1) % 4;
        if (waterDebugMode_ != 0) {
            terrainDebug_ = false;
            powerDebug_ = false;
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.key == bound("power_debug", SDLK_F7)) {
        powerDebug_ = !powerDebug_;
        if (powerDebug_) {
            terrainDebug_ = false;
            waterDebugMode_ = 0;
        }
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F5) {
        try {
            SaveGame::write(config_.savePath(),
                            session_.terrainSeed(),
                            session_.world(),
                            &session_.players(),
                            session_.mapChunksPerSide(),
                            session_.terrainLayout().value);
            context_.logger.info("persistence", "Saved game to " + config_.savePath().string());
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
            session_.replaceWorld(std::move(data.entities),
                                  data.terrainSeed,
                                  std::move(data.foundations),
                                  data.mapChunksPerSide,
                                  TerrainLayoutId{data.terrainLayout});
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
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_DELETE) {
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
        setPaused(!paused_);
        orbiting_ = false;
        mousePanning_ = false;
        return;
    }
    if (paused_) {
        handlePauseEvent(event);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
        viewMode_ == ViewMode::strategy) {
        if (const auto world = minimapWorldAt(event.button.x, event.button.y)) {
            camera_.focusAt({world->x, camera_.focus().y, world->y});
            draggingSelection_ = false;
            return;
        }
    }
    if (context_.renderer) {
        context_.renderer->handleUiEvent(event);
        if (const auto action = context_.renderer->takeUiAction(); action && handleGameplayHudAction(*action))
            return;
        if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
            context_.renderer->pointerOverUi())
            return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && constructionPlacementMode_)
        constructionCursorScreen_ = glm::vec2{event.motion.x, event.motion.y};
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
        viewMode_ == ViewMode::strategy && constructionPlacementMode_) {
        constructionCursorScreen_ = glm::vec2{event.button.x, event.button.y};
        pendingConstructionScreen_ = constructionCursorScreen_;
        draggingSelection_ = false;
        return;
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
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        viewMode_ == ViewMode::strategy && (event.key.mod & SDL_KMOD_ALT) &&
        ((event.key.key >= SDLK_0 && event.key.key <= SDLK_9) ||
         event.key.key == SDLK_MINUS || event.key.key == SDLK_EQUALS)) {
        updateGameplayHud(*context_.renderer, session_.players().find(localPlayer_));
        const std::size_t index = event.key.key == SDLK_MINUS ? 10 : event.key.key == SDLK_EQUALS ? 11 :
                                  event.key.key == SDLK_0 ? 9 : event.key.key - SDLK_1;
        if (index < hudShortcuts_.size() && !hudShortcuts_[index].empty())
            handleGameplayHudAction(hudShortcuts_[index]);
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
                if (powerLinkMode_ == PowerLinkMode::connect)
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
                        {localPlayer_, nextCommandSequence_++, ConstructCommand{id, target->id}});
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
        if (!selectedUnits_.empty())
            drone = selectedUnits_.front();
        if (constructionPreviewValid_)
            if (const Entity* builder = session_.world().findEntity(drone);
                builder && builder->flight) {
                std::vector<EntityId> builders;
                if (selectedUnits_.empty())
                    builders.push_back(drone);
                else
                    for (EntityId id : selectedUnits_)
                        if (const Entity* selected = session_.world().findEntity(id);
                            selected && selected->authority.owner == localPlayer_ &&
                            selected->flight)
                            builders.push_back(id);
                session_.submit({localPlayer_,
                                 nextCommandSequence_++,
                                 PlaceBuildingCommand{drone,
                                                      constructionRecipeId_,
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
            powerLinkMode_ = PowerLinkMode::connect;
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
                powerLinkMode_ = PowerLinkMode::disconnect;
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

    std::erase_if(hudAlerts_, [&](const HudAlert& alert) { return missing(alert.source); });
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
    renderer.drawWorld(session_.world(), view, visibilityPlayer,
                       powerOverlayVisible_ || powerDebug_);
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
        const float playableExtent = placementMap.halfExtent() -
                                     context_.definitions.matchRules().terrainEdgeMargin;
        if (std::abs(position.x) + placementBounds.x > playableExtent ||
            std::abs(position.z) + placementBounds.y > playableExtent)
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
        if (ghost.power)
            renderer.drawPowerPlacementConnection(session_.world(), view, localPlayer_, ghost);
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
        renderer.drawDetailedDebugHud(view,
                                      session_.world().findEntity(detailEntity),
                                      visibilityPlayer,
                                      session_.terrainSeed(),
                                      session_.tick(),
                                      session_.world().size());
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
    if (terrainDebug_) {
        const glm::vec3 cursor = renderer.screenToTerrain(pointerScreen_.x, pointerScreen_.y, view);
        renderer.drawTerrainDebugHud(cursor, session_.resourceLayout());
    }
    if (waterDebugMode_ != 0) {
        const glm::vec3 cursor = renderer.screenToTerrain(pointerScreen_.x, pointerScreen_.y, view);
        renderer.drawWaterDebugHud(cursor, waterDebugMode_);
    }
    if (powerDebug_) {
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
