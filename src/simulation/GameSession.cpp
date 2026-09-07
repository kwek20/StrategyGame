#include "simulation/GameSession.hpp"
#include "simulation/StateChecksum.hpp"

#include "localization/Text.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/Navigation.hpp"
#include "world/WorldGeneration.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <limits>
#include <iostream>
#include <type_traits>

namespace strategy {

std::uint64_t GameSession::stateChecksum() const {
    return authoritativeStateChecksum(world_, players_, terrainSeed_, tick_);
}

GameSession::GameSession(const DefinitionRegistry& definitions,
                         std::uint32_t terrainSeed,
                         std::string playerOneCountry,
                         std::string playerTwoCountry,
                         std::string playerOneSpecialization,
                         std::string playerTwoSpecialization)
    : players_(std::move(playerOneCountry),
               std::move(playerTwoCountry),
               std::move(playerOneSpecialization),
               std::move(playerTwoSpecialization))
    , gameplay_(definitions)
    , terrainSeed_(terrainSeed)
    , terrain_(terrainSeed)
    , navigation_(terrain_, gameplay_) {
    for (Player& player : players_.players())
        player.resources = gameplay_.matchRules().startingResources;
    const auto createStartingEntities = [this](PlayerId player,
                                               const auto& starts,
                                               float rotation) {
        for (const StartingEntityDefinition& start : starts) {
            Entity& entity =
                world_.createEntity(Text::get(start.nameKey), start.archetype, player);
            initializeEntity(entity);
            entity.transform.position = start.position;
            if (!entity.flight)
                entity.transform.position.y = 0.0F;
            else
                entity.flight.altitude = start.position.y;
            entity.transform.rotationDegrees.y = rotation;
            if (entity.unitControl)
                entity.unitControl.directlyControllable = start.directlyControllable;
        }
    };
    createStartingEntities(1, gameplay_.matchRules().playerOne, 0.0F);
    createStartingEntities(2, gameplay_.matchRules().playerTwo, 180.0F);
    for (Entity& entity : world_.entities()) {
        if (entity.kind != EntityKind::building)
            continue;
        const float radius = collisionRadius(gameplay_, entity.archetype);
        const EntityArchetype* definition = gameplay_.archetype(entity.archetype);
        TerrainFootprint shape = definition && definition->footprint
                                     ? *definition->footprint
                                     : TerrainFootprint{FootprintShape::circle, radius, {radius, radius}};
        shape.rotationDegrees = entity.transform.rotationDegrees.y;
        const TerrainFoundation foundation = terrain_.evaluateFoundation(
            entity.id, entity.transform.position.x, entity.transform.position.z, shape);
        entity.transform.rotationDegrees.x = -glm::degrees(std::atan(foundation.gradient.y));
        entity.transform.rotationDegrees.z = glm::degrees(std::atan(foundation.gradient.x));
        world_.foundations().push_back(foundation);
        terrain_.applyFoundation(foundation);
    }
    populateResources(world_, terrain_, gameplay_, terrainSeed);
    updateExploration();
}

float GameSession::stat(const Entity& entity, GameplayStat requested) const {
    const Player* player = players_.find(entity.authority.owner);
    if (!player)
        return 0.0F;
    RuntimeModifierLayers layers;
    if (entity.upgrades)
        for (const auto& [id, level] : entity.upgrades.levels)
            if (const UpgradeDefinition* upgrade = gameplay_.upgrade(id))
                for (std::uint32_t applied = 0; applied < level; ++applied)
                    if (upgrade->affectsProducer)
                        layers.producingBuildingUpgrades.insert(
                            layers.producingBuildingUpgrades.end(), upgrade->modifiers.begin(), upgrade->modifiers.end());
                    else
                        layers.permanentUpgrades.insert(
                            layers.permanentUpgrades.end(), upgrade->modifiers.begin(), upgrade->modifiers.end());
    return gameplay_.resolve(requested, player->countryId, player->specializationId,
                             entity.archetype.value, {}, layers);
}
void GameSession::initializeEntity(Entity& entity) {
    gameplay_.initializeEntity(entity);
    if (entity.health)
        if (const float value = stat(entity, GameplayStat::health); value > 0) {
            entity.health.maximum = value;
            entity.health.current = value;
        }
    if (entity.unitControl)
        if (const float value = stat(entity, GameplayStat::movementSpeed); value > 0)
            entity.unitControl.movementSpeed = value;
    if (entity.vision)
        if (const float value = stat(entity, GameplayStat::sightRange); value > 0) {
            entity.vision.sightRange = value;
        }
    if (entity.gatherer) {
        if (const float value = stat(entity, GameplayStat::gatherRate); value > 0) {
            entity.gatherer.gatherPerSecond = value;
        }
        if (const float value = stat(entity, GameplayStat::carryCapacity); value > 0) {
            entity.gatherer.carryCapacity = value;
        }
    }
}

bool GameSession::submit(PlayerCommand command) {
    if (players_.find(command.player) == nullptr) {
        return false;
    }
    const auto previous = lastSequence_.find(command.player);
    if (previous != lastSequence_.end() && command.sequence <= previous->second) {
        return false;
    }
    lastSequence_[command.player] = command.sequence;
    commands_.push_back(std::move(command));
    return true;
}

void GameSession::update(double elapsedSeconds) {
    accumulator_ += std::min(elapsedSeconds, 0.25);
    while (accumulator_ >= fixedTickSeconds) {
        advanceTicks();
        accumulator_ -= fixedTickSeconds;
    }
}

void GameSession::advanceTicks(std::uint32_t count) {
    for (std::uint32_t index = 0; index < count; ++index)
        simulateTick();
}

bool GameSession::canStartRecipe(PlayerId playerId, EntityId producerId, RecipeId recipeId) const {
    const Player* player = players_.find(playerId);
    const Entity* producer = world_.findEntity(producerId);
    const RecipeDefinition* recipe = gameplay_.recipe(recipeId);
    if (!player || !producer || producer->authority.owner != playerId || !producer->production ||
        !isOperational(*producer) ||
        !recipe || recipe->producer != producer->archetype.value)
        return false;
    for (const auto& [resource, amount] : recipe->cost) {
        const auto available = player->resources.find(resource);
        if (available == player->resources.end() || available->second < amount)
            return false;
    }
    if (recipe->product.kind == RecipeProductKind::unit) {
        std::uint32_t population = 0;
        for (const Entity& entity : world_.entities()) {
            if (entity.authority.owner == playerId && entity.kind == EntityKind::unit)
                ++population;
            if (entity.authority.owner == playerId && entity.production)
                for (const ProductionOrder& queued : entity.production.queue)
                    if (queued.kind == ProductionKind::trainCharacter)
                        population += queued.amount;
        }
        if (population + recipe->product.amount > gameplay_.matchRules().unitLimit)
            return false;
    }
    return true;
}

bool GameSession::canStartUpgrade(PlayerId playerId,
                                  EntityId researcherId,
                                  const std::string& upgradeId) const {
    const Player* player = players_.find(playerId);
    const Entity* entity = world_.findEntity(researcherId);
    const UpgradeDefinition* upgrade = gameplay_.upgrade(upgradeId);
    if (!player || !entity || entity->authority.owner != playerId || !entity->production ||
        !isOperational(*entity) ||
        !entity->upgrades || !upgrade ||
        std::find(upgrade->allowedResearchers.begin(), upgrade->allowedResearchers.end(),
                  entity->archetype.value) == upgrade->allowedResearchers.end())
        return false;
    std::uint32_t scheduledLevel = entity->upgrades.levels.contains(upgradeId)
                                       ? entity->upgrades.levels.at(upgradeId)
                                       : 0;
    for (const ProductionOrder& order : entity->production.queue)
        if (order.upgradeId == upgradeId)
            ++scheduledLevel;
    if (scheduledLevel >= upgrade->maximumLevel)
        return false;
    for (const std::string& prerequisite : upgrade->prerequisites)
        if (!entity->upgrades.levels.contains(prerequisite) ||
            entity->upgrades.levels.at(prerequisite) == 0)
            return false;
    if (!upgrade->exclusiveGroup.empty())
        for (const auto& [id, level] : entity->upgrades.levels)
            if (level > 0 && id != upgradeId) {
                const UpgradeDefinition* other = gameplay_.upgrade(id);
                if (other && other->exclusiveGroup == upgrade->exclusiveGroup)
                    return false;
            }
    const RecipeDefinition* recipe = gameplay_.recipe(RecipeId{upgrade->researchRecipe});
    if (!recipe || (!recipe->producer.empty() && recipe->producer != entity->archetype.value))
        return false;
    for (const auto& [resource, amount] : recipe->cost) {
        const auto available = player->resources.find(resource);
        if (available == player->resources.end() || available->second < amount)
            return false;
    }
    return true;
}

void GameSession::apply(const PlayerCommand& command) {
    std::visit(
        [this, &command](const auto& payload) {
            Entity* entity = world_.findEntity(payload.entity);
            if (entity == nullptr || entity->authority.owner != command.player) {
                return;
            }
            using Type = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Type, PossessUnitCommand>) {
                if (entity->unitControl && entity->unitControl.directlyControllable &&
                    (entity->authority.directController == 0 ||
                     entity->authority.directController == command.player)) {
                    entity->authority.directController = command.player;
                }
            } else if constexpr (std::is_same_v<Type, ReleaseUnitCommand>) {
                if (entity->unitControl && entity->authority.directController == command.player) {
                    entity->authority.directController = 0;
                    entity->unitControl.directInput = {0.0F, 0.0F};
                    entity->unitControl.running = false;
                }
            } else if constexpr (std::is_same_v<Type, DirectUnitInputCommand>) {
                if (entity->unitControl && entity->authority.directController == command.player) {
                    entity->unitControl.directInput = payload.movement;
                    entity->unitControl.running = payload.running;
                    entity->transform.rotationDegrees.y = payload.facingDegrees;
                    if (glm::length(entity->unitControl.directInput) > 1.0F) {
                        entity->unitControl.directInput =
                            glm::normalize(entity->unitControl.directInput);
                    }
                }
            } else if constexpr (std::is_same_v<Type, MoveUnitCommand>) {
                if (!entity->unitControl)
                    return;
                entity->unitControl.order = UnitOrderKind::move;
                entity->unitControl.orderTarget = 0;
                entity->unitControl.strategicDestination = payload.destination;
                entity->unitControl.hasStrategicDestination = true;
                entity->transient.navigationPath =
                    navigation_.findPath(world_,
                                         entity->transform.position,
                                         payload.destination,
                                         collisionRadius(gameplay_, entity->archetype),
                                         entity->id);
                entity->transient.navigationWaypoint = 0;
                entity->transient.navigationRetrySeconds =
                    entity->transient.navigationPath.empty() ? 0.5F : 0.0F;
            } else if constexpr (std::is_same_v<Type, GatherResourceCommand>) {
                Entity* resource = world_.findEntity(payload.resource);
                if (entity->unitControl && entity->gatherer && resource && resource->resource &&
                    resource->resource.remaining > 0) {
                    entity->unitControl.order = UnitOrderKind::gather;
                    entity->unitControl.orderTarget = resource->id;
                    entity->unitControl.strategicDestination = resource->transform.position;
                    entity->unitControl.hasStrategicDestination = true;
                    entity->transient.navigationPath.clear();
                }
            } else if constexpr (std::is_same_v<Type, AttackEntityCommand>) {
                Entity* target = world_.findEntity(payload.target);
                if (entity->unitControl && entity->combat &&
                    entity->unitControl.directlyControllable && target && target->health &&
                    target->authority.owner != 0 && target->authority.owner != command.player) {
                    entity->unitControl.order = UnitOrderKind::attack;
                    entity->unitControl.orderTarget = target->id;
                    entity->transient.navigationPath.clear();
                }
            } else if constexpr (std::is_same_v<Type, StartRecipeCommand>) {
                if (!canStartRecipe(command.player, entity->id, payload.recipeId))
                    return;
                Player* player = players_.find(command.player);
                const RecipeDefinition* recipe = gameplay_.recipe(payload.recipeId);
                if (player && recipe && (recipe->product.kind == RecipeProductKind::unit ||
                                         recipe->product.kind == RecipeProductKind::resource)) {
                    for (const auto& [resource, amount] : recipe->cost)
                        player->resources[resource] -= amount;
                    const float duration =
                        gameplay_.productionDuration(player->countryId,
                                                     player->specializationId,
                                                     entity->archetype.value,
                                                     recipe->product.id,
                                                     entity->production.productionSpeedMultiplier);
                    ProductionOrder order;
                    order.kind = recipe->product.kind == RecipeProductKind::resource
                                     ? ProductionKind::processResource
                                     : ProductionKind::trainCharacter;
                    order.recipeId = recipe->id;
                    order.productId = recipe->product.id;
                    if (const EntityArchetype* product = gameplay_.archetype(recipe->product.id))
                        order.iconId = gameplay_.presentationIcon(
                            PresentationId{product->presentation});
                    else if (recipe->product.kind == RecipeProductKind::resource)
                        order.iconId = "resource_" + recipe->product.id;
                    order.amount = recipe->product.amount;
                    order.durationTicks = static_cast<std::uint32_t>(
                        std::max(1.0, std::ceil(duration / fixedTickSeconds)));
                    order.remainingTicks = order.durationTicks;
                    order.reservedCosts.insert(recipe->cost.begin(), recipe->cost.end());
                    entity->production.queue.push_back(std::move(order));
                }
            } else if constexpr (std::is_same_v<Type, PlaceBuildingCommand>) {
                Player* player = players_.find(command.player);
                const RecipeDefinition* recipe = gameplay_.recipe(RecipeId{payload.buildingId});
                if (!player || !entity->flight || !recipe || recipe->product.kind != RecipeProductKind::building)
                    return;
                const float footprintRadius = collisionRadius(gameplay_, recipe->product.id);
                if (overlapsObject(world_, gameplay_, {payload.position.x, payload.position.z},
                                   footprintRadius)) return;
                const EntityArchetype* buildingDefinition = gameplay_.archetype(recipe->product.id);
                TerrainFootprint shape = buildingDefinition && buildingDefinition->footprint
                                             ? *buildingDefinition->footprint
                                             : TerrainFootprint{FootprintShape::circle, footprintRadius,
                                                                {footprintRadius, footprintRadius}};
                const FootprintFit footprint = terrain_.fitFootprint(payload.position.x,
                                                                     payload.position.z, shape);
                if (!footprint.valid) return;
                for (const auto& [resource, amount] : recipe->cost)
                    if (player->resources[resource] < amount) return;
                for (const auto& [resource, amount] : recipe->cost) player->resources[resource] -= amount;
                Entity& building = world_.createEntity(recipe->product.id, recipe->product.id, command.player);
                gameplay_.initializeEntity(building);
                building.transform.position = {payload.position.x, 0.0F, payload.position.z};
                TerrainFoundation foundation = terrain_.evaluateFoundation(
                    building.id, payload.position.x, payload.position.z, shape);
                foundation.influence = 0.0F;
                building.transform.rotationDegrees.x = -glm::degrees(std::atan(foundation.gradient.y));
                building.transform.rotationDegrees.z = glm::degrees(std::atan(foundation.gradient.x));
                world_.foundations().push_back(foundation);
                terrain_.applyFoundation(foundation);
                building.construction.emplace();
                building.construction.recipeId = recipe->id;
                building.construction.powerRequired = recipe->constructionPower;
                building.construction.state = BuildingLifecycleState::planned;
                if (building.health) building.health.current = 1.0F;
                std::vector<EntityId> builders = payload.builders;
                if (builders.empty())
                    builders.push_back(payload.entity);
                std::sort(builders.begin(), builders.end());
                builders.erase(std::unique(builders.begin(), builders.end()), builders.end());
                for (EntityId builderId : builders) {
                    Entity* builder = world_.findEntity(builderId);
                    if (!builder || builder->authority.owner != command.player ||
                        !builder->flight || !builder->battery || !builder->unitControl)
                        continue;
                    builder->unitControl.order = UnitOrderKind::construct;
                    builder->unitControl.orderTarget = building.id;
                    builder->unitControl.strategicDestination = building.transform.position;
                    builder->unitControl.hasStrategicDestination = true;
                    builder->transient.navigationPath.clear();
                    builder->transient.navigationWaypoint = 0;
                }
            } else if constexpr (std::is_same_v<Type, ConstructCommand>) {
                Entity* building = world_.findEntity(payload.building);
                if (entity->flight && entity->battery && building && building->construction &&
                    !isOperational(*building)) {
                    entity->unitControl.order = UnitOrderKind::construct;
                    entity->unitControl.orderTarget = building->id;
                    entity->unitControl.strategicDestination = building->transform.position;
                    entity->unitControl.hasStrategicDestination = true;
                }
            } else if constexpr (std::is_same_v<Type, StopConstructionCommand>) {
                if (entity->unitControl) { entity->unitControl.order=UnitOrderKind::idle; entity->unitControl.orderTarget=0; entity->unitControl.hasStrategicDestination=false; }
            } else if constexpr (std::is_same_v<Type, CancelConstructionCommand>) {
                if (!entity->construction || isOperational(*entity)) return;
                const RecipeDefinition* recipe = gameplay_.recipe(RecipeId{entity->construction.recipeId});
                Player* player = players_.find(command.player);
                if (recipe && player) {
                    for (const auto& [resource, amount] : recipe->cost)
                        player->resources[resource] += amount;
                }
                const EntityId cancelled = entity->id;
                for (Entity& builder : world_.entities())
                    if (builder.unitControl && builder.unitControl.orderTarget == cancelled) {
                        builder.unitControl.order = UnitOrderKind::idle;
                        builder.unitControl.orderTarget = 0;
                        builder.unitControl.hasStrategicDestination = false;
                    }
                std::erase_if(world_.foundations(), [cancelled](const TerrainFoundation& foundation) {
                    return foundation.sourceEntity == cancelled;
                });
                terrain_.rebuildFoundations(world_.foundations());
                world_.destroyEntity(cancelled);
            } else if constexpr (std::is_same_v<Type, RepairCommand>) {
                Entity* target = world_.findEntity(payload.target);
                if (entity->flight && target && target->health) { entity->unitControl.order=UnitOrderKind::repair; entity->unitControl.orderTarget=target->id; entity->unitControl.strategicDestination=target->transform.position; entity->unitControl.hasStrategicDestination=true; }
            } else if constexpr (std::is_same_v<Type, StartUpgradeCommand>) {
                if (!canStartUpgrade(command.player, entity->id, payload.upgradeId))
                    return;
                const UpgradeDefinition* upgrade = gameplay_.upgrade(payload.upgradeId);
                Player* player = players_.find(command.player);
                const RecipeDefinition* recipe = gameplay_.recipe(RecipeId{upgrade->researchRecipe});
                for (const auto& [resource, amount] : recipe->cost)
                    player->resources[resource] -= amount;
                ProductionOrder order;
                order.kind = ProductionKind::improveTraining;
                order.recipeId = recipe->id;
                order.upgradeId = payload.upgradeId;
                order.iconId = upgrade->icon;
                order.durationTicks = recipe->durationTicks;
                order.remainingTicks = order.durationTicks;
                order.reservedCosts.insert(recipe->cost.begin(), recipe->cost.end());
                entity->production.queue.push_back(std::move(order));
            } else if constexpr (std::is_same_v<Type, CancelProductionCommand>) {
                if (!entity->production || payload.queueIndex >= entity->production.queue.size())
                    return;
                auto order = entity->production.queue.begin() + payload.queueIndex;
                if (order->kind != ProductionKind::improveTraining)
                    return;
                if (Player* player = players_.find(command.player))
                    for (const auto& [resource, amount] : order->reservedCosts)
                        player->resources[resource] += amount;
                entity->production.queue.erase(order);
            }
        },
        command.payload);
}

