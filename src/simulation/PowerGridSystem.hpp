#pragma once

#include "simulation/PowerEvent.hpp"
#include "world/Entity.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace strategy {

class DefinitionRegistry;
class PlayerRegistry;
class World;

struct PowerGridNode {
    EntityId entity{0};
    std::vector<EntityId> connections;
};

struct PowerGridEdge {
    EntityId first{0};
    EntityId second{0};
};

struct PowerGridComponent {
    std::uint64_t gridId{0};
    std::vector<EntityId> nodes;
};

// Immutable for the duration of an allocation tick. All collections are sorted by stable IDs.
struct PowerGridSnapshot {
    PlayerId player{0};
    std::uint64_t revision{0};
    std::vector<PowerGridNode> nodes;
    std::vector<PowerGridEdge> edges;
    std::vector<PowerGridComponent> components;
};

// Owns the authoritative power-topology cache and performs one power allocation step.
// Allocates across the whole grid each tick; only explicit storage retains energy.
class PowerGridSystem final {
  public:
    void simulate(World& world,
                  const PlayerRegistry& players,
                  const DefinitionRegistry& definitions,
                  std::uint64_t tick,
                  std::vector<PowerEvent>& events);

    void reset();
    void markDirty(PlayerId player);
    void markAllDirty();

    [[nodiscard]] PowerFailureReason connect(World& world,
                                             PlayerId player,
                                             EntityId source,
                                             EntityId target);
    [[nodiscard]] PowerFailureReason disconnect(World& world,
                                                PlayerId player,
                                                EntityId source,
                                                EntityId target);
    // Placement may create a reciprocal link before the new building is operational. The nearest
    // eligible operational device wins; stable entity IDs resolve equal-distance ties.
    [[nodiscard]] EntityId connectNearestForPlacement(World& world,
                                                      PlayerId player,
                                                      EntityId source);

    [[nodiscard]] const PowerGridSnapshot* snapshot(PlayerId player) const;

  private:
    void rebuild(PlayerId player, World& world);

    std::map<PlayerId, PowerGridSnapshot> snapshots_;
    std::set<PlayerId> dirtyPlayers_;
    std::map<PlayerId, std::uint64_t> observedEntityRevisions_;
    std::uint64_t observedReplacementRevision_{0};
    std::uint64_t nextRevision_{1};
};

} // namespace strategy
