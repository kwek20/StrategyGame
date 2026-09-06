#pragma once

#include "world/Entity.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace strategy {

struct EntityArchetypeTag;
struct BuildingArchetypeTag;
struct ResourceArchetypeTag;
struct ResourceTag;
struct WeaponTag;
struct PowerDeviceTag;
struct RecipeTag;
using UnitArchetypeId = DefinitionId<EntityArchetypeTag>;
using BuildingArchetypeId = DefinitionId<EntityArchetypeTag>;
using ResourceArchetypeId = DefinitionId<EntityArchetypeTag>;
using ResourceId = DefinitionId<ResourceTag>;
using WeaponId = DefinitionId<WeaponTag>;
using PowerDeviceId = DefinitionId<PowerDeviceTag>;
using RecipeId = DefinitionId<RecipeTag>;
struct PresentationTag;
using PresentationId = DefinitionId<PresentationTag>;

enum class GameplayStat {
    health,
    movementSpeed,
    sightRange,
    attackDamage,
    attackRange,
    attackCooldown,
    gatherRate,
    carryCapacity,
    trainingTime,
    productionSpeed,
    upgradeTime,
    researchTime,
    productionUpgradeAmount,
    maximumProductionUpgrades
};

enum class ModifierOperation { add, multiply, overrideValue, minimum, maximum };

struct ModifierTarget {
    std::string entity, producer;
    std::vector<std::string> tags, producerTags, productTags;
    std::vector<std::string> weaponTags, recipeTags, resourceTypes, powerDeviceTags;
};

struct GameplayModifier {
    std::string id;
    GameplayStat stat;
    ModifierTarget target;
    ModifierOperation operation;
    float value;
    std::int32_t priority{100};
};

struct RuntimeModifierLayers {
    std::vector<GameplayModifier> permanentUpgrades;
    std::vector<GameplayModifier> producingBuildingUpgrades;
    std::vector<GameplayModifier> temporaryEffects;
    std::vector<GameplayModifier> powerState;
};

struct UpgradeDefinition {
    std::string id, researchRecipe, nameKey, icon, exclusiveGroup;
    std::uint32_t maximumLevel{1};
    bool affectsProducer{false};
    std::vector<std::string> prerequisites, allowedResearchers;
    std::vector<GameplayModifier> modifiers;
};

enum class ResourceStorageKind { stockpile, network };

struct ResourceDefinition {
    std::string id, nameKey, icon;
    std::unordered_set<std::string> tags;
    ResourceStorageKind storage{ResourceStorageKind::stockpile};
    bool enabled{true};
};

struct WeaponDefinition {
    std::string id, damageType;
    std::unordered_set<std::string> tags;
    float damage{0.0F};
    float range{0.0F};
    std::uint32_t cooldownTicks{1};
    float accuracy{1.0F};
    std::unordered_set<std::string> validTargets;
};

struct PowerDeviceDefinition {
    std::string id;
    std::unordered_set<std::string> tags;
    float production{0.0F}, consumption{0.0F}, storage{0.0F};
    float connectionRange{0.0F}, transferLimit{0.0F}, chargePerTick{0.0F};
};

enum class RecipeProductKind { unit, building, resource };

struct RecipeProduct {
    RecipeProductKind kind{RecipeProductKind::unit};
    std::string id;
    std::uint32_t amount{1};
};

struct RecipeDefinition {
    std::string id, producer;
    RecipeProduct product;
    std::unordered_map<std::string, float> cost;
    std::unordered_set<std::string> tags;
    std::uint32_t durationTicks{1};
    float constructionPower{0.0F};
    float workStep{1.0F};
    float dronePowerPerStep{1.0F};
};

struct CountryDefinition {
    std::string id, nameKey, specializationId{"unassigned"};
};

struct MovementDefinition {
    std::string type{"ground"};
    float speed{0.0F};
};

struct BatteryDefinition {
    float capacity{0.0F}, movementDrainPerSecond{0.0F}, reserveThreshold{0.0F};
};

struct EntityArchetype {
    std::string id;
    std::string nameKey, presentation;
    EntityKind kind{EntityKind::decoration};
    float collisionRadius{1.0F};
    float interactionMargin{0.0F}, spawnClearance{0.0F};
    std::uint32_t spawnCandidateCount{0};
    std::string resourceType, upgradeTo;
    float resourceCapacity{0.0F};
    struct Generation {
        std::string stream;
        std::uint32_t clusterPairs{0}, nodesPerCluster{0}, attemptsPerCluster{0};
        float spread{0.0F}, centerExtent{0.0F};
    };
    std::optional<Generation> generation;
    MovementDefinition movement;
    std::optional<BatteryDefinition> battery;
    std::optional<PowerDeviceId> powerDevice;
    std::vector<WeaponId> weapons;
    std::unordered_set<std::string> tags, components;
    std::unordered_map<GameplayStat, float> stats;
    std::vector<std::string> availableUpgrades;
};

struct StartingEntityDefinition {
    std::string archetype, nameKey;
    glm::vec3 position{0.0F};
    bool directlyControllable{false};
};

struct MatchRulesDefinition {
    std::vector<StartingEntityDefinition> playerOne, playerTwo;
    std::vector<std::string> buildPalette, generatedResourceNodes;
    std::string trainingUpgradeProduct;
    std::uint32_t unitLimit{100};
    std::map<std::string, float> startingResources;
    float terrainEdgeMargin{0.0F}, minimumResourceHeight{0.0F}, maximumResourceHeight{1.0F};
    float maximumResourceSlope{0.0F}, baseExclusionRadius{0.0F};
};

