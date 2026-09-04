#pragma once

#include <cstdint>
#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

#include "players/Player.hpp"

namespace strategy {

using EntityId = std::uint64_t;

struct Transform {
    glm::vec3 position{0.0F};
    glm::vec3 rotationDegrees{0.0F};
    glm::vec3 scale{1.0F};
};

struct Authority {
    PlayerId owner{0};
    PlayerId directController{0};
};

enum class ResourceKind : std::uint8_t { none,wood,stone,gold };
enum class UnitOrderKind : std::uint8_t { idle,move,gather,returnResources,attack };

enum class EntityKind : std::uint8_t { decoration,unit,building,resource };

struct VisionComponent { float sightRange{10.0F}; };

inline float effectiveSightRange(float resolvedRange,float terrainHeight) {
    return resolvedRange+std::max(0.0F,terrainHeight)*0.8F;
}

struct UnitComponent {
    bool directlyControllable{false};
    glm::vec2 directInput{0.0F};
    bool running{false};
    glm::vec3 strategicDestination{0.0F};
    bool hasStrategicDestination{false};
    float movementSpeed{7.0F};
    float sightRange{10.0F};
    std::vector<glm::vec3> navigationPath;
    std::size_t navigationWaypoint{0};
    float navigationRetrySeconds{0.0F};
    UnitOrderKind order{UnitOrderKind::idle};
    EntityId orderTarget{0};
    ResourceKind carriedKind{ResourceKind::none};
    float carriedAmount{0.0F};
    float carryCapacity{10.0F};
    float gatherPerSecond{4.0F};
};

struct Health {
    float current{100.0F};
    float maximum{100.0F};
};

struct GathererComponent { ResourceKind carriedKind{ResourceKind::none};float carriedAmount{0.0F};float carryCapacity{10.0F};float gatherPerSecond{4.0F}; };
struct CombatComponent { float damage{0.0F};float range{0.0F};float cooldownSeconds{1.0F};float cooldownRemaining{0.0F}; };
struct ResourceComponent { ResourceKind kind{ResourceKind::none}; float remaining{0.0F}; };
struct UpgradeComponent { std::unordered_map<std::string,std::uint32_t> levels; };

enum class ProductionKind : std::uint8_t {
    trainCharacter,
    upgradeBuilding,
    improveTraining
};

struct ProductionOrder {
    ProductionKind kind{ProductionKind::trainCharacter};
    float durationSeconds{0.0F};
    float remainingSeconds{0.0F};
};

struct ProductionComponent {
    std::uint32_t level{1}; // Legacy mirror; persisted building tier lives in buildingUpgrades.
    float characterBuildSeconds{10.0F};
    float productionSpeedMultiplier{1.0F};
    std::uint32_t productionSpeedUpgrades{0};
    std::deque<ProductionOrder> queue;
};

struct BuildingUpgradeComponent { std::uint32_t level{1}; };

template<class Data> struct Component : Data {
    bool present{false};
    explicit operator bool()const{return present;}
    Data* operator->(){return this;}
    const Data* operator->()const{return this;}
    Data& emplace(){present=true;return *this;}
    void reset(){static_cast<Data&>(*this)=Data{};present=false;}
};

struct Entity {
    EntityId id{0};
    std::string name;
    std::string modelKey;
    EntityKind kind{EntityKind::decoration};
    Transform transform;
    Authority authority;
    Component<Health> health;
    Component<VisionComponent> vision;
    Component<UnitComponent> unitControl;
    Component<GathererComponent> gatherer;
    Component<CombatComponent> combat;
    Component<ResourceComponent> resource;
    Component<ProductionComponent> production;
    Component<BuildingUpgradeComponent> buildingUpgrades;
    Component<UpgradeComponent> upgrades;
};

} // namespace strategy
