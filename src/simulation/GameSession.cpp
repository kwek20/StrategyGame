#include "simulation/GameSession.hpp"
#include "simulation/StateChecksum.hpp"

#include "localization/Text.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/StartingPlacement.hpp"
#include "world/Navigation.hpp"
#include "world/MapArea.hpp"
#include "world/WorldGeneration.hpp"
#include "world/GenerationProgress.hpp"

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

std::uint32_t retryTerrainSeed(std::uint32_t requestedSeed, std::uint32_t attempt) {
    if (attempt == 0) return requestedSeed;
    std::uint32_t value = requestedSeed ^ (0x9E3779B9U * attempt);
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
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
    const bool targetChanged = actor.transient.navigationGoalEntity != target.id ||
        std::abs(actor.transient.navigationInteractionRange - range) > 0.001F;
    actor.transient.navigationGoalEntity = target.id;
    actor.transient.navigationInteractionRange = range;
    // Static interaction targets must retain one stable approach point. Recomputing the nearest
    // boundary point as the actor moves makes the destination marker wander and repeatedly throws
    // away a valid path. A moving target is rerouted after its current path is exhausted.
    if (targetChanged || !actor.unitControl.hasStrategicDestination) {
        actor.unitControl.strategicDestination = {destination.x, target.transform.position.y,
                                                  destination.y};
        actor.unitControl.hasStrategicDestination = true;
        actor.transient.navigationPath.clear();
        actor.transient.navigationWaypoint = 0;
    }
}

void clearGatherLoop(Entity& entity) {
    if (!entity.gatherer) return;
    entity.gatherer.sourceTarget = 0;
    entity.gatherer.deliveryTarget = 0;
    entity.gatherer.preferredProcessor = 0;
    entity.gatherer.preferredOutput.clear();
    entity.gatherer.repeatGathering = false;
    entity.gatherer.waitingForProcessor = false;
}
} // namespace

std::uint64_t GameSession::stateChecksum() const {
    return authoritativeStateChecksum(world_, players_, terrainSeed_, tick_, mapChunksPerSide_,
                                      terrainLayout_.value);
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
    entity.battery.recoveryReason = BatteryRecoveryReason::none;
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
    entity.battery.recoveryReason = BatteryRecoveryReason::none;
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
    clearGatherLoop(entity);
    if (entity.battery) {
        entity.battery.returningToCharge = false;
        entity.battery.hasSuspendedOrder = false;
        entity.battery.suspendedOrder = UnitOrderKind::idle;
        entity.battery.suspendedTarget = 0;
        entity.battery.suspendedHasDestination = false;
        entity.battery.chargerTarget = 0;
        entity.battery.recoveryReason = BatteryRecoveryReason::none;
    }
}

Entity* GameSession::nextResourceNode(const Entity& depletedNode) {
    if (!depletedNode.resource) return nullptr;
    const ResourceFieldDefinition* fieldDefinition = gameplay_.resourceFieldForNode(
        ResourceArchetypeId{depletedNode.archetype.value});
    const glm::vec2 depletedPosition{depletedNode.transform.position.x,
                                     depletedNode.transform.position.z};
    std::vector<const GeneratedResourceField*> containingFields;
    if (fieldDefinition) {
        for (const GeneratedResourceField& field : resourceLayout_.fields) {
            if (field.definition.value != fieldDefinition->id) continue;
            const glm::vec2 delta = depletedPosition - field.center;
            if (glm::dot(delta, delta) <= field.radius * field.radius + 0.01F)
                containingFields.push_back(&field);
        }
    }
    const float fallbackRadius = fieldDefinition
        ? fieldDefinition->generation.maximumFieldRadius * 2.0F
        : 0.0F;
    Entity* nearest = nullptr;
    float nearestDistanceSquared = std::numeric_limits<float>::max();
    for (Entity& candidate : world_.entities()) {
        if (candidate.id == depletedNode.id || !candidate.resource ||
            candidate.resource.remaining <= 0.0F ||
            candidate.resource.type != depletedNode.resource.type)
            continue;
        const glm::vec2 position{candidate.transform.position.x, candidate.transform.position.z};
        bool sameField = false;
        for (const GeneratedResourceField* field : containingFields) {
            const glm::vec2 fromCenter = position - field->center;
            if (glm::dot(fromCenter, fromCenter) <= field->radius * field->radius + 0.01F) {
                sameField = true;
                break;
            }
        }
        const glm::vec2 delta = position - depletedPosition;
        const float distanceSquared = glm::dot(delta, delta);
        if (containingFields.empty())
            sameField = fallbackRadius > 0.0F &&
                        distanceSquared <= fallbackRadius * fallbackRadius;
        if (!sameField) continue;
        if (!nearest || distanceSquared < nearestDistanceSquared - 0.001F ||
            (std::abs(distanceSquared - nearestDistanceSquared) <= 0.001F &&
             candidate.id < nearest->id)) {
            nearest = &candidate;
            nearestDistanceSquared = distanceSquared;
        }
    }
    return nearest;
}