void GameSession::simulateTick() {
    while (!commands_.empty()) {
        apply(commands_.front());
        commands_.pop_front();
    }

    struct CompletedCharacter {
        PlayerId owner;
        glm::vec3 townPosition;
        std::string producerId;
        std::string productId;
        std::uint32_t amount;
    };
    std::vector<CompletedCharacter> completed;
    std::vector<EntityId> destroyed;
    for (Entity& entity : world_.entities()) {
        if (entity.flight && entity.battery && entity.battery.returningToCharge) {
            {
                    for (const Entity& hub : world_.entities()) {
                        if (hub.authority.owner != entity.authority.owner || !isOperational(hub))
                            continue;
                        if (hub.archetype.value != "command_hub" && hub.archetype.value != "town_center")
                            continue;
                        const EntityArchetype* hubType = gameplay_.archetype(hub.archetype);
                        if (!hubType || !hubType->powerDevice)
                            continue;
                        const PowerDeviceDefinition* charger = gameplay_.powerDevice(*hubType->powerDevice);
                        if (!charger || !charger->tags.contains("charger"))
                            continue;
                        const glm::vec3 offset = hub.transform.position - entity.transform.position;
                        if (glm::dot(offset, offset) <= 4.0F) {
                            entity.battery.charge = std::min(
                                entity.battery.capacity,
                                entity.battery.charge + charger->chargePerTick);
                            if (entity.battery.charge >= entity.battery.capacity) {
                                entity.battery.returningToCharge = false;
                                entity.unitControl.hasStrategicDestination = false;
                                entity.unitControl.order = UnitOrderKind::idle;
                            }
                            break;
                        }
                    }
            }
        }
        if (entity.production && isOperational(entity) && !entity.production.queue.empty()) {
            ProductionOrder& order = entity.production.queue.front();
            if (order.remainingTicks > 0)
                --order.remainingTicks;
            if (order.remainingTicks == 0) {
                if (order.kind == ProductionKind::trainCharacter)
                    completed.push_back({entity.authority.owner,
                                         entity.transform.position,
                                         entity.archetype.value,
                                         order.productId,
                                         order.amount});
                else if (order.kind == ProductionKind::processResource) {
                    if (Player* player = players_.find(entity.authority.owner))
                        player->resources[order.productId] += static_cast<float>(order.amount);
                }
                else if (order.kind == ProductionKind::upgradeBuilding) {
                    ++entity.buildingUpgrades.level;
                    const EntityArchetype* current = gameplay_.archetype(entity.archetype);
                    if (current && !current->upgradeTo.empty())
                        entity.archetype = EntityArchetypeId{current->upgradeTo};
                        entity.presentation = PresentationId{current->upgradeTo};
                    const float previousRatio = entity.health && entity.health.maximum > 0
                                                    ? entity.health.current / entity.health.maximum
                                                    : 1.0F;
                    const float maximum = stat(entity, GameplayStat::health);
                    if (entity.health && maximum > 0) {
                        entity.health.maximum = maximum;
                        entity.health.current = maximum * previousRatio;
                    }
                } else if (order.kind == ProductionKind::improveTraining) {
                    if (order.upgradeId.empty()) {
                        entity.production.productionSpeedMultiplier +=
                            stat(entity, GameplayStat::productionUpgradeAmount);
                        ++entity.production.productionSpeedUpgrades;
                    }
                    if (!order.upgradeId.empty()) {
                        if (!entity.upgrades)
                            entity.upgrades.emplace();
                        auto& level = entity.upgrades.levels[order.upgradeId];
                        if (const UpgradeDefinition* upgrade = gameplay_.upgrade(order.upgradeId);
                            upgrade && level < upgrade->maximumLevel)
                            ++level;
                        if (order.upgradeId == "building.town_center_level_2") {
                            entity.archetype = EntityArchetypeId{"town_center_level_2"};
                            entity.presentation = PresentationId{"town_center_level_2"};
                            if (entity.buildingUpgrades)
                                entity.buildingUpgrades.level = 2;
                        }
                    }
                }
                entity.production.queue.pop_front();
            }
        }
        if (!entity.unitControl)
            continue;
        if ((entity.unitControl.order == UnitOrderKind::construct || entity.unitControl.order == UnitOrderKind::repair) && entity.battery) {
            Entity* target = world_.findEntity(entity.unitControl.orderTarget);
            if (target) {
                const glm::vec2 d{target->transform.position.x-entity.transform.position.x,target->transform.position.z-entity.transform.position.z};
                if (glm::dot(d,d) <= 4.0F) {
                    if (entity.unitControl.order == UnitOrderKind::construct && target->construction) {
                        const RecipeDefinition* recipe = gameplay_.recipe(RecipeId{target->construction.recipeId});
                        if (recipe && entity.battery.charge >= recipe->dronePowerPerStep) {
                            entity.battery.charge -= recipe->dronePowerPerStep;
                            target->construction.state = BuildingLifecycleState::underConstruction;
                            target->construction.powerProgress = std::min(target->construction.powerRequired, target->construction.powerProgress + recipe->workStep);
                            const float progress = target->construction.powerRequired > 0.0F
                                                       ? target->construction.powerProgress /
                                                             target->construction.powerRequired
                                                       : 1.0F;
                            const auto foundation = std::find_if(
                                world_.foundations().begin(), world_.foundations().end(),
                                [&](const TerrainFoundation& item) {
                                    return item.sourceEntity == target->id;
                                });
                            if (foundation != world_.foundations().end() &&
                                foundation->influence != progress) {
                                foundation->influence = progress;
                                terrain_.rebuildFoundations(world_.foundations());
                            }
                            if (target->construction.powerProgress >= target->construction.powerRequired) {
                                target->construction.state = BuildingLifecycleState::operational;
                                if (target->health) target->health.current = target->health.maximum;
                                entity.unitControl.order=UnitOrderKind::idle; entity.unitControl.orderTarget=0; entity.unitControl.hasStrategicDestination=false;
                            }
                        }
                    } else if (entity.unitControl.order == UnitOrderKind::repair && target->health && target->health.current < target->health.maximum && entity.battery.charge >= 1.0F) {
                        entity.battery.charge -= 1.0F; target->health.current = std::min(target->health.maximum,target->health.current+2.0F);
                        if (target->construction && target->health.current >= target->health.maximum)
                            target->construction.state = BuildingLifecycleState::operational;
                    }
                }
            }
        }
        entity.transient.navigationRetrySeconds = std::max(
            0.0F, entity.transient.navigationRetrySeconds - static_cast<float>(fixedTickSeconds));
        const auto routeTo = [this, &entity](glm::vec3 destination) {
            const glm::vec2 change{destination.x - entity.unitControl.strategicDestination.x,
                                   destination.z - entity.unitControl.strategicDestination.z};
            if (!entity.unitControl.hasStrategicDestination || glm::dot(change, change) > 1.0F) {
                entity.unitControl.strategicDestination = destination;
                entity.unitControl.hasStrategicDestination = true;
                entity.transient.navigationPath.clear();
                entity.transient.navigationWaypoint = 0;
            }
        };
        if (entity.authority.directController == 0 && entity.unitControl.directlyControllable) {
            if (entity.unitControl.order == UnitOrderKind::gather) {
                Entity* node = world_.findEntity(entity.unitControl.orderTarget);
                if (!node || node->resource.remaining <= 0.0F)
                    entity.unitControl.order = entity.gatherer.carriedAmount > 0
                                                   ? UnitOrderKind::returnResources
                                                   : UnitOrderKind::idle;
                else if (entity.gatherer.carriedAmount >= stat(entity, GameplayStat::carryCapacity))
                    entity.unitControl.order = UnitOrderKind::returnResources;
                else {
                    const float reach =
                        collisionRadius(gameplay_, entity.archetype) +
                        collisionRadius(gameplay_, node->archetype) +
                        gameplay_.archetype(entity.archetype)->interactionMargin;
                    const glm::vec2 delta{node->transform.position.x - entity.transform.position.x,
                                          node->transform.position.z - entity.transform.position.z};
                    if (glm::dot(delta, delta) <= reach * reach) {
                        entity.unitControl.hasStrategicDestination = false;
                        entity.gatherer.carriedResource = node->resource.type.empty()
                                                              ? node->archetype.value
                                                              : node->resource.type;
                        const float capacity = stat(entity, GameplayStat::carryCapacity),
                                    rate = stat(entity, GameplayStat::gatherRate);
                        const float amount =
                            std::min({node->resource.remaining,
                                      rate * static_cast<float>(fixedTickSeconds),
                                      capacity - entity.gatherer.carriedAmount});
                        node->resource.remaining -= amount;
                        entity.gatherer.carriedAmount += amount;
                        if (entity.gatherer.carriedAmount >= capacity ||
                            node->resource.remaining <= 0)
                            entity.unitControl.order = UnitOrderKind::returnResources;
                    } else
                        routeTo(node->transform.position);
                }
            }
            if (entity.unitControl.order == UnitOrderKind::returnResources) {
                Entity* hall = nullptr;
                float nearest = std::numeric_limits<float>::max();
                for (Entity& candidate : world_.entities())
                    if (candidate.authority.owner == entity.authority.owner &&
                        isOperational(candidate) &&
                        gameplay_.archetype(candidate.archetype) &&
                        gameplay_.archetype(candidate.archetype)->tags.contains("resource-dropoff")) {
                        const glm::vec2 d{
                            candidate.transform.position.x - entity.transform.position.x,
                            candidate.transform.position.z - entity.transform.position.z};
                        const float distance = glm::dot(d, d);
                        if (distance < nearest) {
                            nearest = distance;
                            hall = &candidate;
                        }
                    }
                if (!hall)
                    entity.unitControl.order = UnitOrderKind::idle;
                else {
                    const float reach =
                        collisionRadius(gameplay_, entity.archetype) +
                        collisionRadius(gameplay_, hall->archetype) +
                        gameplay_.archetype(entity.archetype)->interactionMargin;
                    if (nearest <= reach * reach) {
                        if (Player* player = players_.find(entity.authority.owner)) {
                            player->resources[entity.gatherer.carriedResource] +=
                                entity.gatherer.carriedAmount;
                            if (entity.gatherer.carriedResource == "wood")
                                player->wood += entity.gatherer.carriedAmount;
                            else if (entity.gatherer.carriedResource == "materials")
                                player->wood += entity.gatherer.carriedAmount;
                            else if (entity.gatherer.carriedResource == "stone")
                                player->stone += entity.gatherer.carriedAmount;
                            else if (entity.gatherer.carriedResource == "gold")
                                player->gold += entity.gatherer.carriedAmount;
                        }
                        entity.gatherer.carriedAmount = 0;
                        entity.gatherer.carriedResource.clear();
                        Entity* node = world_.findEntity(entity.unitControl.orderTarget);
                        entity.unitControl.order = node && node->resource.remaining > 0
                                                       ? UnitOrderKind::gather
                                                       : UnitOrderKind::idle;
                        entity.unitControl.hasStrategicDestination = false;
                    } else
                        routeTo(hall->transform.position);
                }
            }
            if (entity.unitControl.order == UnitOrderKind::attack) {
                Entity* target = world_.findEntity(entity.unitControl.orderTarget);
                if (!target || !target->health || target->health.current <= 0 ||
                    target->authority.owner == entity.authority.owner)
                    entity.unitControl.order = UnitOrderKind::idle;
                else {
                    const glm::vec2 d{target->transform.position.x - entity.transform.position.x,
                                      target->transform.position.z - entity.transform.position.z};
                    const float range = collisionRadius(gameplay_, entity.archetype) +
                                        collisionRadius(gameplay_, target->archetype) +
                                        stat(entity, GameplayStat::attackRange);
                    if (glm::dot(d, d) <= range * range) {
                        entity.unitControl.hasStrategicDestination = false;
                        target->health.current -= stat(entity, GameplayStat::attackDamage) *
                                                  static_cast<float>(fixedTickSeconds);
                        if (target->construction && target->health.current > 0.0F &&
                            isOperational(*target))
                            target->construction.state = BuildingLifecycleState::damaged;
                        if (target->health.current <= 0)
                            destroyed.push_back(target->id);
                    } else
                        routeTo(target->transform.position);
                }
            }
        }
        glm::vec2 movement{0.0F};
        if (entity.authority.directController != 0) {
            movement = entity.unitControl.directInput;
        } else if (entity.unitControl.hasStrategicDestination) {
            if (entity.flight) {
                const glm::vec2 delta{
                    entity.unitControl.strategicDestination.x - entity.transform.position.x,
                    entity.unitControl.strategicDestination.z - entity.transform.position.z};
                if (glm::length(delta) < 0.35F) {
                    entity.unitControl.hasStrategicDestination = false;
                    if (entity.unitControl.order == UnitOrderKind::move)
                        entity.unitControl.order = UnitOrderKind::idle;
                } else {
                    movement = glm::normalize(delta);
                }
            } else if (entity.transient.navigationWaypoint >= entity.transient.navigationPath.size()) {
                if (entity.transient.navigationRetrySeconds > 0.0F)
                    continue;
                entity.transient.navigationPath =
                    navigation_.findPath(world_,
                                         entity.transform.position,
                                         entity.unitControl.strategicDestination,
                                         collisionRadius(gameplay_, entity.archetype),
                                         entity.id);
                entity.transient.navigationWaypoint = 0;
                if (entity.transient.navigationPath.empty()) {
                    entity.unitControl.hasStrategicDestination = false;
                    entity.transient.navigationRetrySeconds = 0.5F;
                    if (entity.unitControl.order == UnitOrderKind::move)
                        entity.unitControl.order = UnitOrderKind::idle;
                    continue;
                }
            }
            if (!entity.flight) {
                const glm::vec3 waypoint =
                    entity.transient.navigationPath[entity.transient.navigationWaypoint];
                const glm::vec2 delta{waypoint.x - entity.transform.position.x,
                                      waypoint.z - entity.transform.position.z};
                if (glm::length(delta) < 0.35F) {
                    ++entity.transient.navigationWaypoint;
                    if (entity.transient.navigationWaypoint >= entity.transient.navigationPath.size()) {
                        entity.unitControl.hasStrategicDestination = false;
                        if (entity.unitControl.order == UnitOrderKind::move)
                            entity.unitControl.order = UnitOrderKind::idle;
                    }
                } else {
                    movement = glm::normalize(delta);
                }
            }
        }
        if (!entity.flight && glm::length(movement) > 0.001F &&
            entity.unitControl.directlyControllable) {
            glm::vec2 separation{0};
            for (const Entity& other : world_.entities())
                if (other.id != entity.id && !other.flight && other.unitControl.directlyControllable) {
                    const glm::vec2 away{entity.transform.position.x - other.transform.position.x,
                                         entity.transform.position.z - other.transform.position.z};
                    const float distance = glm::length(away),
                                desired = collisionRadius(gameplay_, entity.archetype) +
                                          collisionRadius(gameplay_, other.archetype) + 0.45F;
                    if (distance > 0.001F && distance < desired)
                        separation += (away / distance) * (desired - distance) / desired;
                }
            movement += separation * 0.8F;
            if (glm::length(movement) > 1.0F)
                movement = glm::normalize(movement);
        }
        if (glm::length(movement) > 0.001F && entity.authority.directController == 0) {
            entity.transform.rotationDegrees.y = glm::degrees(std::atan2(movement.x, movement.y));
        }
        const float speedMultiplier = entity.unitControl.running ? 1.65F : 1.0F;
        const glm::vec2 current{entity.transform.position.x, entity.transform.position.z};
        const float resolvedMovementSpeed = entity.authority.owner
                                                ? stat(entity, GameplayStat::movementSpeed)
                                                : entity.unitControl.movementSpeed;
        const glm::vec2 delta = movement * resolvedMovementSpeed * speedMultiplier *
                                static_cast<float>(fixedTickSeconds);
        const bool flying = entity.flight.present;
        if (flying && entity.battery && glm::length(delta) > 0.001F) {
            entity.battery.charge = std::max(0.0F, entity.battery.charge -
                entity.battery.movementDrainPerSecond * static_cast<float>(fixedTickSeconds));
            if (entity.battery.charge <= entity.battery.reserveThreshold)
                entity.battery.returningToCharge = true;
        }
        const float radius = flying ? 0.0F : collisionRadius(gameplay_, entity.archetype);
        const float boundary =
            static_cast<float>(Terrain::cellCount) * Terrain::spacing * 0.5F - radius;
        const auto validPosition = [this, &entity, radius, boundary, flying](glm::vec2 candidate) {
            return std::abs(candidate.x) <= boundary && std::abs(candidate.y) <= boundary &&
                   (flying || !overlapsObject(world_, gameplay_, candidate, radius, entity.id));
        };
        if (validPosition(current + delta)) {
            entity.transform.position.x = current.x + delta.x;
            entity.transform.position.z = current.y + delta.y;
        } else if (validPosition(current + glm::vec2{delta.x, 0.0F})) {
            entity.transform.position.x = current.x + delta.x;
        } else if (validPosition(current + glm::vec2{0.0F, delta.y})) {
            entity.transform.position.z = current.y + delta.y;
        } else if (entity.unitControl.hasStrategicDestination) {
            entity.transient.navigationPath.clear();
            entity.transient.navigationWaypoint = 0;
            entity.transient.navigationRetrySeconds = 0.25F;
        }
        if (flying) {
            entity.flight.altitude = std::clamp(entity.flight.altitude,
                                                entity.flight.minimumAltitude,
                                                entity.flight.maximumAltitude);
            entity.transform.position.y = entity.flight.altitude;
            if (entity.battery && entity.battery.returningToCharge && !entity.unitControl.hasStrategicDestination) {
                const Entity* nearest = nullptr;
                float best = std::numeric_limits<float>::max();
                for (const Entity& candidate : world_.entities()) {
                    const auto* candidateType = gameplay_.archetype(candidate.archetype);
                    if (!candidateType || candidate.authority.owner != entity.authority.owner ||
                        !isOperational(candidate) || !candidateType->powerDevice) continue;
                    const auto* device = gameplay_.powerDevice(*candidateType->powerDevice);
                    if (!device || !device->tags.contains("charger")) continue;
                    const glm::vec3 d = candidate.transform.position - entity.transform.position;
                    const float distance = glm::dot(d, d);
                    if (distance < best) { best = distance; nearest = &candidate; }
                }
                if (nearest) {
                    entity.unitControl.strategicDestination = nearest->transform.position;
                    entity.unitControl.hasStrategicDestination = true;
                    entity.unitControl.order = UnitOrderKind::returnResources;
                }
            }
        }
    }
    std::sort(destroyed.begin(), destroyed.end());
    destroyed.erase(std::unique(destroyed.begin(), destroyed.end()), destroyed.end());
    for (EntityId id : destroyed) {
        if (Entity* entity = world_.findEntity(id); entity && entity->construction)
            entity->construction.state = BuildingLifecycleState::destroyed;
        for (Entity& worker : world_.entities())
            if (worker.unitControl && worker.unitControl.orderTarget == id) {
                worker.unitControl.order = UnitOrderKind::idle;
                worker.unitControl.orderTarget = 0;
                worker.unitControl.hasStrategicDestination = false;
            }
        world_.destroyEntity(id);
    }
    for (const CompletedCharacter& character : completed) {
        const EntityArchetype* producer = gameplay_.archetype(character.producerId);
        if (!producer)
            continue;
        const float spawnDistance = collisionRadius(gameplay_, character.producerId) +
                                    collisionRadius(gameplay_, character.productId) +
                                    producer->spawnClearance;
        for (std::uint32_t produced = 0; produced < character.amount; ++produced) {
          for (std::uint32_t candidate = 0; candidate < producer->spawnCandidateCount; ++candidate) {
            const float angle = static_cast<float>(candidate) * glm::two_pi<float>() /
                                static_cast<float>(producer->spawnCandidateCount);
            const glm::vec2 position{character.townPosition.x + std::cos(angle) * spawnDistance,
                                     character.townPosition.z + std::sin(angle) * spawnDistance};
            if (overlapsObject(
                    world_, gameplay_, position, collisionRadius(gameplay_, character.productId)))
                continue;
            const EntityArchetype* definition = gameplay_.archetype(character.productId);
            Entity& unit = world_.createEntity(definition && !definition->nameKey.empty()
                                                   ? Text::get(definition->nameKey)
                                                   : character.productId,
                                               character.productId,
                                               character.owner);
            initializeEntity(unit);
            unit.transform.position = {position.x, 0.0F, position.y};
            unit.unitControl.directlyControllable = unit.archetype.value != "construction_drone";
            break;
          }
        }
    }
    updateExploration();
    ++tick_;
}

