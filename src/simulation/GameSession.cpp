#include "simulation/GameSession.hpp"
#include "simulation/StateChecksum.hpp"

#include "localization/Text.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/StartingPlacement.hpp"
#include "world/Navigation.hpp"
#include "world/MapArea.hpp"
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

namespace {
float boundaryClearance(const DefinitionRegistry& definitions,
                        const Entity& actor,
                        const Entity& target) {
    const glm::vec2 actorPosition{actor.transform.position.x, actor.transform.position.z};
    return signedDistance(spatialShape(definitions, target), actorPosition) -
           collisionRadius(definitions, actor.archetype);
}

float interactionRange(const DefinitionRegistry& definitions,
                       const Entity& actor,
                       std::string_view action) {
    const EntityArchetype* definition = definitions.archetype(actor.archetype);
    if (!definition) return 0.0F;
    if (const auto found = definition->interactionRanges.find(std::string{action});
        found != definition->interactionRanges.end()) return found->second;
    return definition->interactionMargin;
}

NavigationProfile navigationProfile(const DefinitionRegistry& definitions,
                                    const Entity& entity) {
    const EntityArchetype* archetype = definitions.archetype(entity.archetype);
    return archetype ? NavigationProfile{archetype->movement.domains,
                                         archetype->movement.ignoresEntityObstacles}
                     : NavigationProfile{};
}

bool canReceiveCargo(const Entity& entity) {
    return entity.processor && entity.power && entity.power.enabled && isOperational(entity) &&
           entity.power.state == PowerOperationalState::powered;
}

void routeToInteraction(const DefinitionRegistry& definitions,
                        Entity& actor,
                        const Entity& target,
                        float range) {
    const glm::vec2 actorPosition{actor.transform.position.x, actor.transform.position.z};
    const float actorRadius = collisionRadius(definitions, actor.archetype);
    const SpatialShape approachBoundary =
        expanded(spatialShape(definitions, target), actorRadius + std::max(0.0F, range * 0.5F));
    const glm::vec2 destination = closestBoundaryPoint(approachBoundary, actorPosition);
    const glm::vec2 previous{actor.unitControl.strategicDestination.x,
                             actor.unitControl.strategicDestination.z};
    actor.transient.navigationGoalEntity = target.id;
    actor.transient.navigationInteractionRange = range;
    if (!actor.unitControl.hasStrategicDestination ||
        glm::dot(destination - previous, destination - previous) > 0.25F) {
        actor.unitControl.strategicDestination = {destination.x, target.transform.position.y,
                                                  destination.y};
        actor.unitControl.hasStrategicDestination = true;
        actor.transient.navigationPath.clear();
        actor.transient.navigationWaypoint = 0;
    }
}
} // namespace

std::uint64_t GameSession::stateChecksum() const {
    return authoritativeStateChecksum(world_, players_, terrainSeed_, tick_, mapChunksPerSide_);
}

void GameSession::beginRecharge(Entity& entity) {
    if (!entity.unitControl || !entity.battery || entity.battery.returningToCharge)
        return;
    const UnitOrderKind order = entity.unitControl.order;
    if (order != UnitOrderKind::idle && order != UnitOrderKind::returningToCharge &&
        order != UnitOrderKind::charging && order != UnitOrderKind::stranded) {
        entity.battery.hasSuspendedOrder = true;
        entity.battery.suspendedOrder = order;
        entity.battery.suspendedTarget = entity.unitControl.orderTarget;
        entity.battery.suspendedDestination = entity.unitControl.strategicDestination;
        entity.battery.suspendedHasDestination = entity.unitControl.hasStrategicDestination;
    }
    entity.battery.returningToCharge = true;
    entity.battery.chargerTarget = 0;
    entity.unitControl.order = UnitOrderKind::returningToCharge;
    entity.unitControl.orderTarget = 0;
    entity.unitControl.hasStrategicDestination = false;
    entity.unitControl.directInput = {0.0F, 0.0F};
    entity.unitControl.running = false;
    entity.transient.navigationPath.clear();
    entity.transient.navigationWaypoint = 0;
    entity.transient.navigationGoalEntity = 0;
    entity.transient.navigationInteractionRange = 0.0F;
}

void GameSession::finishRecharge(Entity& entity) {
    entity.battery.returningToCharge = false;
    entity.battery.chargerTarget = 0;
    if (entity.battery.hasSuspendedOrder) {
        entity.unitControl.order = entity.battery.suspendedOrder;
        entity.unitControl.orderTarget = entity.battery.suspendedTarget;
        entity.unitControl.strategicDestination = entity.battery.suspendedDestination;
        entity.unitControl.hasStrategicDestination = entity.battery.suspendedHasDestination;
        entity.battery.hasSuspendedOrder = false;
        entity.battery.suspendedOrder = UnitOrderKind::idle;
        entity.battery.suspendedTarget = 0;
        entity.battery.suspendedHasDestination = false;
    } else {
        entity.unitControl.order = UnitOrderKind::idle;
        entity.unitControl.orderTarget = 0;
        entity.unitControl.hasStrategicDestination = false;
    }
}

void GameSession::stopUnit(Entity& entity) {
    if (!entity.unitControl) return;
    entity.unitControl.order = UnitOrderKind::idle;
    entity.unitControl.orderTarget = 0;
    entity.unitControl.hasStrategicDestination = false;
    entity.unitControl.directInput = {0.0F, 0.0F};
    entity.unitControl.running = false;
    entity.transient.navigationPath.clear();
    entity.transient.navigationWaypoint = 0;
    entity.transient.navigationGoalEntity = 0;
    entity.transient.navigationInteractionRange = 0.0F;
    if (entity.gatherer) {
        entity.gatherer.sourceTarget = 0;
        entity.gatherer.deliveryTarget = 0;
        entity.gatherer.preferredOutput.clear();
        entity.gatherer.repeatGathering = false;
        entity.gatherer.waitingForProcessor = false;
    }
    if (entity.battery) {
        entity.battery.returningToCharge = false;
        entity.battery.hasSuspendedOrder = false;
        entity.battery.suspendedOrder = UnitOrderKind::idle;
        entity.battery.suspendedTarget = 0;
        entity.battery.suspendedHasDestination = false;
        entity.battery.chargerTarget = 0;
    }
}