GameSession::GameSession(const DefinitionRegistry& definitions,
                         std::uint32_t terrainSeed,
                         std::string playerOneCountry,
                         std::string playerTwoCountry,
                         std::string playerOneSpecialization,
                         std::string playerTwoSpecialization,
                         std::uint32_t mapChunksPerSide,
                         float startingResourcesScale,
                         float resourceAbundanceScale,
                         TerrainLayoutId terrainLayout,
                         WorldGenerationProgress* generationProgress)
    : players_(std::move(playerOneCountry),
               std::move(playerTwoCountry),
               std::move(playerOneSpecialization),
               std::move(playerTwoSpecialization))
    , gameplay_(definitions)
    , terrainSeed_(terrainSeed)
    , terrainLayout_(std::move(terrainLayout))
    , terrain_(terrainSeed, terrainLayout_, generationProgress)
    , mapChunksPerSide_(std::clamp(mapChunksPerSide, 10U,
                                  static_cast<std::uint32_t>(Terrain::chunksPerSide)))
    , navigation_(terrain_, gameplay_, mapChunksPerSide_, generationProgress)
    , resourceAbundanceScale_(std::clamp(resourceAbundanceScale, 0.5F, 2.0F)) {
    for (Player& player : players_.players())
        for (const auto& [resource, amount] : gameplay_.matchRules().startingResources)
            player.resources[resource] =
                amount * std::clamp(startingResourcesScale, 0.0F, 4.0F);
    std::vector<StartingRegion> startingRegions;
    if (generationProgress) generationProgress->report(WorldGenerationPhase::starts, 0.0F);
    const std::uint32_t requestedSeed = terrainSeed_;
    const std::uint32_t maximumAttempts = terrain_.maximumGenerationAttempts();
    for (std::uint32_t attempt = 0; attempt < maximumAttempts; ++attempt) {
        terrainSeed_ = retryTerrainSeed(requestedSeed, attempt);
        if (attempt > 0) {
            terrain_ = Terrain{terrainSeed_, terrainLayout_, generationProgress};
            navigation_.rebuildTerrain(terrain_, mapChunksPerSide_, generationProgress);
        }
        try {
            if (generationProgress)
                generationProgress->report(WorldGenerationPhase::starts,
                    static_cast<float>(attempt) / std::max(1U, maximumAttempts));
            startingRegions = selectStartingRegions(
                terrain_, gameplay_, mapChunksPerSide_, players_.players().size(),
                terrainSeed_);
            break;
        } catch (const std::runtime_error&) {
            if (attempt + 1 == maximumAttempts)
                throw std::runtime_error(
                    "Unable to generate a connected, playable terrain after " +
                    std::to_string(maximumAttempts) + " deterministic attempts");
        }
    }
    if (generationProgress) generationProgress->report(WorldGenerationPhase::starts, 1.0F);
    for (const StartingRegion& region : startingRegions)
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
    resourceLayout_ = populateResources(world_, terrain_, gameplay_, terrainSeed,
                                        mapChunksPerSide_, resourceAbundanceScale_,
                                        startingAnchors_, generationProgress);
    if (generationProgress) generationProgress->report(WorldGenerationPhase::validation, 0.0F);
    // Start selection validates usable land and reachability; resource generation validates each
    // guaranteed opening deposit before this phase is reported complete.
    if (startingAnchors_.size() != players_.players().size())
        throw std::runtime_error("Terrain validation did not produce one start per player");
    if (generationProgress) generationProgress->report(WorldGenerationPhase::validation, 1.0F);
    vegetation_ = generateVegetation(world_, terrain_, gameplay_, terrainSeed_, mapChunksPerSide_,
                                     generationProgress);
    updateExploration();
    if (generationProgress) generationProgress->report(WorldGenerationPhase::complete, 1.0F);
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
                clearGatherLoop(*entity);
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
                const PowerFailureReason failure = powerGridSystem_.connect(
                    world_, command.player, entity->id, payload.target);
                if (failure != PowerFailureReason::none) {
                    powerEvents_.push_back({PowerEventKind::commandRejected, tick_, command.player,
                                            entity->id, payload.target, 0, failure});
                    return;
                }
                powerEvents_.push_back({PowerEventKind::connectionCreated, tick_, command.player,
                                        entity->id, payload.target, 0});
            } else if constexpr (std::is_same_v<Type, DisconnectPowerCommand>) {
                const PowerFailureReason failure = powerGridSystem_.disconnect(
                    world_, command.player, entity->id, payload.target);
                if (failure != PowerFailureReason::none) {
                    powerEvents_.push_back({PowerEventKind::commandRejected, tick_, command.player,
                                            entity->id, payload.target, 0, failure});
                    return;
                }
                powerEvents_.push_back({PowerEventKind::connectionRemoved, tick_, command.player,
                                        entity->id, payload.target, 0});
            } else if constexpr (std::is_same_v<Type, SetPowerPriorityCommand>) {
                if (entity->power)
                    entity->power.priority = payload.priority;
            } else if constexpr (std::is_same_v<Type, SetPowerEnabledCommand>) {
                if (entity->power && entity->power.enabled != payload.enabled) {
                    entity->power.enabled = payload.enabled;
                    powerGridSystem_.markDirty(command.player);
                }
            } else if constexpr (std::is_same_v<Type, SetPowerDischargeEnabledCommand>) {
                if (entity->power && entity->power.generation > 0.0F &&
                    entity->power.storageCapacity > 0.0F)
                    entity->power.dischargeEnabled = payload.enabled;
            } else if constexpr (std::is_same_v<Type, AttackEntityCommand>) {
                Entity* target = world_.findEntity(payload.target);
                if (entity->unitControl && entity->combat &&
                    entity->unitControl.directlyControllable && target && target->health &&
                    target->authority.owner != 0 && target->authority.owner != command.player) {
                    clearGatherLoop(*entity);
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
                const float playableExtent =
                    map.halfExtent() - gameplay_.matchRules().terrainEdgeMargin;
                if (std::abs(placementShape.center.x) + bounds.x > playableExtent ||
                    std::abs(placementShape.center.y) + bounds.y > playableExtent) return;
                if (overlapsObject(world_, gameplay_, placementShape)) return;
                const TerrainPlacementResult placement = terrain_.evaluatePlacement(
                    payload.position.x, payload.position.z, shape,
                    buildingDefinition ? buildingDefinition->placement
                                       : TerrainPlacementProfile{});
                if (!placement.valid()) return;
                for (const auto& [resource, amount] : recipe->cost)
                    if (player->resources[resource] < amount) return;
                for (const auto& [resource, amount] : recipe->cost) player->resources[resource] -= amount;
                vegetation_.clearWithin(placementShape);
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
                    clearGatherLoop(*builder);
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
                    clearGatherLoop(*entity);
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
                if (entity->power)
                    powerGridSystem_.markDirty(entity->authority.owner);
                world_.destroyEntity(cancelled);
            } else if constexpr (std::is_same_v<Type, RepairCommand>) {
                Entity* target = world_.findEntity(payload.target);
                if (entity->battery && entity->battery.charge <= 0.0F) {
                    beginRecharge(*entity);
                    return;
                }
                if (entity->flight && target && target->health && entity->battery &&
                    !entity->battery.returningToCharge) { clearGatherLoop(*entity); entity->unitControl.order=UnitOrderKind::repair; entity->unitControl.orderTarget=target->id; entity->unitControl.strategicDestination=target->transform.position; entity->unitControl.hasStrategicDestination=true; }
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
    bool foundationInfluenceChanged = false;
    powerGridSystem_.simulate(world_, players_, gameplay_, tick_, powerEvents_);
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
        // Battery exhaustion is an authoritative recovery trigger even while the drone is idle.
        // Commands and movement also call beginRecharge immediately, but this guard closes every
        // remaining path that can consume the last unit of charge.
        if (entity.flight && entity.battery && entity.battery.charge <= 0.0F &&
            !entity.battery.returningToCharge)
            beginRecharge(entity);
        if (entity.flight && entity.battery && entity.battery.returningToCharge) {
            const Entity* chargerEntity = world_.findEntity(entity.battery.chargerTarget);
            const auto chargerDevice = [&](const Entity* candidate)
                -> const PowerDeviceDefinition* {
                if (!candidate || candidate->authority.owner != entity.authority.owner ||
                    !isOperational(*candidate)) return nullptr;
                const EntityArchetype* type = gameplay_.archetype(candidate->archetype);
                if (!type || !type->powerDevice) return nullptr;
                const PowerDeviceDefinition* device = gameplay_.powerDevice(*type->powerDevice);
                return device && device->tags.contains("charger") ? device : nullptr;
            };
            const auto poweredCharger = [&](const Entity* candidate) {
                const PowerDeviceDefinition* device = chargerDevice(candidate);
                return device && device->chargePerTick > 0.0F && candidate->power &&
                       candidate->power.enabled &&
                       candidate->power.state != PowerOperationalState::offline;
            };
            const auto chargerHasSlot = [&](const Entity& candidate,
                                            const PowerDeviceDefinition& device) {
                std::uint32_t reserved = 0;
                for (const Entity& other : world_.entities())
                    if (other.id != entity.id && other.battery &&
                        other.battery.returningToCharge &&
                        other.battery.chargerTarget == candidate.id)
                        ++reserved;
                return reserved < device.chargingSlots;
            };
            const auto reachableCharger = [&](const Entity& candidate) {
                const float range = interactionRange(gameplay_, entity, "charge");
                const MapArea map{mapChunksPerSide_};
                if (!map.contains({candidate.transform.position.x,
                                   candidate.transform.position.z},
                                  gameplay_.matchRules().terrainEdgeMargin))
                    return false;
                if (boundaryClearance(gameplay_, entity, candidate) <= range) return true;
                return !navigation_.findPath(
                    world_, entity.transform.position,
                    NavigationGoalRegion{spatialShape(gameplay_, candidate), range,
                                         {entity.transform.position.x,
                                          entity.transform.position.z},
                                         candidate.id, entity.id},
                    collisionRadius(gameplay_, entity.archetype), entity.id,
                    navigationProfile(gameplay_, entity)).empty();
            };
            const PowerDeviceDefinition* currentDevice = chargerDevice(chargerEntity);
            if (!poweredCharger(chargerEntity) || !currentDevice ||
                !chargerHasSlot(*chargerEntity, *currentDevice)) {
                chargerEntity = nullptr;
                float nearestDistance = std::numeric_limits<float>::max();
                bool foundPowered = false;
                bool foundAvailable = false;
                for (const Entity& candidate : world_.entities()) {
                    if (!poweredCharger(&candidate)) continue;
                    foundPowered = true;
                    const PowerDeviceDefinition* device = chargerDevice(&candidate);
                    if (!device || !chargerHasSlot(candidate, *device)) continue;
                    foundAvailable = true;
                    if (!reachableCharger(candidate)) continue;
                    const float distance = boundaryClearance(gameplay_, entity, candidate);
                    if (distance < nearestDistance ||
                        (distance == nearestDistance && chargerEntity && candidate.id < chargerEntity->id)) {
                        nearestDistance = distance;
                        chargerEntity = &candidate;
                    }
                }
                entity.battery.chargerTarget = chargerEntity ? chargerEntity->id : 0;
                if (!chargerEntity)
                    entity.battery.recoveryReason =
                        !foundPowered ? BatteryRecoveryReason::noCharger
                        : !foundAvailable ? BatteryRecoveryReason::chargersOccupied
                                          : BatteryRecoveryReason::noReachableCharger;
            }
            if (!chargerEntity) {
                entity.unitControl.order = UnitOrderKind::stranded;
                entity.unitControl.orderTarget = 0;
                entity.unitControl.hasStrategicDestination = false;
            } else {
                entity.battery.recoveryReason = BatteryRecoveryReason::none;
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
                                foundationInfluenceChanged = true;
                            }
                            if (target->construction.powerProgress >= target->construction.powerRequired) {
                                target->construction.state = BuildingLifecycleState::operational;
                                if (target->power)
                                    powerGridSystem_.markDirty(target->authority.owner);
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
                    Entity* nextNode = node && node->resource && entity.gatherer &&
                                               entity.gatherer.carriedAmount <
                                                   stat(entity, GameplayStat::carryCapacity)
                                           ? nextResourceNode(*node)
                                           : nullptr;
                    if (nextNode) {
                        entity.gatherer.sourceTarget = nextNode->id;
                        entity.unitControl.order = UnitOrderKind::gather;
                        entity.unitControl.orderTarget = nextNode->id;
                        entity.unitControl.hasStrategicDestination = false;
                        entity.transient.navigationPath.clear();
                        entity.transient.navigationWaypoint = 0;
                        entity.transient.navigationGoalEntity = 0;
                    } else if (!node || !node->resource) {
                        entity.gatherer.sourceTarget = 0;
                    }
                    if (!nextNode)
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
                            Entity* nextNode = node->resource.remaining <= 0.0F &&
                                                       entity.gatherer.carriedAmount < capacity
                                                   ? nextResourceNode(*node)
                                                   : nullptr;
                            if (nextNode) {
                                entity.gatherer.sourceTarget = nextNode->id;
                                entity.unitControl.orderTarget = nextNode->id;
                                entity.unitControl.hasStrategicDestination = false;
                                entity.transient.navigationPath.clear();
                                entity.transient.navigationWaypoint = 0;
                                entity.transient.navigationGoalEntity = 0;
                            } else {
                                entity.unitControl.order = UnitOrderKind::returnResources;
                            }
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
        const float edgeMargin = gameplay_.matchRules().terrainEdgeMargin;
        const auto validPosition = [this, &entity, radius, map, edgeMargin, flying,
                                    movementProfile](glm::vec2 candidate) {
            return map.contains(candidate, radius + edgeMargin) &&
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
    // Multiple drones and projects may update foundations in one simulation tick. Rebuild the
    // affected terrain regions once after all authoritative work contributions are applied.
    if (foundationInfluenceChanged)
        terrain_.rebuildFoundations(world_.foundations());
    std::sort(destroyed.begin(), destroyed.end());
    destroyed.erase(std::unique(destroyed.begin(), destroyed.end()), destroyed.end());
    for (EntityId id : destroyed) {
        if (Entity* entity = world_.findEntity(id); entity) {
            if (entity->construction)
                entity->construction.state = BuildingLifecycleState::destroyed;
            if (entity->power) {
                powerGridSystem_.markDirty(entity->authority.owner);
                for (EntityId connected : entity->power.connections)
                    powerEvents_.push_back({PowerEventKind::connectionRemoved, tick_,
                                            entity->authority.owner, id, connected, 0});
            }
        }
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
                               std::uint32_t mapChunksPerSide,
                               TerrainLayoutId terrainLayout) {
    world_.replaceEntities(std::move(entities));
    world_.replaceFoundations(std::move(foundations));
    terrainSeed_ = terrainSeed;
    terrainLayout_ = std::move(terrainLayout);
    mapChunksPerSide_ = std::clamp(mapChunksPerSide, 10U,
                                   static_cast<std::uint32_t>(Terrain::chunksPerSide));
    terrain_ = Terrain{terrainSeed, terrainLayout_};
    terrain_.rebuildFoundations(world_.foundations());
    resourceLayout_ = {};
    navigation_.rebuildTerrain(terrain_, mapChunksPerSide_);
    vegetation_ = generateVegetation(world_, terrain_, gameplay_, terrainSeed_, mapChunksPerSide_);
    commands_.clear();
    lastSequence_.clear();
    powerGridSystem_.reset();
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
