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

EntityId PowerGridSystem::connectNearestForPlacement(World& world,
                                                     PlayerId player,
                                                     EntityId sourceId) {
    Entity* source = world.findEntity(sourceId);
    if (!source || source->authority.owner != player || !source->power ||
        !source->power.enabled || source->power.connectionRange <= 0.0F ||
        source->power.maximumConnections == 0 ||
        source->power.connections.size() >= source->power.maximumConnections)
        return 0;
    const glm::vec2 sourcePosition{source->transform.position.x, source->transform.position.z};
    Entity* nearest = nullptr;
    float nearestDistanceSquared = std::numeric_limits<float>::max();
    for (Entity& candidate : world.entities()) {
        if (candidate.id == sourceId || candidate.authority.owner != player || !candidate.power ||
            !candidate.power.enabled || !isOperational(candidate) ||
            candidate.power.connectionRange <= 0.0F || candidate.power.maximumConnections == 0 ||
            candidate.power.connections.size() >= candidate.power.maximumConnections)
            continue;
        const glm::vec2 candidatePosition{candidate.transform.position.x,
                                          candidate.transform.position.z};
        const glm::vec2 delta = sourcePosition - candidatePosition;
        const float distanceSquared = glm::dot(delta, delta);
        const float range = std::min(source->power.connectionRange,
                                     candidate.power.connectionRange);
        if (distanceSquared > range * range ||
            (nearest && distanceSquared == nearestDistanceSquared && candidate.id > nearest->id))
            continue;
        nearest = &candidate;
        nearestDistanceSquared = distanceSquared;
    }
    if (!nearest) return 0;
    source->power.connections.insert(
        std::lower_bound(source->power.connections.begin(), source->power.connections.end(), nearest->id),
        nearest->id);
    nearest->power.connections.insert(
        std::lower_bound(nearest->power.connections.begin(), nearest->power.connections.end(), sourceId),
        sourceId);
    markDirty(player);
    return nearest->id;
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
        entity.transient.powerBranchPriority = entity.power.priority;
        entity.transient.powerFallbackActive = false;
        entity.transient.powerIncomingLastTick.clear();
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
            // Only explicit storage survives a tick. These arrays are allocation budgets,
            // never transit energy, and are discarded after this component is evaluated.
            constexpr double epsilon = 0.000001;
            const std::size_t count = topologyComponent.nodes.size();
            const std::size_t none = count;
            std::vector<Entity*> devices;
            std::map<EntityId, std::size_t> index;
            for (EntityId id : topologyComponent.nodes) {
                index[id] = devices.size();
                devices.push_back(world.findEntity(id));
            }
            std::vector<std::vector<std::size_t>> adjacent(count);
            std::vector<double> generated(count), discharge(count), outputLeft(count), demandLeft(count), chargeLeft(count);
            std::vector<bool> generator(count), reachable(count, false);
            for (std::size_t i = 0; i < count; ++i) {
                Entity& device = *devices[i];
                device.power.gridId = topologyComponent.gridId;
                const auto* type = definitions.archetype(device.archetype);
                const auto* definition = type && type->powerDevice
                    ? definitions.powerDevice(*type->powerDevice) : nullptr;
                generator[i] = device.power.generation > 0.0F ||
                    (definition && (definition->tags.contains("producer") ||
                                    definition->tags.contains("generator")));
                generated[i] = std::max(0.0F, device.power.generation);
                demandLeft[i] = device.power.consumptionEnabled ? std::max(0.0F, device.power.demand) : 0.0;
                outputLeft[i] = device.power.outputEnabled ? std::max(0.0F, device.power.transferLimit) : 0.0;
                device.power.stored = std::clamp(device.power.stored, 0.0F, device.power.storageCapacity);
                discharge[i] = definition && device.power.dischargeEnabled
                    ? std::min(device.power.stored, definition->storageDischargePerTick) : 0.0;
                chargeLeft[i] = definition
                    ? std::min(definition->storageChargePerTick, device.power.storageCapacity - device.power.stored) : 0.0;
                for (EntityId id : device.power.connections)
                    if (const auto found = index.find(id); found != index.end())
                        adjacent[i].push_back(found->second);
                std::sort(adjacent[i].begin(), adjacent[i].end());
                adjacent[i].erase(std::unique(adjacent[i].begin(), adjacent[i].end()), adjacent[i].end());
            }

            struct Forest {
                std::vector<std::size_t> parent, root, depth, order;
                std::vector<std::vector<std::size_t>> children;
            };
            const auto forestFor = [&](const std::vector<double>& budget) {
                Forest forest{std::vector<std::size_t>(count, none), std::vector<std::size_t>(count, none),
                              std::vector<std::size_t>(count, none), {}, std::vector<std::vector<std::size_t>>(count)};
                std::vector<double> cost(count, 0.0);
                std::vector<std::vector<EntityId>> paths(count);
                for (std::size_t i = 0; i < count; ++i)
                    if (budget[i] > epsilon) {
                        forest.root[i] = i;
                        forest.depth[i] = 0;
                        paths[i] = {devices[i]->id};
                    }
                bool changed = true;
                while (changed) {
                    changed = false;
                    for (std::size_t from = 0; from < count; ++from) {
                        if (forest.root[from] == none || outputLeft[from] <= epsilon) continue;
                        for (std::size_t to : adjacent[from]) {
                            // Generators never accept incoming power, even after exhausting output.
                            if (generator[to] || budget[to] > epsilon) continue;
                            const auto depth = forest.depth[from] + 1;
                            if (forest.root[to] != none && depth > forest.depth[to]) continue;
                            const auto root = forest.root[from];
                            const auto squared = [&](std::size_t left, std::size_t right) {
                                const double x = static_cast<double>(devices[left]->transform.position.x) -
                                                 devices[right]->transform.position.x;
                                const double z = static_cast<double>(devices[left]->transform.position.z) -
                                                 devices[right]->transform.position.z;
                                return x * x + z * z;
                            };
                            const double candidateCost = cost[from] + squared(from, to);
                            auto path = paths[from];
                            path.push_back(devices[to]->id);
                            bool better = forest.root[to] == none || depth < forest.depth[to];
                            if (!better && depth == forest.depth[to]) {
                                const double distance = squared(to, root);
                                const double previousDistance = squared(to, forest.root[to]);
                                better = distance < previousDistance ||
                                    (distance == previousDistance &&
                                     (devices[root]->id < devices[forest.root[to]]->id ||
                                      (root == forest.root[to] &&
                                       (candidateCost < cost[to] || (candidateCost == cost[to] && path < paths[to])))));
                            }
                            if (!better) continue;
                            forest.parent[to] = from;
                            forest.root[to] = root;
                            forest.depth[to] = depth;
                            cost[to] = candidateCost;
                            paths[to] = std::move(path);
                            changed = true;
                        }
                    }
                }
                for (std::size_t i = 0; i < count; ++i) {
                    if (forest.root[i] == none) continue;
                    forest.order.push_back(i);
                    if (forest.parent[i] != none)
                        forest.children[forest.parent[i]].push_back(i);
                }
                std::sort(forest.order.begin(), forest.order.end(), [&](auto left, auto right) {
                    return forest.depth[left] != forest.depth[right] ? forest.depth[left] < forest.depth[right]
                                                                     : devices[left]->id < devices[right]->id;
                });
                return forest;
            };

            // Establish source reachability before budgets are spent, including passive relays.
            std::vector<double> available(count);
            for (std::size_t i = 0; i < count; ++i) available[i] = generated[i] + discharge[i];
            const Forest initial = forestFor(available);
            for (auto i : initial.order) {
                reachable[i] = true;
                devices[i]->transient.powerPrimaryRoot = devices[initial.root[i]]->id;
                devices[i]->transient.powerRoot = devices[initial.root[i]]->id;
                if (initial.parent[i] != none)
                    devices[i]->transient.powerParent = devices[initial.parent[i]]->id;
            }

            const auto allocate = [&](std::vector<double>& budget, int priority, bool charging, bool storedSource) {
                // Rebuild after exhausting a source or output bottleneck so another reachable
                // generator can supply the remainder without receiving power through a generator.
                while (true) {
                    const Forest forest = forestFor(budget);
                    std::vector<double> need(count, 0.0), localNeed(count, 0.0), incoming(count, 0.0);
                    for (auto it = forest.order.rbegin(); it != forest.order.rend(); ++it) {
                        const auto i = *it;
                        double childrenNeed = 0.0;
                        for (auto child : forest.children[i]) childrenNeed += need[child];
                        localNeed[i] = charging ? chargeLeft[i]
                            : (static_cast<int>(devices[i]->power.priority) >= priority || childrenNeed > epsilon)
                                  ? demandLeft[i] : 0.0;
                        // An upstream consumer must be supplied before any power passes through it.
                        need[i] = localNeed[i] + std::min(outputLeft[i], childrenNeed);
                    }
                    double allocated = 0.0;
                    for (auto i : forest.order) {
                        Entity& device = *devices[i];
                        if (forest.root[i] == i) {
                            incoming[i] = std::min(budget[i], need[i]);
                            budget[i] -= incoming[i];
                            allocated += incoming[i];
                            if (storedSource) {
                                device.power.stored = std::max(0.0F, device.power.stored - static_cast<float>(incoming[i]));
                                device.transient.powerDischargedLastTick += static_cast<float>(incoming[i]);
                            } else {
                                device.transient.powerGeneratedLastTick += static_cast<float>(incoming[i]);
                            }
                        }
                        const double local = std::min(incoming[i], localNeed[i]);
                        if (charging) {
                            chargeLeft[i] -= local;
                            device.power.stored += static_cast<float>(local);
                            device.transient.powerChargedLastTick += static_cast<float>(local);
                        } else {
                            demandLeft[i] -= local;
                            device.power.supplied += static_cast<float>(local);
                            device.transient.powerConsumedLastTick += static_cast<float>(local);
                        }
                        if (incoming[i] > epsilon) {
                            const auto rootId = devices[forest.root[i]]->id;
                            device.transient.powerRoot = rootId;
                            device.transient.powerParent = forest.parent[i] == none ? 0 : devices[forest.parent[i]]->id;
                            device.transient.powerFallbackActive |= rootId != device.transient.powerPrimaryRoot;
                            if (!charging)
                                device.transient.powerBranchPriority = static_cast<PowerPriority>(std::max(
                                    priority, static_cast<int>(device.transient.powerBranchPriority)));
                        }
                        double remaining = std::min(outputLeft[i], incoming[i] - local);
                        // Equal shares among requesting connections in this priority tier.
                        // Reassign unused shares rather than wasting output on satisfied branches.
                        std::vector<double> sent(forest.children[i].size(), 0.0);
                        while (remaining > epsilon) {
                            std::size_t requesting = 0;
                            for (std::size_t c = 0; c < sent.size(); ++c)
                                if (need[forest.children[i][c]] - sent[c] > epsilon) ++requesting;
                            if (requesting == 0) break;
                            const double share = remaining / static_cast<double>(requesting);
                            double moved = 0.0;
                            for (std::size_t c = 0; c < sent.size(); ++c) {
                                const double amount = std::min(share, std::max(0.0, need[forest.children[i][c]] - sent[c]));
                                sent[c] += amount;
                                moved += amount;
                            }
                            remaining -= moved;
                            if (moved <= epsilon) break;
                        }
                        for (std::size_t c = 0; c < sent.size(); ++c) {
                            const auto child = forest.children[i][c];
                            const double amount = sent[c];
                            incoming[child] += amount;
                            outputLeft[i] = std::max(0.0, outputLeft[i] - amount);
                            device.transient.powerSentLastTick += static_cast<float>(amount);
                            devices[child]->transient.powerReceivedLastTick += static_cast<float>(amount);
                            if (amount > epsilon)
                                devices[child]->transient.powerIncomingLastTick.emplace_back(device.id, static_cast<float>(amount));
                        }
                    }
                    if (allocated <= epsilon) break;
                }
            };

            for (int priority = static_cast<int>(PowerPriority::high);
                 priority >= static_cast<int>(PowerPriority::low); --priority) {
                allocate(generated, priority, false, false);
                allocate(discharge, priority, false, true);
            }
            // No storage charging while any enabled consumer in this grid lacks power.
            if (std::all_of(demandLeft.begin(), demandLeft.end(), [&](double demand) { return demand <= epsilon; })) {
                for (std::size_t i = 0; i < count; ++i) {
                    const auto* type = definitions.archetype(devices[i]->archetype);
                    const auto* definition = type && type->powerDevice ? definitions.powerDevice(*type->powerDevice) : nullptr;
                    chargeLeft[i] = definition ? std::min<double>(definition->storageChargePerTick,
                        devices[i]->power.storageCapacity - devices[i]->power.stored) : 0.0;
                    // Never cycle discharged storage back into storage in the same tick.
                    if (devices[i]->transient.powerDischargedLastTick > 0.0F) chargeLeft[i] = 0.0;
                }
                allocate(generated, 0, true, false);
            }
            for (std::size_t i = 0; i < count; ++i) {
                auto& device = *devices[i];
                // Repeated source contributions can round slightly above the float component limits.
                device.power.supplied = std::clamp(device.power.supplied, 0.0F, device.power.demand);
                device.power.stored = std::clamp(device.power.stored, 0.0F, device.power.storageCapacity);
                device.transient.powerCurtailedLastTick = static_cast<float>(generated[i]);
                if (!device.power.consumptionEnabled)
                    device.power.state = PowerOperationalState::offline;
                else if (device.power.demand > 0.0F)
                    device.power.state = demandLeft[i] <= epsilon ? PowerOperationalState::powered
                        : device.power.supplied > 0.0F ? PowerOperationalState::underpowered : PowerOperationalState::offline;
                else
                    device.power.state = reachable[i] ? PowerOperationalState::powered : PowerOperationalState::offline;
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
