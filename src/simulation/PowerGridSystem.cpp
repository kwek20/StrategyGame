#include "simulation/PowerGridSystem.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "players/PlayerRegistry.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace strategy {

void PowerGridSystem::reset() {
    snapshots_.clear();
    dirtyPlayers_.clear();
    observedEntityRevisions_.clear();
    observedReplacementRevision_ = 0;
    nextRevision_ = 1;
}

void PowerGridSystem::markDirty(PlayerId player) {
    if (player != 0) dirtyPlayers_.insert(player);
}

void PowerGridSystem::markAllDirty() {
    for (const auto& [player, snapshot] : snapshots_) {
        (void)snapshot;
        dirtyPlayers_.insert(player);
    }
}

const PowerGridSnapshot* PowerGridSystem::snapshot(PlayerId player) const {
    const auto found = snapshots_.find(player);
    return found == snapshots_.end() ? nullptr : &found->second;
}

PowerFailureReason PowerGridSystem::connect(World& world,
                                            PlayerId player,
                                            EntityId sourceId,
                                            EntityId targetId) {
    Entity* source = world.findEntity(sourceId);
    Entity* target = world.findEntity(targetId);
    if (!source || !target || source == target || !source->power || !target->power)
        return PowerFailureReason::invalidTarget;
    if (source->authority.owner != player || target->authority.owner != player)
        return PowerFailureReason::enemyTarget;
    if (!isOperational(*source) || !isOperational(*target))
        return PowerFailureReason::notOperational;
    if (!source->power.enabled || !target->power.enabled)
        return PowerFailureReason::disabled;

    auto& sourceConnections = source->power.connections;
    auto& targetConnections = target->power.connections;
    std::sort(sourceConnections.begin(), sourceConnections.end());
    std::sort(targetConnections.begin(), targetConnections.end());
    sourceConnections.erase(std::unique(sourceConnections.begin(), sourceConnections.end()),
                            sourceConnections.end());
    targetConnections.erase(std::unique(targetConnections.begin(), targetConnections.end()),
                            targetConnections.end());
    if (std::binary_search(sourceConnections.begin(), sourceConnections.end(), targetId) ||
        std::binary_search(targetConnections.begin(), targetConnections.end(), sourceId))
        return PowerFailureReason::alreadyConnected;
    if (source->power.maximumConnections == 0 || target->power.maximumConnections == 0 ||
        sourceConnections.size() >= source->power.maximumConnections ||
        targetConnections.size() >= target->power.maximumConnections)
        return PowerFailureReason::connectionLimit;

    const glm::vec2 sourcePosition{source->transform.position.x, source->transform.position.z};
    const glm::vec2 targetPosition{target->transform.position.x, target->transform.position.z};
    const float maximumRange =
        std::min(source->power.connectionRange, target->power.connectionRange);
    if (maximumRange <= 0.0F || glm::distance(sourcePosition, targetPosition) > maximumRange)
        return PowerFailureReason::outOfRange;

    sourceConnections.insert(
        std::lower_bound(sourceConnections.begin(), sourceConnections.end(), targetId), targetId);
    targetConnections.insert(
        std::lower_bound(targetConnections.begin(), targetConnections.end(), sourceId), sourceId);
    markDirty(player);
    return PowerFailureReason::none;
}

PowerFailureReason PowerGridSystem::disconnect(World& world,
                                               PlayerId player,
                                               EntityId sourceId,
                                               EntityId targetId) {
    Entity* source = world.findEntity(sourceId);
    Entity* target = world.findEntity(targetId);
    if (!source || !target || source == target || !source->power || !target->power)
        return PowerFailureReason::invalidTarget;
    if (source->authority.owner != player || target->authority.owner != player)
        return PowerFailureReason::enemyTarget;
    const bool sourceConnected =
        std::find(source->power.connections.begin(), source->power.connections.end(), targetId) !=
        source->power.connections.end();
    const bool targetConnected =
        std::find(target->power.connections.begin(), target->power.connections.end(), sourceId) !=
        target->power.connections.end();
    if (!sourceConnected || !targetConnected)
        return PowerFailureReason::notConnected;

    std::erase(source->power.connections, targetId);
    std::erase(target->power.connections, sourceId);
    markDirty(player);
    return PowerFailureReason::none;
}