void GameSession::updateExploration() {
    constexpr float extent = Terrain::cellCount * Terrain::spacing;
    for (Player& player : players_.players()) {
        std::fill(player.visible.begin(), player.visible.end(), 0);
        for (const Entity& entity : world_.entities()) {
            if (entity.authority.owner != player.id || !entity.vision || !isOperational(entity))
                continue;
            const float elevation =
                terrain_.heightAt(entity.transform.position.x, entity.transform.position.z);
            const float radius =
                effectiveSightRange(stat(entity, GameplayStat::sightRange), elevation);
            const int centerX = static_cast<int>((entity.transform.position.x / extent + 0.5F) *
                                                 Player::explorationCells);
            const int centerZ = static_cast<int>((entity.transform.position.z / extent + 0.5F) *
                                                 Player::explorationCells);
            const int cells = static_cast<int>(radius / extent * Player::explorationCells) + 1;
            for (int z = std::max(0, centerZ - cells);
                 z <= std::min(Player::explorationCells - 1, centerZ + cells);
                 ++z)
                for (int x = std::max(0, centerX - cells);
                     x <= std::min(Player::explorationCells - 1, centerX + cells);
                     ++x) {
                    const float wx =
                        (static_cast<float>(x) + 0.5F) / Player::explorationCells * extent -
                        extent * 0.5F;
                    const float wz =
                        (static_cast<float>(z) + 0.5F) / Player::explorationCells * extent -
                        extent * 0.5F;
                    if ((wx - entity.transform.position.x) * (wx - entity.transform.position.x) +
                            (wz - entity.transform.position.z) *
                                (wz - entity.transform.position.z) <=
                        radius * radius) {
                        const auto index =
                            static_cast<std::size_t>(z * Player::explorationCells + x);
                        player.visible[index] = 255;
                        player.discovered[index] = 255;
                    }
                }
        }
        const auto cellVisible = [&](glm::vec3 position) {
            const int x = std::clamp(
                          static_cast<int>((position.x / extent + 0.5F) * Player::explorationCells),
                          0,
                          Player::explorationCells - 1),
                      z = std::clamp(
                          static_cast<int>((position.z / extent + 0.5F) * Player::explorationCells),
                          0,
                          Player::explorationCells - 1);
            return player.visible[static_cast<std::size_t>(z * Player::explorationCells + x)] != 0;
        };
        for (const Entity& entity : world_.entities())
            if (entity.authority.owner != player.id && cellVisible(entity.transform.position)) {
                const bool depleted = entity.resource && entity.resource.remaining <= 0.0F;
                auto known =
                    std::find_if(player.intelligence.begin(),
                                 player.intelligence.end(),
                                 [&](const LastKnownEntity& item) { return item.id == entity.id; });
                if (depleted) {
                    if (known != player.intelligence.end())
                        player.intelligence.erase(known);
                    continue;
                }
                LastKnownEntity snapshot{entity.id,
                                         entity.archetype.value,
                                         entity.transform.position,
                                         entity.transform.rotationDegrees,
                                         entity.transform.scale,
                                         entity.kind == EntityKind::building};
                if (known == player.intelligence.end())
                    player.intelligence.push_back(std::move(snapshot));
                else
                    *known = std::move(snapshot);
            }
        player.intelligence.erase(
            std::remove_if(player.intelligence.begin(),
                           player.intelligence.end(),
                           [&](const LastKnownEntity& known) {
                               if (!cellVisible(known.position))
                                   return false;
                               const Entity* entity = world_.findEntity(known.id);
                               return !entity ||
                                      (entity->resource && entity->resource.remaining <= 0.0F);
                           }),
            player.intelligence.end());
    }
}

void GameSession::replaceWorld(std::vector<Entity> entities, std::uint32_t terrainSeed,
                               std::vector<TerrainFoundation> foundations) {
    world_.replaceEntities(std::move(entities));
    world_.replaceFoundations(std::move(foundations));
    terrainSeed_ = terrainSeed;
    terrain_ = Terrain{terrainSeed};
    terrain_.rebuildFoundations(world_.foundations());
    navigation_.rebuildTerrain(terrain_);
    commands_.clear();
    lastSequence_.clear();
    updateExploration();
}

void GameSession::restorePlayerProgress(PlayerId id,
                                        float wood,
                                        float stone,
                                        float gold,
                                        std::map<std::string, float> resources,
                                        std::vector<std::uint8_t> discovered,
                                        std::vector<LastKnownEntity> intelligence) {
    if (Player* player = players_.find(id)) {
        player->wood = wood;
        player->stone = stone;
        player->gold = gold;
        if (!resources.empty())
            player->resources = std::move(resources);
        if (discovered.size() == player->discovered.size())
            player->discovered = std::move(discovered);
        player->intelligence = std::move(intelligence);
    }
}

} // namespace strategy
