#pragma once

#include "players/Player.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <map>
#include <vector>

namespace strategy {

struct EntityArchetypeTag;
struct PresentationTag;
template <typename Tag> struct DefinitionId {
    std::string value;
    DefinitionId() = default;
    explicit DefinitionId(std::string_view identifier) : value(identifier) {}
    [[nodiscard]] bool empty() const { return value.empty(); }
    friend bool operator==(const DefinitionId&, const DefinitionId&) = default;
};
using EntityArchetypeId = DefinitionId<EntityArchetypeTag>;
using PresentationId = DefinitionId<PresentationTag>;

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

enum class UnitOrderKind : std::uint8_t {
    idle,
    move,
    gather,
    returnResources,
    attack,
    construct,
    repair,
    returningToCharge,
    charging,
    stranded
};

enum class EntityKind : std::uint8_t { decoration, unit, building, resource };

struct VisionComponent {
    float sightRange{0.0F};
};

inline float effectiveSightRange(float resolvedRange, float terrainHeight) {
    return resolvedRange + std::max(0.0F, terrainHeight) * 0.8F;
}

struct UnitComponent {
    bool directlyControllable{false};
    glm::vec2 directInput{0.0F};
    bool running{false};
    glm::vec3 strategicDestination{0.0F};
    bool hasStrategicDestination{false};
    float movementSpeed{0.0F};
    UnitOrderKind order{UnitOrderKind::idle};
    EntityId orderTarget{0};
};

struct Health {
    float current{0.0F};
    float maximum{0.0F};
};

struct GathererComponent {
    std::string carriedResource;
    float carriedAmount{0.0F};
    float carryCapacity{0.0F};
    float gatherPerSecond{0.0F};
    EntityId sourceTarget{0};
    EntityId deliveryTarget{0};
    EntityId preferredProcessor{0};
    std::string preferredOutput;
    bool repeatGathering{true};
    bool waitingForProcessor{false};
};
struct FlightComponent {
    float altitude{6.0F};
    float verticalSpeed{4.0F};
    float minimumAltitude{2.0F};
    float maximumAltitude{24.0F};
};
struct BatteryComponent {
    float charge{0.0F};
    float capacity{0.0F};
    float movementDrainPerSecond{0.0F};
    float reserveThreshold{0.0F};
    bool returningToCharge{false};
    bool hasSuspendedOrder{false};
    UnitOrderKind suspendedOrder{UnitOrderKind::idle};
    EntityId suspendedTarget{0};
    glm::vec3 suspendedDestination{0.0F};
    bool suspendedHasDestination{false};
    EntityId chargerTarget{0};
};
enum class BuildingLifecycleState : std::uint8_t {
    planned,
    underConstruction,
    operational,
    damaged,
    destroyed
};
struct ConstructionComponent {
    std::string recipeId;
    float powerRequired{0.0F};
    float powerProgress{0.0F};
    BuildingLifecycleState state{BuildingLifecycleState::planned};
    bool placementValid{true};
};
struct CombatComponent {
    float damage{0.0F};
    float range{0.0F};
    float cooldownSeconds{0.0F};
    float cooldownRemaining{0.0F};
};
struct ResourceComponent {
    std::string type;
    float remaining{0.0F};
};
enum class ProcessorOperationalState : std::uint8_t {
    idle, powered, underpowered, blocked, processing, offline
};
struct ProcessorComponent {
    // Raw cargo is authoritative and remains local until a powered conversion succeeds.
    std::map<std::string, float> bufferedInputs;
    bool waitingForPower{false};
    std::uint64_t lastConversionTick{0};
    std::uint32_t activityTicksRemaining{0};
    ProcessorOperationalState state{ProcessorOperationalState::idle};
};
enum class PowerOperationalState : std::uint8_t { notApplicable, powered, underpowered, offline };
struct PowerComponent {
    float generation{0.0F};
    float demand{0.0F};
    float supplied{0.0F};
    std::int32_t priority{100};
    PowerOperationalState state{PowerOperationalState::offline};
};
struct UpgradeComponent {
    std::map<std::string, std::uint32_t> levels;
};

// Rebuildable local caches are deliberately excluded from saves and state checksums.
struct EntityTransientState {
    std::vector<glm::vec3> navigationPath;
    std::size_t navigationWaypoint{0};
    float navigationRetrySeconds{0.0F};
    EntityId navigationGoalEntity{0};
    float navigationInteractionRange{0.0F};
};

enum class ProductionKind : std::uint8_t { trainCharacter, upgradeBuilding, improveTraining, processResource };

struct ProductionOrder {
    ProductionKind kind{ProductionKind::trainCharacter};
    std::string recipeId;
    std::string productId;
    std::string upgradeId;
    std::string iconId;
    std::uint32_t amount{1};
    std::uint32_t durationTicks{0};
    std::uint32_t remainingTicks{0};
    std::map<std::string, float> reservedCosts;
};

struct ProductionComponent {
    float productionSpeedMultiplier{1.0F};
    std::uint32_t productionSpeedUpgrades{0};
    std::deque<ProductionOrder> queue;
};

struct BuildingUpgradeComponent {
    std::uint32_t level{1};
};

template <class Data> struct Component : Data {
    bool present{false};
    explicit operator bool() const {
        return present;
    }
    Data* operator->() {
        return this;
    }
    const Data* operator->() const {
        return this;
    }
    Data& emplace() {
        present = true;
        return *this;
    }
    void reset() {
        static_cast<Data&>(*this) = Data{};
        present = false;
    }
};

struct Entity {
    EntityId id{0};
    std::string name;
    EntityArchetypeId archetype;
    PresentationId presentation;

    [[nodiscard]] const std::string& gameplayId() const { return archetype.value; }
    [[nodiscard]] const std::string& renderId() const {
        return presentation.value;
    }
    EntityKind kind{EntityKind::decoration};
    Transform transform;
    Authority authority;
    Component<Health> health;
    Component<VisionComponent> vision;
    Component<UnitComponent> unitControl;
    Component<GathererComponent> gatherer;
    Component<FlightComponent> flight;
    Component<BatteryComponent> battery;
    Component<ConstructionComponent> construction;
    Component<CombatComponent> combat;
    Component<ResourceComponent> resource;
    Component<ProcessorComponent> processor;
    Component<PowerComponent> power;
    Component<ProductionComponent> production;
    Component<BuildingUpgradeComponent> buildingUpgrades;
    Component<UpgradeComponent> upgrades;
    EntityTransientState transient;
};

[[nodiscard]] inline bool isOperational(const Entity& entity) {
    return !entity.construction ||
           entity.construction.state == BuildingLifecycleState::operational ||
           entity.construction.state == BuildingLifecycleState::damaged;
}

} // namespace strategy