void PowerGridSystem::rebuild(PlayerId player, World& world) {
    PowerGridSnapshot rebuilt;
    rebuilt.player = player;
    rebuilt.revision = nextRevision_++;

    for (Entity& candidate : world.entities()) {
        if (candidate.authority.owner != player || !candidate.power ||
            !candidate.power.enabled || !isOperational(candidate))
            continue;
        auto connections = candidate.power.connections;
        std::sort(connections.begin(), connections.end());
        connections.erase(std::unique(connections.begin(), connections.end()), connections.end());
        rebuilt.nodes.push_back({candidate.id, std::move(connections)});
    }
    std::sort(rebuilt.nodes.begin(), rebuilt.nodes.end(),
              [](const PowerGridNode& left, const PowerGridNode& right) {
                  return left.entity < right.entity;
              });

    const auto containsNode = [&](EntityId id) {
        const auto found = std::lower_bound(
            rebuilt.nodes.begin(), rebuilt.nodes.end(), id,
            [](const PowerGridNode& node, EntityId value) { return node.entity < value; });
        return found != rebuilt.nodes.end() && found->entity == id;
    };
    for (PowerGridNode& node : rebuilt.nodes)
        std::erase_if(node.connections, [&](EntityId adjacent) { return !containsNode(adjacent); });
    for (const PowerGridNode& node : rebuilt.nodes) {
        const Entity* source = world.findEntity(node.entity);
        for (EntityId adjacent : node.connections) {
            if (adjacent <= node.entity || !containsNode(adjacent)) continue;
            const Entity* target = world.findEntity(adjacent);
            if (!source || !target || !source->power || !target->power) continue;
            rebuilt.edges.push_back({node.entity, adjacent});
        }
    }

    std::vector<EntityId> visited;
    for (const PowerGridNode& root : rebuilt.nodes) {
        if (std::binary_search(visited.begin(), visited.end(), root.entity)) continue;
        PowerGridComponent component;
        std::vector<EntityId> pending{root.entity};
        while (!pending.empty()) {
            const EntityId currentId = pending.front();
            pending.erase(pending.begin());
            if (std::binary_search(visited.begin(), visited.end(), currentId)) continue;
            const auto current = std::lower_bound(
                rebuilt.nodes.begin(), rebuilt.nodes.end(), currentId,
                [](const PowerGridNode& node, EntityId value) { return node.entity < value; });
            if (current == rebuilt.nodes.end() || current->entity != currentId) continue;
            visited.insert(std::lower_bound(visited.begin(), visited.end(), currentId), currentId);
            component.nodes.push_back(currentId);
            for (EntityId adjacent : current->connections)
                if (containsNode(adjacent) &&
                    !std::binary_search(visited.begin(), visited.end(), adjacent) &&
                    !std::binary_search(pending.begin(), pending.end(), adjacent))
                    pending.insert(std::upper_bound(pending.begin(), pending.end(), adjacent),
                                   adjacent);
        }
        if (!component.nodes.empty()) {
            component.gridId = component.nodes.front();
            rebuilt.components.push_back(std::move(component));
        }
    }

    snapshots_[player] = std::move(rebuilt);
    dirtyPlayers_.erase(player);
}

