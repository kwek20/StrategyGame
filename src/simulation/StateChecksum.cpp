#include "simulation/StateChecksum.hpp"

#include "players/PlayerRegistry.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <string_view>
#include <vector>

namespace strategy {
namespace {
class Hash {
  public:
    template <class T> void value(T value) {
        const auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
        for (std::byte byte : bytes) { result_ ^= std::to_integer<unsigned char>(byte); result_ *= 1099511628211ULL; }
    }
    void text(std::string_view text) { for (char c : text) value(c); value(std::uint8_t{0}); }
    void vector(glm::vec3 value) { this->value(value.x); this->value(value.y); this->value(value.z); }
    [[nodiscard]] std::uint64_t result() const { return result_; }
  private: std::uint64_t result_{14695981039346656037ULL};
};
}

std::uint64_t authoritativeStateChecksum(const World& world,
                                         const PlayerRegistry& players,
                                         std::uint32_t terrainSeed,
                                         std::uint64_t tick) {
    Hash hash; hash.value(terrainSeed); hash.value(tick);
    std::vector<const Player*> orderedPlayers;
    for (const Player& player : players.players()) orderedPlayers.push_back(&player);
    std::sort(orderedPlayers.begin(), orderedPlayers.end(), [](auto a, auto b){ return a->id < b->id; });
    hash.value(static_cast<std::uint64_t>(orderedPlayers.size()));
    for (const Player* player : orderedPlayers) {
        hash.value(player->id); hash.text(player->countryId); hash.text(player->specializationId);
        hash.value(player->wood); hash.value(player->stone); hash.value(player->gold);
        hash.value(static_cast<std::uint64_t>(player->resources.size()));
        for (const auto& [id, amount] : player->resources) { hash.text(id); hash.value(amount); }
        for (auto discovered : player->discovered) hash.value(discovered);
    }
    std::vector<const Entity*> entities;
    for (const Entity& entity : world.entities()) entities.push_back(&entity);
    std::sort(entities.begin(), entities.end(), [](auto a, auto b){ return a->id < b->id; });
    hash.value(static_cast<std::uint64_t>(entities.size()));
    for (const Entity* entity : entities) {
        hash.value(entity->id); hash.text(entity->modelKey);
        hash.value(entity->kind); hash.vector(entity->transform.position);
        hash.vector(entity->transform.rotationDegrees); hash.vector(entity->transform.scale);
        hash.value(entity->authority.owner); hash.value(entity->authority.directController);
        hash.value(static_cast<bool>(entity->health)); if(entity->health){hash.value(entity->health.current);hash.value(entity->health.maximum);}
        hash.value(static_cast<bool>(entity->vision)); if(entity->vision) hash.value(entity->vision.sightRange);
        hash.value(static_cast<bool>(entity->unitControl)); if(entity->unitControl){
            hash.value(entity->unitControl.directlyControllable); hash.value(entity->unitControl.directInput.x); hash.value(entity->unitControl.directInput.y);
            hash.value(entity->unitControl.running); hash.vector(entity->unitControl.strategicDestination); hash.value(entity->unitControl.hasStrategicDestination);
            hash.value(entity->unitControl.movementSpeed); hash.value(entity->unitControl.order); hash.value(entity->unitControl.orderTarget); }
        hash.value(static_cast<bool>(entity->gatherer)); if(entity->gatherer){hash.text(entity->gatherer.carriedResource);hash.value(entity->gatherer.carriedAmount);hash.value(entity->gatherer.carryCapacity);hash.value(entity->gatherer.gatherPerSecond);}
        hash.value(static_cast<bool>(entity->combat)); if(entity->combat){hash.value(entity->combat.damage);hash.value(entity->combat.range);hash.value(entity->combat.cooldownSeconds);hash.value(entity->combat.cooldownRemaining);}
        hash.value(static_cast<bool>(entity->resource)); if(entity->resource){hash.text(entity->resource.type);hash.value(entity->resource.remaining);}
        hash.value(static_cast<bool>(entity->production)); if(entity->production){hash.value(entity->production.characterBuildSeconds);hash.value(entity->production.productionSpeedMultiplier);hash.value(entity->production.productionSpeedUpgrades);hash.value(static_cast<std::uint64_t>(entity->production.queue.size()));for(const auto& order:entity->production.queue){hash.value(order.kind);hash.text(order.recipeId);hash.text(order.productId);hash.value(order.amount);hash.value(order.durationTicks);hash.value(order.remainingTicks);hash.value(static_cast<std::uint64_t>(order.reservedCosts.size()));for(const auto& [id,amount]:order.reservedCosts){hash.text(id);hash.value(amount);}}}
        hash.value(static_cast<bool>(entity->buildingUpgrades)); if(entity->buildingUpgrades) hash.value(entity->buildingUpgrades.level);
        hash.value(static_cast<bool>(entity->upgrades)); if(entity->upgrades){hash.value(static_cast<std::uint64_t>(entity->upgrades.levels.size()));for(const auto& [id,level]:entity->upgrades.levels){hash.text(id);hash.value(level);}}
    }
    return hash.result();
}
} // namespace strategy
