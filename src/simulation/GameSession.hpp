#pragma once

#include "gameplay/GameplayCatalogue.hpp"
#include "players/PlayerRegistry.hpp"
#include "simulation/Command.hpp"
#include "simulation/ResourceEvent.hpp"
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

    explicit GameSession(const DefinitionRegistry& definitions,
                         std::uint32_t terrainSeed,
                         std::string playerOneCountry = "spain",
                         std::string playerTwoCountry = "japan",
                         std::string playerOneSpecialization = "unassigned",
                         std::string playerTwoSpecialization = "unassigned",
                         std::uint32_t mapChunksPerSide = Terrain::chunksPerSide,
                         float startingResourcesScale = 1.0F,
                         float resourceAbundanceScale = 1.0F);

    void update(double elapsedSeconds);
    void advanceTicks(std::uint32_t count = 1);
    bool submit(PlayerCommand command);
    void replaceWorld(std::vector<Entity> entities, std::uint32_t terrainSeed,
                      std::vector<TerrainFoundation> foundations = {},
                      std::uint32_t mapChunksPerSide = Terrain::chunksPerSide);
    void restorePlayerProgress(PlayerId player,
                               std::map<std::string, float> resources,
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
    [[nodiscard]] PlayerRegistry& players() { return players_; }
    [[nodiscard]] std::uint64_t tick() const {
        return tick_;
    }
    [[nodiscard]] std::uint32_t terrainSeed() const {
        return terrainSeed_;
    }
    [[nodiscard]] std::uint32_t mapChunksPerSide() const { return mapChunksPerSide_; }
    [[nodiscard]] std::uint64_t stateChecksum() const;
    [[nodiscard]] bool canStartRecipe(PlayerId player, EntityId producer, RecipeId recipe) const;
    [[nodiscard]] bool canStartUpgrade(PlayerId player, EntityId researcher,
                                       const std::string& upgrade) const;
    [[nodiscard]] std::vector<ResourceEvent> consumeResourceEvents() {
        auto result = std::move(resourceEvents_);
        resourceEvents_.clear();
        return result;
    }

  private:
    PlayerRegistry players_;
    const DefinitionRegistry& gameplay_;
    World world_;
    std::deque<PlayerCommand> commands_;
    std::map<PlayerId, std::uint64_t> lastSequence_;
    std::uint64_t tick_{0};
    std::uint32_t terrainSeed_{0};
    double accumulator_{0.0};
    Terrain terrain_;
    std::uint32_t mapChunksPerSide_{15};
    Navigation navigation_;
    float resourceAbundanceScale_{1.0F};
    std::vector<ResourceEvent> resourceEvents_;

    void simulateTick();
    void apply(const PlayerCommand& command);
    void beginRecharge(Entity& entity);
    void finishRecharge(Entity& entity);
    void stopUnit(Entity& entity);
    void updateExploration();
    [[nodiscard]] float stat(const Entity& entity, GameplayStat stat) const;
    void initializeEntity(Entity& entity);
};

} // namespace strategy