void PowerGridSystem::simulate(World& world,
                               const PlayerRegistry& players,
                               const DefinitionRegistry& definitions,
                               std::uint64_t tick,
                               std::vector<PowerEvent>& events) {
    struct PreviousPowerState {
        EntityId entity{0};
        PowerOperationalState state{PowerOperationalState::offline};
        std::uint64_t gridId{0};
    };

    std::vector<PreviousPowerState> previousPowerStates;
    for (Entity& entity : world.entities()) {
        if (!entity.power) continue;
        previousPowerStates.push_back({entity.id, entity.power.state, entity.power.gridId});
        entity.power.supplied = 0.0F;
        entity.power.gridId = 0;
        entity.power.state = PowerOperationalState::offline;
        entity.transient.powerParent = 0;
        entity.transient.powerRoot = 0;
        entity.transient.powerPrimaryRoot = 0;
        entity.transient.powerBranchPriority = PowerPriority::medium;
        entity.transient.powerFallbackActive = false;
        entity.transient.powerBufferStartLastTick = 0.0F;
        entity.transient.powerGeneratedLastTick = 0.0F;
        entity.transient.powerDischargedLastTick = 0.0F;
        entity.transient.powerConsumedLastTick = 0.0F;
        entity.transient.powerChargedLastTick = 0.0F;
        entity.transient.powerCurtailedLastTick = 0.0F;
        entity.transient.powerSentLastTick = 0.0F;
        entity.transient.powerReceivedLastTick = 0.0F;
    }

    for (const Player& player : players.players()) {
        if (observedReplacementRevision_ != world.replacementRevision() ||
            observedEntityRevisions_[player.id] != world.entityRevision(player.id)) {
            markDirty(player.id);
            observedEntityRevisions_[player.id] = world.entityRevision(player.id);
        }
        if (!snapshots_.contains(player.id) || dirtyPlayers_.contains(player.id))
            rebuild(player.id, world);
        const PowerGridSnapshot& topology = snapshots_.at(player.id);

        for (const PowerGridComponent& topologyComponent : topology.components) {
            std::vector<Entity*> component;
            component.reserve(topologyComponent.nodes.size());
            for (EntityId id : topologyComponent.nodes)
                if (Entity* entity = world.findEntity(id)) component.push_back(entity);
            if (component.empty()) continue;
            const std::uint64_t gridId = topologyComponent.gridId;
            std::map<EntityId, Entity*> devices;
            std::map<EntityId, EntityId> parent;
            std::map<EntityId, EntityId> root;
            std::map<EntityId, std::uint32_t> depth;
            std::map<EntityId, PowerPriority> branchPriority;
            std::map<EntityId, float> incoming;
            for (Entity* device : component) {
                device->power.gridId = gridId;
                devices[device->id] = device;
                device->power.transitEnergy = std::clamp(
                    device->power.transitEnergy, 0.0F, device->power.transitCapacity);
                device->transient.powerBufferStartLastTick = device->power.transitEnergy;
            }

            const auto powerDefinition = [&](const Entity& device) {
                const EntityArchetype* type = definitions.archetype(device.archetype);
                return type && type->powerDevice ? definitions.powerDevice(*type->powerDevice)
                                                 : nullptr;
            };
            const auto canDischarge = [&](const Entity& device) {
                const PowerDeviceDefinition* definition = powerDefinition(device);
                return device.power.dischargeEnabled && device.power.storageCapacity > 0.0F &&
                       device.power.stored > 0.0F && definition &&
                       definition->storageDischargePerTick > 0.0F;
            };

            // Establish a deterministic outward forest. Generated power and eligible discharging
            // storage are roots; traversal prevents power from circulating back toward a source.
            std::set<EntityId> roots;
            for (const auto& [id, device] : devices)
                if (device->power.generation > 0.0F || canDischarge(*device)) {
                    parent[id] = 0;
                    root[id] = id;
                    depth[id] = 0;
                    roots.insert(id);
                }
            const auto buildForest = [&]() {
                parent.clear();
                root.clear();
                depth.clear();
                for (EntityId id : roots) {
                    parent[id] = 0;
                    root[id] = id;
                    depth[id] = 0;
                }
                // Relax until stable. Hop count wins first. Equal-hop candidates use direct
                // horizontal distance to their generator, then stable root and parent IDs.
                bool changed = true;
                while (changed) {
                    changed = false;
                    for (const PowerGridNode& current : topology.nodes) {
                        if (!devices.contains(current.entity) || !depth.contains(current.entity))
                            continue;
                        for (EntityId adjacent : current.connections) {
                            if (!devices.contains(adjacent) || roots.contains(adjacent)) continue;
                            const std::uint32_t candidateDepth = depth[current.entity] + 1;
                            const EntityId candidateRoot = root[current.entity];
                            const Entity* candidateGenerator = devices.at(candidateRoot);
                            const Entity* candidateDevice = devices.at(adjacent);
                            const float candidateX = candidateDevice->transform.position.x -
                                                     candidateGenerator->transform.position.x;
                            const float candidateZ = candidateDevice->transform.position.z -
                                                     candidateGenerator->transform.position.z;
                            const float candidateDistance = candidateX * candidateX + candidateZ * candidateZ;
                            bool better = !depth.contains(adjacent) || candidateDepth < depth[adjacent];
                            if (!better && depth.contains(adjacent) && candidateDepth == depth[adjacent]) {
                                const Entity* existingGenerator = devices.at(root[adjacent]);
                                const float existingX = candidateDevice->transform.position.x -
                                                        existingGenerator->transform.position.x;
                                const float existingZ = candidateDevice->transform.position.z -
                                                        existingGenerator->transform.position.z;
                                const float existingDistance = existingX * existingX + existingZ * existingZ;
                                better = candidateDistance < existingDistance ||
                                         (candidateDistance == existingDistance &&
                                          (candidateRoot < root[adjacent] ||
                                           (candidateRoot == root[adjacent] &&
                                            current.entity < parent[adjacent])));
                            }
                            if (!better) continue;
                            parent[adjacent] = current.entity;
                            root[adjacent] = candidateRoot;
                            depth[adjacent] = candidateDepth;
                            changed = true;
                        }
                    }
                }
            };
            buildForest();
            const std::map<EntityId, EntityId> primaryRoot = root;

            // A closer but deficient generator must not permanently strand consumers while a
            // connected fallback territory has genuine surplus. Reassign deficient territories
            // once, retaining stable hop/distance/ID ordering in the rebuilt forest.
            if (roots.size() > 1) {
                std::map<EntityId, float> availableByRoot;
                std::map<EntityId, float> demandByRoot;
                for (const auto& [id, device] : devices) {
                    if (!root.contains(id)) continue;
                    float available = device->power.transitEnergy;
                    float space = std::max(0.0F, device->power.transitCapacity - available);
                    const float generated = std::min(device->power.generation, space);
                    available += generated;
                    space -= generated;
                    if (canDischarge(*device)) {
                        const PowerDeviceDefinition* definition = powerDefinition(*device);
                        available += std::min(
                            {definition->storageDischargePerTick, device->power.stored, space});
                    }
                    availableByRoot[root[id]] += available;
                    demandByRoot[root[id]] += device->power.demand;
                }
                const bool hasSurplus = std::any_of(
                    roots.begin(), roots.end(), [&](EntityId id) {
                        return availableByRoot[id] > demandByRoot[id];
                    });
                if (hasSurplus) {
                    std::vector<EntityId> deficient;
                    for (EntityId id : roots)
                        if (availableByRoot[id] < demandByRoot[id]) deficient.push_back(id);
                    if (!deficient.empty() && deficient.size() < roots.size()) {
                        for (EntityId id : deficient) roots.erase(id);
                        buildForest();
                    }
                }
            }

            // Propagate the strongest downstream consumer priority toward each root. A relay's
            // branch therefore represents the consumers behind it instead of hiding their needs.
            for (const auto& [id, device] : devices) branchPriority[id] = device->power.priority;
            std::vector<EntityId> priorityOrder = topologyComponent.nodes;
            std::sort(priorityOrder.begin(), priorityOrder.end(), [&](EntityId left, EntityId right) {
                const std::uint32_t leftDepth = depth.contains(left) ? depth[left] : 0;
                const std::uint32_t rightDepth = depth.contains(right) ? depth[right] : 0;
                return leftDepth != rightDepth ? leftDepth > rightDepth : left > right;
            });
            for (EntityId id : priorityOrder) {
                if (!parent.contains(id) || parent[id] == 0) continue;
                EntityId parentId = parent[id];
                if (static_cast<unsigned>(branchPriority[id]) >
                    static_cast<unsigned>(branchPriority[parentId]))
                    branchPriority[parentId] = branchPriority[id];
            }

            // Production and storage discharge enter their local node before this tick's send.
            for (auto& [id, device] : devices) {
                if (root.contains(id)) device->transient.powerRoot = root[id];
                if (primaryRoot.contains(id))
                    device->transient.powerPrimaryRoot = primaryRoot.at(id);
                device->transient.powerFallbackActive =
                    root.contains(id) && primaryRoot.contains(id) && root[id] != primaryRoot.at(id);
                if (parent.contains(id)) device->transient.powerParent = parent[id];
                if (branchPriority.contains(id))
                    device->transient.powerBranchPriority = branchPriority[id];
            }
            for (auto& [id, device] : devices) {
                const float generationSpace = std::max(
                    0.0F, device->power.transitCapacity - device->power.transitEnergy);
                const float generated = std::min(device->power.generation, generationSpace);
                device->power.transitEnergy += generated;
                device->transient.powerGeneratedLastTick = generated;
                device->transient.powerCurtailedLastTick = device->power.generation - generated;
                if (canDischarge(*device)) {
                    const PowerDeviceDefinition* definition = powerDefinition(*device);
                    const float rate = definition->storageDischargePerTick;
                    const float discharged = std::min(
                        {rate, device->power.stored,
                         device->power.transitCapacity - device->power.transitEnergy});
                    device->power.stored -= discharged;
                    device->power.transitEnergy += discharged;
                    device->transient.powerDischargedLastTick = discharged;
                }
                const float consumed = std::min(device->power.demand,
                                                device->power.transitEnergy);
                device->power.transitEnergy -= consumed;
                device->power.supplied = consumed;
                device->transient.powerConsumedLastTick += consumed;
            }

            std::vector<EntityId> senders = topologyComponent.nodes;
            std::sort(senders.begin(), senders.end(), [&](EntityId left, EntityId right) {
                const auto leftDepth = depth.contains(left) ? depth.at(left)
                                                            : std::numeric_limits<std::uint32_t>::max();
                const auto rightDepth = depth.contains(right) ? depth.at(right)
                                                              : std::numeric_limits<std::uint32_t>::max();
                return leftDepth != rightDepth ? leftDepth < rightDepth : left < right;
            });
            for (EntityId sourceId : senders) {
                Entity* source = devices.at(sourceId);
                if (!depth.contains(sourceId) || source->power.transitEnergy <= 0.0F) continue;
                float outputRemaining = source->power.transferLimit > 0.0F
                                            ? source->power.transferLimit
                                            : source->power.transitEnergy;
                std::vector<EntityId> children;
                for (const auto& [candidate, candidateParent] : parent)
                    if (candidateParent == sourceId) children.push_back(candidate);
                std::sort(children.begin(), children.end(), [&](EntityId left, EntityId right) {
                    const PowerPriority leftPriority = branchPriority.at(left);
                    const PowerPriority rightPriority = branchPriority.at(right);
                    if (leftPriority != rightPriority)
                        return static_cast<unsigned>(leftPriority) >
                               static_cast<unsigned>(rightPriority);
                    const Entity* generator = devices.at(root.at(sourceId));
                    const glm::vec2 generatorPosition{generator->transform.position.x,
                                                      generator->transform.position.z};
                    const Entity* leftDevice = devices.at(left);
                    const Entity* rightDevice = devices.at(right);
                    const glm::vec2 leftOffset{
                        leftDevice->transform.position.x - generatorPosition.x,
                        leftDevice->transform.position.z - generatorPosition.y};
                    const glm::vec2 rightOffset{
                        rightDevice->transform.position.x - generatorPosition.x,
                        rightDevice->transform.position.z - generatorPosition.y};
                    const float leftDistance = glm::dot(leftOffset, leftOffset);
                    const float rightDistance = glm::dot(rightOffset, rightOffset);
                    return leftDistance != rightDistance ? leftDistance < rightDistance
                                                         : left < right;
                });
                for (EntityId targetId : children) {
                    if (outputRemaining <= 0.0F || source->power.transitEnergy <= 0.0F) break;
                    Entity* target = devices.at(targetId);
                    const auto edge = std::find_if(
                        topology.edges.begin(), topology.edges.end(), [&](const PowerGridEdge& item) {
                            const auto endpoints = std::minmax(sourceId, targetId);
                            return item.first == endpoints.first && item.second == endpoints.second;
                        });
                    if (edge == topology.edges.end()) continue;
                    const bool forwardsFurther = std::any_of(
                        parent.begin(), parent.end(),
                        [&](const auto& entry) { return entry.second == targetId; });
                    const float desiredLevel = forwardsFurther || target->power.demand <= 0.0F
                                                   ? target->power.transitCapacity
                                                   : target->power.demand;
                    const float targetSpace = std::max(
                        0.0F, desiredLevel - target->power.transitEnergy - incoming[targetId]);
                    const float amount = std::min(
                        {source->power.transitEnergy, outputRemaining, targetSpace});
                    if (amount <= 0.0F) continue;
                    source->power.transitEnergy -= amount;
                    outputRemaining -= amount;
                    incoming[targetId] += amount;
                    source->transient.powerSentLastTick += amount;
                    target->transient.powerReceivedLastTick += amount;
                }
            }

            // Incoming energy may satisfy the receiving device now, but cannot be forwarded until
            // the next tick because all sends above used only start-of-send buffers.
            for (auto& [id, device] : devices) {
                device->power.transitEnergy = std::min(
                    device->power.transitCapacity, device->power.transitEnergy + incoming[id]);
                const float unmet = std::max(0.0F, device->power.demand - device->power.supplied);
                const float consumed = std::min(unmet, device->power.transitEnergy);
                device->power.transitEnergy -= consumed;
                device->power.supplied += consumed;
                device->transient.powerConsumedLastTick += consumed;
                device->power.state = device->power.demand <= 0.0F ||
                                              device->power.supplied >= device->power.demand
                                          ? PowerOperationalState::powered
                                          : device->power.supplied > 0.0F
                                                ? PowerOperationalState::underpowered
                                                : PowerOperationalState::offline;
            }

            // Local surplus may charge storage only after consumer use and outward transfer.
            for (auto& [id, device] : devices) {
                if (device->power.storageCapacity <= 0.0F || device->power.transitEnergy <= 0.0F)
                    continue;
                const PowerDeviceDefinition* definition = powerDefinition(*device);
                const float rate = definition ? definition->storageChargePerTick : 0.0F;
                const float charged = std::min(
                    {rate, device->power.transitEnergy,
                     device->power.storageCapacity - device->power.stored});
                device->power.transitEnergy -= charged;
                device->power.stored += charged;
                device->transient.powerChargedLastTick = charged;
            }
        }
    }
    observedReplacementRevision_ = world.replacementRevision();

    for (const PreviousPowerState& previous : previousPowerStates) {
        const Entity* entity = world.findEntity(previous.entity);
        if (!entity || !entity->power || entity->power.demand <= 0.0F || tick == 0)
            continue;
        if (previous.state == entity->power.state) continue;
        PowerEventKind event = PowerEventKind::shortage;
        if (entity->power.state == PowerOperationalState::powered)
            event = PowerEventKind::recovered;
        else if (entity->power.state == PowerOperationalState::offline)
            event = PowerEventKind::shutdown;
        events.push_back({event, tick, entity->authority.owner, entity->id, 0,
                          entity->power.gridId});
    }
}

} // namespace strategy