using UnitDefinition = EntityArchetype;
using BuildingDefinition = EntityArchetype;
using ResourceNodeDefinition = EntityArchetype;

class DefinitionRegistry final {
  public:
    DefinitionRegistry(
        const std::filesystem::path& units = "assets/gameplay/units.json",
        const std::filesystem::path& buildings = "assets/gameplay/buildings.json",
        const std::filesystem::path& resourceNodes = "assets/gameplay/resource_nodes.json",
        const std::filesystem::path& resources = "assets/gameplay/resources.json",
        const std::filesystem::path& weapons = "assets/gameplay/weapons.json",
        const std::filesystem::path& powerDevices = "assets/gameplay/power.json",
        const std::filesystem::path& recipes = "assets/gameplay/recipes.json",
        const std::filesystem::path& countries = "assets/gameplay/countries.json",
        const std::filesystem::path& specializations = "assets/gameplay/specializations.json",
        const std::filesystem::path& presentations = "assets/entities.json",
        const std::filesystem::path& localization = "assets/text/en_us.json",
        const std::filesystem::path& textures = "assets/textures",
        const std::filesystem::path& rules = "assets/gameplay/rules.json",
        const std::filesystem::path& upgrades = "assets/gameplay/upgrades.json");

    [[nodiscard]] float resolve(GameplayStat stat,
                                const std::string& country,
                                const std::string& specialization,
                                const std::string& entity,
                                const std::string& producer = {},
                                const RuntimeModifierLayers& layers = {},
                                ResourceId resource = {}) const;
    [[nodiscard]] float productionDuration(const std::string& country,
                                           const std::string& specialization,
                                           const std::string& producer,
                                           const std::string& product,
                                           float producerMultiplier = 1.0F,
                                           const RuntimeModifierLayers& layers = {}) const;
    [[nodiscard]] bool canTrain(const std::string& producer, const std::string& product) const;
    [[nodiscard]] const EntityArchetype* archetype(const std::string& entity) const;
    [[nodiscard]] const EntityArchetype* archetype(EntityArchetypeId entity) const { return archetype(entity.value); }
    [[nodiscard]] const UnitDefinition* unit(UnitArchetypeId id) const;
    [[nodiscard]] const BuildingDefinition* building(BuildingArchetypeId id) const;
    [[nodiscard]] const ResourceNodeDefinition* resource(ResourceArchetypeId id) const;
    [[nodiscard]] const ResourceDefinition* resourceType(ResourceId id) const;
    [[nodiscard]] const WeaponDefinition* weapon(WeaponId id) const;
    [[nodiscard]] const PowerDeviceDefinition* powerDevice(PowerDeviceId id) const;
    [[nodiscard]] const RecipeDefinition* recipe(RecipeId id) const;
    [[nodiscard]] const RecipeDefinition*
    productionRecipe(const std::string& producer, const std::string& product) const;
    [[nodiscard]] std::vector<const RecipeDefinition*> recipesForProducer(const std::string& producer) const;
    [[nodiscard]] std::vector<const UpgradeDefinition*> upgradesForResearcher(const std::string& researcher) const;
    [[nodiscard]] const std::vector<CountryDefinition>& countries() const { return countryList_; }
    [[nodiscard]] const MatchRulesDefinition& matchRules() const { return matchRules_; }
    [[nodiscard]] const UpgradeDefinition* upgrade(const std::string& id) const;
    [[nodiscard]] const std::unordered_map<std::string, UpgradeDefinition>& upgrades() const { return upgrades_; }
    [[nodiscard]] float collisionRadius(const std::string& entity) const;
    void initializeEntity(Entity& entity) const;

  private:
    std::unordered_map<std::string, EntityArchetype> entities_;
    std::unordered_set<std::string> unitIds_, buildingIds_, resourceIds_;
    std::unordered_map<std::string, ResourceDefinition> resourceTypes_;
    std::unordered_map<std::string, WeaponDefinition> weapons_;
    std::unordered_map<std::string, PowerDeviceDefinition> powerDevices_;
    std::unordered_map<std::string, RecipeDefinition> recipes_;
    std::vector<CountryDefinition> countryList_;
    std::unordered_map<std::string, std::vector<GameplayModifier>> countries_, specializations_;
    std::unordered_set<std::string> modifierIds_;
    std::unordered_set<std::string> presentationIds_, localizationKeys_;
    std::filesystem::path textureRoot_;
    MatchRulesDefinition matchRules_;
    std::unordered_map<std::string, UpgradeDefinition> upgrades_;

    void loadArchetypes(const std::filesystem::path&, const char* collection, EntityKind);
    void loadResources(const std::filesystem::path&);
    void loadWeapons(const std::filesystem::path&);
    void loadPowerDevices(const std::filesystem::path&);
    void loadRecipes(const std::filesystem::path&);
    void loadReferenceKeys(const std::filesystem::path&, const std::filesystem::path&);
    void loadRules(const std::filesystem::path&);
    void loadUpgrades(const std::filesystem::path&);
    void validateReferences() const;
    static GameplayStat parseStat(const std::string&);
    static ModifierOperation parseOperation(const std::string&);
    [[nodiscard]] bool matches(const GameplayModifier&,
                               const EntityArchetype&,
                               const EntityArchetype*,
                               const WeaponDefinition* = nullptr,
                               const RecipeDefinition* = nullptr,
                               const ResourceDefinition* = nullptr,
                               const PowerDeviceDefinition* = nullptr) const;
};

} // namespace strategy
