#pragma once

#include "gameplay/GameplayCatalogue.hpp"
#include "players/PlayerRegistry.hpp"
#include "simulation/Command.hpp"
#include "terrain/Terrain.hpp"
#include "world/Navigation.hpp"
#include "world/World.hpp"

#include <cstdint>
#include <deque>
#include <map>

namespace strategy {

class GameSession final {
  public:
    static constexpr double fixedTickSeconds = 1.0 / 30.0;

    explicit GameSession(std::uint32_t terrainSeed,
                         std::string playerOneCountry = "spain",
                         std::string playerTwoCountry = "japan",
                         std::string playerOneSpecialization = "unassigned",
                         std::string playerTwoSpecialization = "unassigned");

    void update(double elapsedSeconds);
    void advanceTicks(std::uint32_t count = 1);
    bool submit(PlayerCommand command);
    void replaceWorld(std::vector<Entity> entities, std::uint32_t terrainSeed);
    void restorePlayerProgress(PlayerId player,
                               float wood,
                               float stone,
                               float gold,
                               std::vector<std::uint8_t> discovered,
                               std::vector<LastKnownEntity> intelligence = {});

    [[nodiscard]] World& world() {
        return world_;
    }
    [[nodiscard]] const World& world() const {
        return world_;
    }
    [[nodiscard]] const PlayerRegistry& players() const {
        return players_;
    }
    [[nodiscard]] std::uint64_t tick() const {
        return tick_;
    }
    [[nodiscard]] std::uint32_t terrainSeed() const {
        return terrainSeed_;
    }
    [[nodiscard]] std::uint64_t stateChecksum() const;

  private:
    PlayerRegistry players_;
    GameplayCatalogue gameplay_;
    World world_;
    std::deque<PlayerCommand> commands_;
    std::map<PlayerId, std::uint64_t> lastSequence_;
    std::uint64_t tick_{0};
    std::uint32_t terrainSeed_{0};
    double accumulator_{0.0};
    Terrain terrain_;
    Navigation navigation_;

    void simulateTick();
    void apply(const PlayerCommand& command);
    void updateExploration();
    [[nodiscard]] float stat(const Entity& entity, GameplayStat stat) const;
    void initializeEntity(Entity& entity);
};

} // namespace strategy