GameSession::GameSession(const DefinitionRegistry& definitions,
                         std::uint32_t terrainSeed,
                         std::string playerOneCountry,
                         std::string playerTwoCountry,
                         std::string playerOneSpecialization,
                         std::string playerTwoSpecialization,
                         std::uint32_t mapChunksPerSide,
                         float startingResourcesScale,
                         float resourceAbundanceScale)
    : players_(std::move(playerOneCountry),
               std::move(playerTwoCountry),
               std::move(playerOneSpecialization),
               std::move(playerTwoSpecialization))
    , gameplay_(definitions)
    , terrainSeed_(terrainSeed)
    , terrain_(terrainSeed)
    , mapChunksPerSide_(std::clamp(mapChunksPerSide, 10U,
                                  static_cast<std::uint32_t>(Terrain::chunksPerSide)))
    , navigation_(terrain_, gameplay_, mapChunksPerSide_)
    , resourceAbundanceScale_(std::clamp(resourceAbundanceScale, 0.5F, 2.0F)) {
    for (Player& player : players_.players())
        for (const auto& [resource, amount] : gameplay_.matchRules().startingResources)
            player.resources[resource] =
                amount * std::clamp(startingResourcesScale, 0.0F, 4.0F);
    for (const StartingRegion& region : selectStartingRegions(
             terrain_, gameplay_, mapChunksPerSide_, players_.players().size()))
        startingAnchors_.push_back(region.anchor);
    const auto createStartingEntities = [this](PlayerId player,
                                               const auto& starts,
                                               float rotation) {
        for (const StartingEntityDefinition& start : starts) {
            Entity& entity =
                world_.createEntity(Text::get(start.nameKey), start.archetype, player);
            initializeEntity(entity);
            entity.transform.position = startingEntityPosition(
                start, startingAnchors_.at(static_cast<std::size_t>(player - 1)));
            if (!entity.flight)
                entity.transform.position.y = 0.0F;
            else
                entity.flight.altitude = start.position.y;
            entity.transform.rotationDegrees.y = rotation;
            if (entity.unitControl)
                entity.unitControl.directlyControllable = start.directlyControllable;
            if (!start.gatheringEnabled)
                entity.gatherer.reset();
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
    populateResources(world_, terrain_, gameplay_, terrainSeed,
                      mapChunksPerSide_, resourceAbundanceScale_, startingAnchors_);
    populateVegetation(world_, terrain_, gameplay_, terrainSeed_, mapChunksPerSide_);
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
                    if (entity->battery &&
                        (entity->battery.charge <= 0.0F ||
                         entity->battery.returningToCharge)) {
                        if (!entity->battery.returningToCharge)
                            beginRecharge(*entity);
                        entity->unitControl.directInput = {0.0F, 0.0F};
                        entity->unitControl.running = false;
                        return;
                    }
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
                if (entity->battery &&
                    (entity->battery.charge <= 0.0F ||
                     entity->battery.returningToCharge)) {
                    if (!entity->battery.returningToCharge)
                        beginRecharge(*entity);
                    return;
                }
                entity->unitControl.order = UnitOrderKind::move;
                entity->unitControl.orderTarget = 0;
                entity->unitControl.strategicDestination = payload.destination;
                entity->unitControl.hasStrategicDestination = true;
                entity->transient.navigationPath =
                    navigation_.findPath(world_,
                                         entity->transform.position,
                                         payload.destination,
                                         collisionRadius(gameplay_, entity->archetype),
                                         entity->id,
                                         navigationProfile(gameplay_, *entity));
                entity->transient.navigationWaypoint = 0;
                if (!entity->transient.navigationPath.empty()) {
                    entity->unitControl.strategicDestination =
                        entity->transient.navigationPath.back();
                    entity->transient.navigationRetrySeconds = 0.0F;
                } else {
                    entity->unitControl.hasStrategicDestination = false;
                    entity->unitControl.order = UnitOrderKind::idle;
                    entity->transient.navigationRetrySeconds = 0.0F;
                }
            } else if constexpr (std::is_same_v<Type, GatherResourceCommand>) {
                Entity* resource = world_.findEntity(payload.resource);
                if (entity->battery && entity->battery.charge <= 0.0F) {
                    beginRecharge(*entity);
                    return;
                }
                if (entity->unitControl && entity->gatherer && resource && resource->resource &&
                    resource->resource.remaining > 0 &&
                    (!entity->battery || !entity->battery.returningToCharge)) {
                    if (entity->gatherer.carriedAmount > 0.0F &&
                        entity->gatherer.carriedResource != resource->resource.type)
                        return;
                    if (payload.processor != 0) {
                        const Entity* processor = world_.findEntity(payload.processor);
                        if (!processor || processor->authority.owner != command.player ||
                            !processor->processor || !isOperational(*processor) ||
                            !gameplay_.acceptsResource(
                                BuildingArchetypeId{processor->archetype.value},
                                ResourceId{resource->resource.type}))
                            return;
                    }
                    const float reach = interactionRange(gameplay_, *entity, "gather");
                    if (boundaryClearance(gameplay_, *entity, *resource) > reach &&
                        navigation_.findPath(
                            world_, entity->transform.position,
                            NavigationGoalRegion{spatialShape(gameplay_, *resource), reach,
                                {entity->transform.position.x, entity->transform.position.z},
                                resource->id, entity->id},
                            collisionRadius(gameplay_, entity->archetype), entity->id,
                            navigationProfile(gameplay_, *entity)).empty()) {
                        resourceEvents_.push_back({ResourceEventKind::sourceInaccessible, tick_,
                                                   command.player, entity->id, resource->id,
                                                   resource->resource.type, 0.0F});
                        return;
                    }
                    entity->unitControl.order = UnitOrderKind::gather;
                    entity->unitControl.orderTarget = resource->id;
                    entity->gatherer.sourceTarget = resource->id;
                    entity->gatherer.preferredProcessor = payload.processor;
                    if (payload.processor != 0) {
                        const Entity* processor = world_.findEntity(payload.processor);
                        if (const auto* conversion = gameplay_.conversionFor(
                                BuildingArchetypeId{processor->archetype.value},
                                ResourceId{resource->resource.type}))
                            entity->gatherer.preferredOutput = conversion->output.value;
                    }
                    entity->gatherer.deliveryTarget = 0;
                    entity->gatherer.repeatGathering = true;
                    entity->gatherer.waitingForProcessor = false;
                    entity->unitControl.strategicDestination = resource->transform.position;
                    entity->unitControl.hasStrategicDestination = true;
                    entity->transient.navigationPath.clear();
                    resourceEvents_.push_back({ResourceEventKind::gatheringStarted, tick_,
                                               command.player, entity->id, resource->id,
                                               resource->resource.type, 0.0F});
                }
            } else if constexpr (std::is_same_v<Type, DeliverResourceCommand>) {
                Entity* processor = world_.findEntity(payload.processor);
                if (!entity->unitControl || !entity->gatherer ||
                    entity->gatherer.carriedAmount <= 0.0F || !processor ||
                    processor->authority.owner != command.player || !processor->processor ||
                    !isOperational(*processor) ||
                    !gameplay_.acceptsResource(BuildingArchetypeId{processor->archetype.value},
                                               ResourceId{entity->gatherer.carriedResource}))
                    return;
                entity->gatherer.preferredProcessor = processor->id;
                if (const auto* conversion = gameplay_.conversionFor(
                        BuildingArchetypeId{processor->archetype.value},
                        ResourceId{entity->gatherer.carriedResource}))
                    entity->gatherer.preferredOutput = conversion->output.value;
                entity->gatherer.deliveryTarget = processor->id;
                entity->gatherer.waitingForProcessor = false;
                entity->unitControl.order = UnitOrderKind::returnResources;
                entity->unitControl.orderTarget = entity->gatherer.sourceTarget;
                entity->unitControl.hasStrategicDestination = false;
                entity->transient.navigationPath.clear();
            } else if constexpr (std::is_same_v<Type, SetPreferredProcessorCommand>) {
                if (!entity->gatherer) return;
                if (payload.processor != 0) {
                    const Entity* processor = world_.findEntity(payload.processor);
                    if (!processor || processor->authority.owner != command.player ||
                        !processor->processor || !isOperational(*processor))
                        return;
                }
                entity->gatherer.preferredProcessor = payload.processor;
                entity->gatherer.deliveryTarget = 0;
            } else if constexpr (std::is_same_v<Type, SetDeliveryOutputCommand>) {
                if (!entity->gatherer || payload.output.empty()) return;
                const std::string input = !entity->gatherer.carriedResource.empty()
                    ? entity->gatherer.carriedResource
                    : [&]() {
                        const Entity* source = world_.findEntity(entity->gatherer.sourceTarget);
                        return source && source->resource ? source->resource.type : std::string{};
                    }();
                if (input != "synthetic") return;
                bool validRoute = false;
                for (const Entity& candidate : world_.entities()) {
                    if (candidate.authority.owner != command.player || !candidate.processor) continue;
                    const auto* conversion = gameplay_.conversionFor(
                        BuildingArchetypeId{candidate.archetype.value}, ResourceId{input});
                    if (conversion && conversion->output.value == payload.output) {
                        validRoute = true;
                        break;
                    }
                }
                if (!validRoute) return;
                entity->gatherer.preferredOutput = payload.output;
                entity->gatherer.preferredProcessor = 0;
                entity->gatherer.deliveryTarget = 0;
            } else if constexpr (std::is_same_v<Type, ConnectPowerCommand>) {
                Entity* target = world_.findEntity(payload.target);
                const auto reject = [&](PowerFailureReason reason) {
                    powerEvents_.push_back({PowerEventKind::commandRejected, tick_, command.player,
                                            entity->id, payload.target, 0, reason});
                };
                if (!entity->power || !target || !target->power || target->id == entity->id) {
                    reject(PowerFailureReason::invalidTarget);
                    return;
                }
                if (target->authority.owner != command.player) {
                    reject(PowerFailureReason::enemyTarget);
                    return;
                }
                if (!isOperational(*entity) || !isOperational(*target)) {
                    reject(PowerFailureReason::notOperational);
                    return;
                }
                if (entity->power.maximumConnections == 0 || target->power.maximumConnections == 0)
                    return;
                auto& sourceConnections = entity->power.connections;
                auto& targetConnections = target->power.connections;
                std::sort(sourceConnections.begin(), sourceConnections.end());
                std::sort(targetConnections.begin(), targetConnections.end());
                if (std::binary_search(sourceConnections.begin(), sourceConnections.end(), target->id) ||
                    sourceConnections.size() >= entity->power.maximumConnections ||
                    targetConnections.size() >= target->power.maximumConnections)
                {
                    reject(PowerFailureReason::connectionLimit);
                    return;
                }
                const glm::vec2 sourcePosition{entity->transform.position.x, entity->transform.position.z};
                const glm::vec2 targetPosition{target->transform.position.x, target->transform.position.z};
                const float maximumRange = std::min(entity->power.connectionRange,
                                                    target->power.connectionRange);
                if (maximumRange <= 0.0F || glm::distance(sourcePosition, targetPosition) > maximumRange)
                {
                    reject(PowerFailureReason::outOfRange);
                    return;
                }
                sourceConnections.insert(std::lower_bound(sourceConnections.begin(), sourceConnections.end(),
                                                          target->id), target->id);
                targetConnections.insert(std::lower_bound(targetConnections.begin(), targetConnections.end(),
                                                          entity->id), entity->id);
                powerEvents_.push_back({PowerEventKind::connectionCreated, tick_, command.player,
                                        entity->id, target->id, 0});
            } else if constexpr (std::is_same_v<Type, DisconnectPowerCommand>) {
                Entity* target = world_.findEntity(payload.target);
                if (!entity->power || !target || !target->power ||
                    target->authority.owner != command.player)
                    return;
                if (std::find(entity->power.connections.begin(), entity->power.connections.end(),
                              target->id) == entity->power.connections.end() ||
                    std::find(target->power.connections.begin(), target->power.connections.end(),
                              entity->id) == target->power.connections.end()) {
                    powerEvents_.push_back({PowerEventKind::commandRejected, tick_, command.player,
                                            entity->id, payload.target, 0,
                                            PowerFailureReason::notConnected});
                    return;
                }
                std::erase(entity->power.connections, target->id);
                std::erase(target->power.connections, entity->id);
                powerEvents_.push_back({PowerEventKind::connectionRemoved, tick_, command.player,
                                        entity->id, target->id, 0});
            } else if constexpr (std::is_same_v<Type, SetPowerPriorityCommand>) {
                if (entity->power)
                    entity->power.priority = std::clamp(payload.priority, 0, 1000);
            } else if constexpr (std::is_same_v<Type, SetPowerEnabledCommand>) {
                if (entity->power)
                    entity->power.enabled = payload.enabled;
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
                const EntityArchetype* buildingDefinition = gameplay_.archetype(recipe->product.id);
                const float footprintRadius = collisionRadius(gameplay_, recipe->product.id);
                TerrainFootprint shape = buildingDefinition && buildingDefinition->footprint
                                             ? *buildingDefinition->footprint
                                             : TerrainFootprint{FootprintShape::circle, footprintRadius,
                                                                {footprintRadius, footprintRadius}};
                const SpatialShape placementShape = spatialShape(
                    gameplay_, EntityArchetypeId{recipe->product.id},
                    {payload.position.x, payload.position.z}, 0.0F);
                const glm::vec2 bounds = axisAlignedHalfExtents(placementShape);
                const MapArea map{mapChunksPerSide_};
                if (std::abs(placementShape.center.x) + bounds.x > map.halfExtent() ||
                    std::abs(placementShape.center.y) + bounds.y > map.halfExtent()) return;
                if (overlapsObject(world_, gameplay_, placementShape, 0, true)) return;
                const TerrainPlacementResult placement = terrain_.evaluatePlacement(
                    payload.position.x, payload.position.z, shape,
                    buildingDefinition ? buildingDefinition->placement
                                       : TerrainPlacementProfile{});
                if (!placement.valid()) return;
                for (const auto& [resource, amount] : recipe->cost)
                    if (player->resources[resource] < amount) return;
                for (const auto& [resource, amount] : recipe->cost) player->resources[resource] -= amount;
                clearVegetationWithin(world_, gameplay_, placementShape);
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
                if (building.health) building.health.current = 0.0F;
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
                    if (builder->battery.charge <= 0.0F)
                        beginRecharge(*builder);
                }
            } else if constexpr (std::is_same_v<Type, ConstructCommand>) {
                Entity* building = world_.findEntity(payload.building);
                if (entity->battery && entity->battery.charge <= 0.0F) {
                    beginRecharge(*entity);
                    return;
                }
                if (entity->flight && entity->battery && building && building->construction &&
                    !isOperational(*building) && !entity->battery.returningToCharge) {
                    entity->unitControl.order = UnitOrderKind::construct;
                    entity->unitControl.orderTarget = building->id;
                    entity->unitControl.strategicDestination = building->transform.position;
                    entity->unitControl.hasStrategicDestination = true;
                }
            } else if constexpr (std::is_same_v<Type, StopConstructionCommand>) {
                stopUnit(*entity);
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
                if (entity->battery && entity->battery.charge <= 0.0F) {
                    beginRecharge(*entity);
                    return;
                }
                if (entity->flight && target && target->health && entity->battery &&
                    !entity->battery.returningToCharge) { entity->unitControl.order=UnitOrderKind::repair; entity->unitControl.orderTarget=target->id; entity->unitControl.strategicDestination=target->transform.position; entity->unitControl.hasStrategicDestination=true; }
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
                if (Player* player = players_.find(command.player))
                    for (const auto& [resource, amount] : order->reservedCosts)
                        player->resources[resource] += amount;
                entity->production.queue.erase(order);
            } else if constexpr (std::is_same_v<Type, RechargeCommand>) {
                if (entity->flight && entity->battery && entity->unitControl)
                    beginRecharge(*entity);
            } else if constexpr (std::is_same_v<Type, StopUnitCommand>) {
                stopUnit(*entity);
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
    struct PreviousPowerState {
        EntityId entity{0};
        PowerOperationalState state{PowerOperationalState::offline};
        std::uint64_t gridId{0};
    };
    std::vector<PreviousPowerState> previousPowerStates;
    for (Entity& entity : world_.entities()) {
        if (!entity.power) continue;
        previousPowerStates.push_back({entity.id, entity.power.state, entity.power.gridId});
        auto& connections = entity.power.connections;
        std::sort(connections.begin(), connections.end());
        connections.erase(std::unique(connections.begin(), connections.end()), connections.end());
        std::erase_if(connections, [&](EntityId connectedId) {
            const Entity* connected = world_.findEntity(connectedId);
            if (connectedId == entity.id || !connected || !connected->power ||
                connected->authority.owner != entity.authority.owner)
                return true;
            auto reverse = connected->power.connections;
            std::sort(reverse.begin(), reverse.end());
            return !std::binary_search(reverse.begin(), reverse.end(), entity.id);
        });
        entity.power.supplied = 0.0F;
        entity.power.gridId = 0;
        entity.power.state = PowerOperationalState::offline;
    }
    for (Player& player : players_.players()) {
        std::vector<Entity*> devices;
        std::uint64_t topologySignature = 1469598103934665603ULL;
        const auto mixTopology = [&](std::uint64_t value) {
            topologySignature ^= value;
            topologySignature *= 1099511628211ULL;
        };
        for (Entity& candidate : world_.entities()) {
            if (candidate.authority.owner == player.id && candidate.power) {
                mixTopology(candidate.id);
                mixTopology(candidate.power.enabled);
                mixTopology(isOperational(candidate));
                for (EntityId adjacent : candidate.power.connections) mixTopology(adjacent);
            }
            if (candidate.authority.owner == player.id && candidate.power &&
                candidate.power.enabled && isOperational(candidate))
                devices.push_back(&candidate);
        }
        std::sort(devices.begin(), devices.end(), [](const Entity* left, const Entity* right) {
            return left->id < right->id;
        });
        if (!powerTopologySignatures_.contains(player.id) ||
            powerTopologySignatures_[player.id] != topologySignature) {
            powerTopologySignatures_[player.id] = topologySignature;
            auto& cached = cachedPowerComponents_[player.id];
            cached.clear();
            std::vector<EntityId> visited;
            for (Entity* root : devices) {
                if (std::binary_search(visited.begin(), visited.end(), root->id)) continue;
                std::vector<EntityId> component;
                std::vector<EntityId> pending{root->id};
                while (!pending.empty()) {
                    const EntityId currentId = pending.front();
                    pending.erase(pending.begin());
                    if (std::binary_search(visited.begin(), visited.end(), currentId)) continue;
                    Entity* current = world_.findEntity(currentId);
                    if (!current || !current->power || !current->power.enabled ||
                        !isOperational(*current) || current->authority.owner != player.id)
                        continue;
                    visited.insert(std::lower_bound(visited.begin(), visited.end(), currentId), currentId);
                    component.push_back(currentId);
                    for (EntityId adjacent : current->power.connections)
                        if (!std::binary_search(visited.begin(), visited.end(), adjacent) &&
                            !std::binary_search(pending.begin(), pending.end(), adjacent))
                            pending.insert(std::upper_bound(pending.begin(), pending.end(), adjacent), adjacent);
                }
                if (!component.empty()) cached.push_back(std::move(component));
            }
        }
        for (const auto& cachedComponent : cachedPowerComponents_[player.id]) {
            std::vector<Entity*> component;
            component.reserve(cachedComponent.size());
            for (EntityId id : cachedComponent)
                if (Entity* entity = world_.findEntity(id)) component.push_back(entity);
            if (component.empty()) continue;
            const std::uint64_t gridId = component.front()->id;
            std::vector<Entity*> consumers;
            std::vector<Entity*> storage;
            std::map<EntityId, float> generationRemaining;
            std::map<EntityId, float> storageDischargeRemaining;
            std::map<EntityId, float> throughputRemaining;
            std::map<std::pair<EntityId, EntityId>, float> edgeRemaining;
            for (Entity* device : component) {
                device->power.gridId = gridId;
                generationRemaining[device->id] = device->power.generation;
                throughputRemaining[device->id] = device->power.transferLimit > 0.0F
                    ? device->power.transferLimit : std::numeric_limits<float>::max();
                if (device->power.demand > 0.0F) consumers.push_back(device);
                if (device->power.storageCapacity > 0.0F) {
                    storage.push_back(device);
                    const EntityArchetype* type = gameplay_.archetype(device->archetype);
                    const PowerDeviceDefinition* definition =
                        type && type->powerDevice ? gameplay_.powerDevice(*type->powerDevice) : nullptr;
                    storageDischargeRemaining[device->id] =
                        definition ? definition->storageDischargePerTick : 0.0F;
                }
                if (device->power.demand <= 0.0F)
                    device->power.state = PowerOperationalState::powered;
                for (EntityId adjacent : device->power.connections) {
                    if (adjacent <= device->id) continue;
                    const Entity* target = world_.findEntity(adjacent);
                    if (!target || !target->power || !target->power.enabled ||
                        target->authority.owner != player.id || !isOperational(*target)) continue;
                    const float sourceLimit = device->power.transferLimit > 0.0F
                                                  ? device->power.transferLimit
                                                  : std::numeric_limits<float>::max();
                    const float targetLimit = target->power.transferLimit > 0.0F
                                                  ? target->power.transferLimit
                                                  : std::numeric_limits<float>::max();
                    edgeRemaining[{device->id, adjacent}] = std::min(sourceLimit, targetLimit);
                }
            }
            std::sort(consumers.begin(), consumers.end(), [](const Entity* left, const Entity* right) {
                return left->power.priority != right->power.priority
                           ? left->power.priority < right->power.priority
                           : left->id < right->id;
            });
            const auto route = [&](EntityId source, EntityId target) {
                std::map<EntityId, EntityId> parent;
                std::vector<EntityId> frontier{source};
                parent[source] = 0;
                for (std::size_t cursor = 0; cursor < frontier.size(); ++cursor) {
                    const EntityId currentId = frontier[cursor];
                    if (currentId == target) break;
                    const Entity* current = world_.findEntity(currentId);
                    if (!current || !current->power) continue;
                    for (EntityId adjacent : current->power.connections) {
                        const auto edge = std::minmax(currentId, adjacent);
                        const auto capacity = edgeRemaining.find(edge);
                        if (capacity == edgeRemaining.end() || capacity->second <= 0.0F ||
                            parent.contains(adjacent) || !throughputRemaining.contains(adjacent) ||
                            (adjacent != target && throughputRemaining.at(adjacent) <= 0.0F))
                            continue;
                        parent[adjacent] = currentId;
                        frontier.push_back(adjacent);
                    }
                }
                std::vector<EntityId> path;
                if (!parent.contains(target)) return path;
                for (EntityId current = target; current != source; current = parent.at(current))
                    path.push_back(current);
                path.push_back(source);
                std::reverse(path.begin(), path.end());
                return path;
            };
            const auto deliver = [&](EntityId target, float requested, bool allowStorage) {
                float delivered = 0.0F;
                for (Entity* source : component) {
                    if (delivered >= requested) break;
                    float sourceAvailable = generationRemaining[source->id];
                    if (allowStorage)
                        sourceAvailable += std::min(source->power.stored,
                                                    storageDischargeRemaining[source->id]);
                    while (sourceAvailable > 0.0F && delivered < requested) {
                        const auto path = route(source->id, target);
                        if (path.empty()) break;
                        float amount = std::min(sourceAvailable, requested - delivered);
                        amount = std::min(amount, throughputRemaining[source->id]);
                        amount = std::min(amount, throughputRemaining[target]);
                        for (std::size_t index = 1; index < path.size(); ++index)
                            amount = std::min(amount, edgeRemaining[std::minmax(path[index - 1], path[index])]);
                        for (std::size_t index = 1; index + 1 < path.size(); ++index)
                            amount = std::min(amount, throughputRemaining[path[index]]);
                        if (amount <= 0.0F) break;
                        const float generated = std::min(amount, generationRemaining[source->id]);
                        generationRemaining[source->id] -= generated;
                        const float discharged = amount - generated;
                        source->power.stored -= discharged;
                        storageDischargeRemaining[source->id] -= discharged;
                        sourceAvailable -= amount;
                        delivered += amount;
                        for (std::size_t index = 1; index < path.size(); ++index)
                            edgeRemaining[std::minmax(path[index - 1], path[index])] -= amount;
                        throughputRemaining[source->id] -= amount;
                        if (target != source->id) throughputRemaining[target] -= amount;
                        for (std::size_t index = 1; index + 1 < path.size(); ++index)
                            throughputRemaining[path[index]] -= amount;
                    }
                }
                return delivered;
            };
            for (Entity* consumer : consumers) {
                const float intakeLimit = consumer->power.transferLimit > 0.0F
                                              ? consumer->power.transferLimit
                                              : consumer->power.demand;
                const float requested = std::min(consumer->power.demand, intakeLimit);
                consumer->power.supplied = deliver(consumer->id, requested, true);
                consumer->power.state = consumer->power.supplied >= consumer->power.demand
                                            ? PowerOperationalState::powered
                                            : consumer->power.supplied > 0.0F
                                                  ? PowerOperationalState::underpowered
                                                  : PowerOperationalState::offline;
            }
            for (Entity* battery : storage) {
                const EntityArchetype* type = gameplay_.archetype(battery->archetype);
                const PowerDeviceDefinition* definition =
                    type && type->powerDevice ? gameplay_.powerDevice(*type->powerDevice) : nullptr;
                const float rate = definition ? definition->storageChargePerTick : 0.0F;
                const float request = std::min(rate,
                    battery->power.storageCapacity - battery->power.stored);
                const float charged = deliver(battery->id, std::max(0.0F, request), false);
                battery->power.stored += charged;
            }
        }
    }
    for (const PreviousPowerState& previous : previousPowerStates) {
        const Entity* entity = world_.findEntity(previous.entity);
        if (!entity || !entity->power || entity->power.demand <= 0.0F || tick_ == 0)
            continue;
        if (previous.state == entity->power.state) continue;
        PowerEventKind event = PowerEventKind::shortage;
        if (entity->power.state == PowerOperationalState::powered)
            event = PowerEventKind::recovered;
        else if (entity->power.state == PowerOperationalState::offline)
            event = PowerEventKind::shutdown;
        powerEvents_.push_back({event, tick_, entity->authority.owner, entity->id, 0,
                                entity->power.gridId});
    }
    for (Entity& entity : world_.entities()) {
        if (entity.resource && isOperational(entity)) {
            const EntityArchetype* type = gameplay_.archetype(entity.archetype);
            if (type && type->rawProductionPerTick > 0.0F && entity.power &&
                entity.power.state == PowerOperationalState::powered)
                entity.resource.remaining = std::min(
                    type->resourceCapacity,
                    entity.resource.remaining + type->rawProductionPerTick);
        }
        if (entity.processor && isOperational(entity)) {
            Player* player = players_.find(entity.authority.owner);
            if (entity.processor.activityTicksRemaining > 0)
                --entity.processor.activityTicksRemaining;
            bool blocked = false;
            bool converted = false;
            if (player)
                for (auto input = entity.processor.bufferedInputs.begin();
                     input != entity.processor.bufferedInputs.end();) {
                    const ResourceConversionDefinition* conversion = gameplay_.conversionFor(
                        BuildingArchetypeId{entity.archetype.value}, ResourceId{input->first});
                    if (!conversion || !entity.power ||
                        entity.power.supplied < conversion->requiredPower) {
                        blocked = blocked || (conversion && entity.power &&
                                              entity.power.supplied < conversion->requiredPower);
                        if (blocked && !entity.processor.waitingForPower)
                            resourceEvents_.push_back({ResourceEventKind::waitingForPower, tick_,
                                                       entity.authority.owner, entity.id, entity.id,
                                                       input->first, input->second});
                        ++input;
                        continue;
                    }
                    player->resources[conversion->output.value] +=
                        input->second * conversion->outputPerInput;
                    resourceEvents_.push_back({ResourceEventKind::conversionCompleted, tick_,
                                               entity.authority.owner, entity.id, entity.id,
                                               conversion->output.value,
                                               input->second * conversion->outputPerInput});
                    converted = true;
                    entity.processor.lastConversionTick = tick_;
                    entity.processor.activityTicksRemaining = 30;
                    input = entity.processor.bufferedInputs.erase(input);
                }
            entity.processor.waitingForPower = blocked;
            if (converted || entity.processor.activityTicksRemaining > 0)
                entity.processor.state = ProcessorOperationalState::processing;
            else if (blocked)
                entity.processor.state = ProcessorOperationalState::blocked;
            else if (!entity.power || entity.power.state == PowerOperationalState::offline)
                entity.processor.state = ProcessorOperationalState::offline;
            else if (entity.power.state == PowerOperationalState::underpowered)
                entity.processor.state = ProcessorOperationalState::underpowered;
            else if (entity.processor.bufferedInputs.empty())
                entity.processor.state = ProcessorOperationalState::idle;
            else
                entity.processor.state = ProcessorOperationalState::powered;
        }
        if (entity.flight && entity.battery && entity.battery.returningToCharge) {
            const Entity* chargerEntity = world_.findEntity(entity.battery.chargerTarget);
            const auto validCharger = [&](const Entity* candidate) {
                if (!candidate || candidate->authority.owner != entity.authority.owner ||
                    !isOperational(*candidate)) return false;
                const EntityArchetype* type = gameplay_.archetype(candidate->archetype);
                if (!type || !type->powerDevice) return false;
                const PowerDeviceDefinition* device = gameplay_.powerDevice(*type->powerDevice);
                return device && device->tags.contains("charger") &&
                       device->chargePerTick > 0.0F && candidate->power &&
                       candidate->power.enabled &&
                       candidate->power.state != PowerOperationalState::offline;
            };
            if (!validCharger(chargerEntity)) {
                chargerEntity = nullptr;
                float nearestDistance = std::numeric_limits<float>::max();
                for (const Entity& candidate : world_.entities()) {
                    if (!validCharger(&candidate)) continue;
                    const float distance = boundaryClearance(gameplay_, entity, candidate);
                    if (distance < nearestDistance ||
                        (distance == nearestDistance && chargerEntity && candidate.id < chargerEntity->id)) {
                        nearestDistance = distance;
                        chargerEntity = &candidate;
                    }
                }
                entity.battery.chargerTarget = chargerEntity ? chargerEntity->id : 0;
            }
            if (!chargerEntity) {
                entity.unitControl.order = UnitOrderKind::stranded;
                entity.unitControl.orderTarget = 0;
                entity.unitControl.hasStrategicDestination = false;
            } else {
                const EntityArchetype* chargerType = gameplay_.archetype(chargerEntity->archetype);
                const PowerDeviceDefinition* charger =
                    gameplay_.powerDevice(*chargerType->powerDevice);
                const float chargeRange = interactionRange(gameplay_, entity, "charge");
                if (boundaryClearance(gameplay_, entity, *chargerEntity) <= chargeRange) {
                    entity.unitControl.order = UnitOrderKind::charging;
                    entity.unitControl.orderTarget = chargerEntity->id;
                    entity.unitControl.hasStrategicDestination = false;
                    const float powerRatio = chargerEntity->power->demand > 0.0F
                        ? std::clamp(chargerEntity->power->supplied /
                                         chargerEntity->power->demand, 0.0F, 1.0F)
                        : 1.0F;
                    entity.battery.charge = std::min(
                        entity.battery.capacity,
                        entity.battery.charge + charger->chargePerTick * powerRatio);
                    if (entity.battery.charge >= entity.battery.capacity)
                        finishRecharge(entity);
                } else {
                    entity.unitControl.order = UnitOrderKind::returningToCharge;
                    entity.unitControl.orderTarget = chargerEntity->id;
                    routeToInteraction(gameplay_, entity, *chargerEntity, chargeRange);
                }
            }
        }
        if (entity.production && isOperational(entity) && !entity.production.queue.empty()) {
            ProductionOrder& order = entity.production.queue.front();
            std::uint32_t progress = 1000;
            if (entity.power && entity.power.demand > 0.0F)
                progress = static_cast<std::uint32_t>(std::clamp(
                    std::lround(entity.power.supplied / entity.power.demand * 1000.0F),
                    0L, 1000L));
            entity.production.powerProgressPermille += progress;
            while (order.remainingTicks > 0 && entity.production.powerProgressPermille >= 1000) {
                --order.remainingTicks;
                entity.production.powerProgressPermille -= 1000;
            }
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
                const float range = interactionRange(
                    gameplay_, entity,
                    entity.unitControl.order == UnitOrderKind::construct ? "construct" : "repair");
                if (boundaryClearance(gameplay_, entity, *target) <= range) {
                    entity.unitControl.hasStrategicDestination = false;
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
                            if (target->health)
                                target->health.current = target->health.maximum * progress;
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
                            if (entity.battery.charge <= 0.0F &&
                                entity.unitControl.order == UnitOrderKind::construct)
                                beginRecharge(entity);
                        } else if (entity.battery.charge <= 0.0F) {
                            beginRecharge(entity);
                        }
                    } else if (entity.unitControl.order == UnitOrderKind::repair && target->health && target->health.current < target->health.maximum && entity.battery.charge >= 1.0F) {
                        entity.battery.charge -= 1.0F; target->health.current = std::min(target->health.maximum,target->health.current+2.0F);
                        if (target->construction && target->health.current >= target->health.maximum)
                            target->construction.state = BuildingLifecycleState::operational;
                        if (entity.battery.charge <= 0.0F)
                            beginRecharge(entity);
                    }
                } else routeToInteraction(gameplay_, entity, *target, range);
            } else {
                stopUnit(entity);
            }
        }
        entity.transient.navigationRetrySeconds = std::max(
            0.0F, entity.transient.navigationRetrySeconds - static_cast<float>(fixedTickSeconds));
        if (entity.authority.directController == 0) {
            if (entity.unitControl.order == UnitOrderKind::gather) {
                Entity* node = world_.findEntity(entity.unitControl.orderTarget);
                if (!node || !node->resource || node->resource.remaining <= 0.0F) {
                    resourceEvents_.push_back({ResourceEventKind::sourceDepleted, tick_,
                                               entity.authority.owner, entity.id,
                                               node ? node->id : entity.gatherer.sourceTarget,
                                               entity.gatherer.carriedResource, 0.0F});
                    if (!node || !node->resource)
                        entity.gatherer.sourceTarget = 0;
                    entity.unitControl.order = entity.gatherer.carriedAmount > 0
                                                   ? UnitOrderKind::returnResources
                                                   : UnitOrderKind::idle;
                } else if (entity.gatherer.carriedAmount >= stat(entity, GameplayStat::carryCapacity))
                    entity.unitControl.order = UnitOrderKind::returnResources;
                else {
                    const float reach = interactionRange(gameplay_, entity, "gather");
                    if (boundaryClearance(gameplay_, entity, *node) <= reach) {
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
                            node->resource.remaining <= 0) {
                            if (entity.gatherer.carriedAmount >= capacity)
                                resourceEvents_.push_back({ResourceEventKind::cargoFull, tick_,
                                                           entity.authority.owner, entity.id, node->id,
                                                           entity.gatherer.carriedResource,
                                                           entity.gatherer.carriedAmount});
                            entity.unitControl.order = UnitOrderKind::returnResources;
                        }
                    } else
                        routeToInteraction(gameplay_, entity, *node, reach);
                }
            }
            if (entity.unitControl.order == UnitOrderKind::returnResources) {
                Entity* hall = nullptr;
                float nearest = std::numeric_limits<float>::max();
                const auto compatible = [&](Entity& candidate) {
                    const auto* conversion = gameplay_.conversionFor(
                        BuildingArchetypeId{candidate.archetype.value},
                        ResourceId{entity.gatherer.carriedResource});
                    return conversion && candidate.processor && isOperational(candidate) &&
                           candidate.authority.owner == entity.authority.owner &&
                           (entity.gatherer.preferredOutput.empty() ||
                            conversion->output.value == entity.gatherer.preferredOutput);
                };
                const auto hasCapacity = [&](const Entity& candidate) {
                    const EntityArchetype* definition = gameplay_.archetype(candidate.archetype);
                    float buffered = 0.0F;
                    if (candidate.processor)
                        for (const auto& [resource, amount] : candidate.processor.bufferedInputs)
                            buffered += amount;
                    return !definition || definition->processorCapacity <= 0.0F ||
                           buffered + 0.001F < definition->processorCapacity;
                };
                const float depositReach = interactionRange(gameplay_, entity, "deposit");
                const auto reachable = [&](Entity& candidate) {
                    if (boundaryClearance(gameplay_, entity, candidate) <= depositReach)
                        return true;
                    return !navigation_.findPath(
                        world_, entity.transform.position,
                        NavigationGoalRegion{spatialShape(gameplay_, candidate), depositReach,
                            {entity.transform.position.x, entity.transform.position.z},
                            candidate.id, entity.id},
                        collisionRadius(gameplay_, entity.archetype), entity.id,
                        navigationProfile(gameplay_, entity)).empty();
                };
                if (entity.gatherer.deliveryTarget != 0) {
                    Entity* committed = world_.findEntity(entity.gatherer.deliveryTarget);
                    if (committed && compatible(*committed) && hasCapacity(*committed) &&
                        reachable(*committed)) {
                        hall = committed;
                        nearest = boundaryClearance(gameplay_, entity, *committed);
                    } else {
                        const EntityId lost = entity.gatherer.deliveryTarget;
                        if (entity.gatherer.preferredProcessor == lost)
                            entity.gatherer.preferredProcessor = 0;
                        entity.gatherer.deliveryTarget = 0;
                        if (!committed || !compatible(*committed) || !reachable(*committed))
                            resourceEvents_.push_back({ResourceEventKind::destinationLost, tick_,
                                                       entity.authority.owner, entity.id, lost,
                                                       entity.gatherer.carriedResource,
                                                       entity.gatherer.carriedAmount});
                    }
                }
                if (!hall && entity.gatherer.preferredProcessor != 0) {
                    Entity* preferred = world_.findEntity(entity.gatherer.preferredProcessor);
                    if (preferred && compatible(*preferred) && hasCapacity(*preferred) &&
                        reachable(*preferred)) {
                        hall = preferred;
                        nearest = boundaryClearance(gameplay_, entity, *preferred);
                    } else {
                        entity.gatherer.preferredProcessor = 0;
                        entity.gatherer.deliveryTarget = 0;
                        resourceEvents_.push_back({ResourceEventKind::destinationLost, tick_,
                                                   entity.authority.owner, entity.id,
                                                   preferred ? preferred->id : 0,
                                                   entity.gatherer.carriedResource,
                                                   entity.gatherer.carriedAmount});
                    }
                }
                if (!hall) {
                    // Dedicated processors always win automatic selection. Distance is only
                    // compared inside the dedicated or fallback-hub class.
                    for (const bool allowHub : {false, true}) {
                        for (Entity& candidate : world_.entities()) {
                            if (!compatible(candidate) || !hasCapacity(candidate) ||
                                !canReceiveCargo(candidate) || !reachable(candidate))
                                continue;
                            const bool isHub = candidate.archetype.value == "command_hub";
                            if (isHub != allowHub) continue;
                            const float distance = boundaryClearance(gameplay_, entity, candidate);
                            if (!hall || distance < nearest - 0.001F ||
                                (std::abs(distance - nearest) <= 0.001F && candidate.id < hall->id)) {
                                nearest = distance;
                                hall = &candidate;
                            }
                        }
                        if (hall) break;
                    }
                }
                if (!hall) {
                    entity.unitControl.order = UnitOrderKind::idle;
                    entity.unitControl.hasStrategicDestination = false;
                    entity.gatherer.deliveryTarget = 0;
                    entity.gatherer.waitingForProcessor = true;
                } else {
                    entity.gatherer.deliveryTarget = hall->id;
                    entity.gatherer.waitingForProcessor = false;
                    const float reach = depositReach;
                    if (nearest <= reach) {
                        if (!canReceiveCargo(*hall)) {
                            entity.gatherer.waitingForProcessor = true;
                            entity.unitControl.hasStrategicDestination = false;
                            entity.transient.navigationPath.clear();
                            entity.transient.navigationWaypoint = 0;
                            continue;
                        }
                        const EntityArchetype* hallDefinition = gameplay_.archetype(hall->archetype);
                        float buffered = 0.0F;
                        for (const auto& [resource, amount] : hall->processor.bufferedInputs)
                            buffered += amount;
                        const float available = !hallDefinition || hallDefinition->processorCapacity <= 0.0F
                            ? entity.gatherer.carriedAmount
                            : std::max(0.0F, hallDefinition->processorCapacity - buffered);
                        const float deposited = std::min(entity.gatherer.carriedAmount, available);
                        hall->processor.bufferedInputs[entity.gatherer.carriedResource] += deposited;
                        resourceEvents_.push_back({ResourceEventKind::deliveryCompleted, tick_,
                                                   entity.authority.owner, entity.id, hall->id,
                                                   entity.gatherer.carriedResource,
                                                   deposited});
                        entity.gatherer.carriedAmount -= deposited;
                        if (entity.gatherer.carriedAmount <= 0.001F) {
                            entity.gatherer.carriedAmount = 0;
                            entity.gatherer.carriedResource.clear();
                        }
                        entity.gatherer.deliveryTarget = 0;
                        Entity* node = world_.findEntity(entity.gatherer.sourceTarget);
                        entity.unitControl.order = entity.gatherer.carriedAmount > 0.0F
                            ? UnitOrderKind::returnResources
                            : entity.gatherer.repeatGathering && node &&
                                                           node->resource && node->resource.remaining > 0
                                                       ? UnitOrderKind::gather
                                                       : UnitOrderKind::idle;
                        entity.unitControl.orderTarget = entity.unitControl.order == UnitOrderKind::gather
                                                             ? entity.gatherer.sourceTarget : 0;
                        entity.unitControl.hasStrategicDestination = false;
                    } else
                        routeToInteraction(gameplay_, entity, *hall, reach);
                }
            }
            if (entity.unitControl.order == UnitOrderKind::attack) {
                Entity* target = world_.findEntity(entity.unitControl.orderTarget);
                if (!target || !target->health ||
                    (target->health.current <= 0 &&
                     (!target->construction || isOperational(*target))) ||
                    target->authority.owner == entity.authority.owner)
                    entity.unitControl.order = UnitOrderKind::idle;
                else {
                    const float range = stat(entity, GameplayStat::attackRange);
                    if (boundaryClearance(gameplay_, entity, *target) <= range) {
                        entity.unitControl.hasStrategicDestination = false;
                        target->health.current -= stat(entity, GameplayStat::attackDamage) *
                                                  static_cast<float>(fixedTickSeconds);
                        if (target->construction && target->health.current > 0.0F &&
                            isOperational(*target))
                            target->construction.state = BuildingLifecycleState::damaged;
                        if (target->health.current <= 0)
                            destroyed.push_back(target->id);
                    } else
                        routeToInteraction(gameplay_, entity, *target, range);
                }
            }
        }
        glm::vec2 movement{0.0F};
        if (entity.authority.directController != 0) {
            movement = entity.unitControl.directInput;
        } else if (entity.unitControl.hasStrategicDestination) {
            if (entity.transient.navigationWaypoint >= entity.transient.navigationPath.size()) {
                if (entity.transient.navigationRetrySeconds > 0.0F)
                    continue;
                const Entity* navigationTarget =
                    world_.findEntity(entity.transient.navigationGoalEntity);
                if (navigationTarget) {
                    entity.transient.navigationPath = navigation_.findPath(
                        world_, entity.transform.position,
                        NavigationGoalRegion{
                            spatialShape(gameplay_, *navigationTarget),
                            entity.transient.navigationInteractionRange,
                            {entity.transform.position.x, entity.transform.position.z},
                            navigationTarget->id,
                            entity.id},
                        collisionRadius(gameplay_, entity.archetype), entity.id,
                        navigationProfile(gameplay_, entity));
                } else {
                    entity.transient.navigationPath = navigation_.findPath(
                        world_, entity.transform.position,
                        entity.unitControl.strategicDestination,
                        collisionRadius(gameplay_, entity.archetype), entity.id,
                        navigationProfile(gameplay_, entity));
                }
                entity.transient.navigationWaypoint = 0;
                if (entity.transient.navigationPath.empty()) {
                    entity.unitControl.hasStrategicDestination = false;
                    entity.transient.navigationRetrySeconds = 0.5F;
                    if (entity.unitControl.order == UnitOrderKind::gather && entity.gatherer) {
                        resourceEvents_.push_back({ResourceEventKind::sourceInaccessible, tick_,
                                                   entity.authority.owner, entity.id,
                                                   entity.gatherer.sourceTarget,
                                                   entity.gatherer.carriedResource, 0.0F});
                        entity.gatherer.sourceTarget = 0;
                        entity.unitControl.order = entity.gatherer.carriedAmount > 0.0F
                                                       ? UnitOrderKind::returnResources
                                                       : UnitOrderKind::idle;
                    } else if (entity.unitControl.order == UnitOrderKind::returnResources &&
                               entity.gatherer) {
                        resourceEvents_.push_back({ResourceEventKind::destinationInaccessible, tick_,
                                                   entity.authority.owner, entity.id,
                                                   entity.gatherer.deliveryTarget,
                                                   entity.gatherer.carriedResource,
                                                   entity.gatherer.carriedAmount});
                        if (entity.gatherer.preferredProcessor == entity.gatherer.deliveryTarget)
                            entity.gatherer.preferredProcessor = 0;
                        entity.gatherer.deliveryTarget = 0;
                    } else if (entity.unitControl.order == UnitOrderKind::move)
                        entity.unitControl.order = UnitOrderKind::idle;
                    continue;
                }
            }
            if (entity.transient.navigationWaypoint < entity.transient.navigationPath.size()) {
                const glm::vec3 waypoint = entity.transient.navigationPath[
                    entity.transient.navigationWaypoint];
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
        const float terrainSpeedMultiplier =
            !entity.flight && terrain_.traversalAt(current.x, current.y) ==
                                  TerrainTraversalClass::difficult
                ? 0.65F
                : 1.0F;
        const glm::vec2 delta = movement * resolvedMovementSpeed * speedMultiplier *
                                terrainSpeedMultiplier *
                                static_cast<float>(fixedTickSeconds);
        const bool flying = entity.flight.present;
        if (flying && entity.battery && glm::length(delta) > 0.001F) {
            entity.battery.charge = std::max(0.0F, entity.battery.charge -
                entity.battery.movementDrainPerSecond * static_cast<float>(fixedTickSeconds));
            if (entity.battery.charge <= 0.0F)
                beginRecharge(entity);
        }
        const float radius = flying ? 0.0F : collisionRadius(gameplay_, entity.archetype);
        const NavigationProfile movementProfile = navigationProfile(gameplay_, entity);
        const MapArea map{mapChunksPerSide_};
        const auto validPosition = [this, &entity, radius, map, flying,
                                    movementProfile](glm::vec2 candidate) {
            return map.contains(candidate, radius) &&
                   terrain_.movementCostAt(candidate.x, candidate.y,
                                           movementProfile.domains) > 0.0F &&
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
    const MapArea map{mapChunksPerSide_};
    const float extent = map.extent();
    for (Player& player : players_.players()) {
        std::fill(player.visible.begin(), player.visible.end(), 0);
        for (const Entity& entity : world_.entities()) {
            if (entity.authority.owner != player.id || !entity.vision || !isOperational(entity))
                continue;
            float powerFactor = 1.0F;
            if (entity.power && entity.power.demand > 0.0F) {
                if (!entity.power.enabled || entity.power.state == PowerOperationalState::offline)
                    continue;
                powerFactor = std::clamp(entity.power.supplied / entity.power.demand, 0.0F, 1.0F);
            }
            const float elevation =
                terrain_.heightAt(entity.transform.position.x, entity.transform.position.z);
            const float radius =
                effectiveSightRange(stat(entity, GameplayStat::sightRange), elevation) * powerFactor;
            const glm::ivec2 center = map.gridCell(
                {entity.transform.position.x, entity.transform.position.z},
                Player::explorationCells);
            const int cells = static_cast<int>(radius / extent * Player::explorationCells) + 1;
            for (int z = std::max(0, center.y - cells);
                 z <= std::min(Player::explorationCells - 1, center.y + cells);
                 ++z)
                for (int x = std::max(0, center.x - cells);
                     x <= std::min(Player::explorationCells - 1, center.x + cells);
                     ++x) {
                    const glm::vec2 world =
                        map.gridCellCenter({x, z}, Player::explorationCells);
                    const float wx = world.x;
                    const float wz = world.y;
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
            const glm::ivec2 cell =
                map.gridCell({position.x, position.z}, Player::explorationCells);
            return player.visible[static_cast<std::size_t>(
                       cell.y * Player::explorationCells + cell.x)] != 0;
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
                                         entity.renderId(),
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
                               std::vector<TerrainFoundation> foundations,
                               std::uint32_t mapChunksPerSide) {
    world_.replaceEntities(std::move(entities));
    world_.replaceFoundations(std::move(foundations));
    terrainSeed_ = terrainSeed;
    mapChunksPerSide_ = std::clamp(mapChunksPerSide, 10U,
                                   static_cast<std::uint32_t>(Terrain::chunksPerSide));
    terrain_ = Terrain{terrainSeed};
    terrain_.rebuildFoundations(world_.foundations());
    navigation_.rebuildTerrain(terrain_, mapChunksPerSide_);
    commands_.clear();
    lastSequence_.clear();
    powerTopologySignatures_.clear();
    cachedPowerComponents_.clear();
    powerEvents_.clear();
    updateExploration();
}

void GameSession::restorePlayerProgress(PlayerId id,
                                        std::map<std::string, float> resources,
                                        std::vector<std::uint8_t> discovered,
                                        std::vector<LastKnownEntity> intelligence) {
    if (Player* player = players_.find(id)) {
        player->resources = std::move(resources);
        if (discovered.size() == player->discovered.size())
            player->discovered = std::move(discovered);
        player->intelligence = std::move(intelligence);
    }
}

} // namespace strategy
