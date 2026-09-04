#pragma once
#include "world/Entity.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace strategy {
enum class GameplayStat { health,movementSpeed,sightRange,attackDamage,attackRange,attackCooldown,gatherRate,carryCapacity,trainingTime,productionSpeed,upgradeTime,researchTime,productionUpgradeAmount,maximumProductionUpgrades };
enum class ModifierOperation { add,multiply,overrideValue,minimum,maximum };
struct ModifierTarget {std::string entity,producer;std::vector<std::string> tags,producerTags,productTags;};
struct GameplayModifier {std::string id;GameplayStat stat;ModifierTarget target;ModifierOperation operation;float value;};
struct EntityArchetype {std::string id;EntityKind kind{EntityKind::decoration};std::unordered_set<std::string> tags,components;std::unordered_map<GameplayStat,float> stats;std::vector<std::string> canTrain,availableUpgrades;};
class GameplayCatalogue final {
public:
    GameplayCatalogue(const std::filesystem::path& entities="assets/gameplay/entities.json",const std::filesystem::path& countries="assets/gameplay/countries.json",const std::filesystem::path& specializations="assets/gameplay/specializations.json");
    [[nodiscard]] float resolve(GameplayStat stat,const std::string& country,const std::string& specialization,const std::string& entity,const std::string& producer={})const;
    [[nodiscard]] float productionDuration(const std::string& country,const std::string& specialization,const std::string& producer,const std::string& product,float producerMultiplier=1.0F)const;
    [[nodiscard]] bool canTrain(const std::string& producer,const std::string& product)const;
    [[nodiscard]] const EntityArchetype* archetype(const std::string& entity)const;
    void initializeEntity(Entity& entity)const;
private:
    std::unordered_map<std::string,EntityArchetype> entities_;std::unordered_map<std::string,std::vector<GameplayModifier>> countries_,specializations_;
    static GameplayStat parseStat(const std::string&);static ModifierOperation parseOperation(const std::string&);
    [[nodiscard]] bool matches(const GameplayModifier&,const EntityArchetype&,const EntityArchetype*)const;
};
}
