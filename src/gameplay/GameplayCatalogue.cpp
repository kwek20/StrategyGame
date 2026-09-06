#include "gameplay/GameplayCatalogue.hpp"

#include <algorithm>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <stdexcept>
namespace strategy {
namespace {
rapidjson::Document document(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error("Could not open gameplay data: " + path.string());
    rapidjson::IStreamWrapper input(stream);
    rapidjson::Document result;
    result.ParseStream(input);
    if (result.HasParseError() || !result.IsObject())
        throw std::runtime_error("Invalid gameplay data: " + path.string());
    return result;
}
std::vector<std::string> strings(const rapidjson::Value& object, const char* key) {
    std::vector<std::string> result;
    if (object.HasMember(key) && object[key].IsArray())
        for (const auto& value : object[key].GetArray())
            if (value.IsString())
                result.emplace_back(value.GetString());
    return result;
}

float requiredNumber(const rapidjson::Value& object,
                     const char* key,
                     const std::string& context) {
    if (!object.HasMember(key) || !object[key].IsNumber())
        throw std::runtime_error(context + " requires numeric field '" + key + "'");
    return object[key].GetFloat();
}

void applyModifier(float& result, const GameplayModifier& modifier) {
    switch (modifier.operation) {
    case ModifierOperation::add: result += modifier.value; break;
    case ModifierOperation::multiply: result *= modifier.value; break;
    case ModifierOperation::overrideValue: result = modifier.value; break;
    case ModifierOperation::minimum: result = std::max(result, modifier.value); break;
    case ModifierOperation::maximum: result = std::min(result, modifier.value); break;
    }
}
} // namespace
GameplayStat DefinitionRegistry::parseStat(const std::string& value) {
    static const std::unordered_map<std::string, GameplayStat> values{
        {"health", GameplayStat::health},
        {"movementSpeed", GameplayStat::movementSpeed},
        {"sightRange", GameplayStat::sightRange},
        {"attackDamage", GameplayStat::attackDamage},
        {"attackRange", GameplayStat::attackRange},
        {"attackCooldown", GameplayStat::attackCooldown},
        {"gatherRate", GameplayStat::gatherRate},
        {"carryCapacity", GameplayStat::carryCapacity},
        {"trainingTime", GameplayStat::trainingTime},
        {"productionSpeed", GameplayStat::productionSpeed},
        {"upgradeTime", GameplayStat::upgradeTime},
        {"researchTime", GameplayStat::researchTime},
        {"productionUpgradeAmount", GameplayStat::productionUpgradeAmount},
        {"maximumProductionUpgrades", GameplayStat::maximumProductionUpgrades}};
    const auto found = values.find(value);
    if (found == values.end())
        throw std::runtime_error("Unknown gameplay stat: " + value);
    return found->second;
}
ModifierOperation DefinitionRegistry::parseOperation(const std::string& value) {
    if (value == "add")
        return ModifierOperation::add;
    if (value == "multiply")
        return ModifierOperation::multiply;
    if (value == "override")
        return ModifierOperation::overrideValue;
    if (value == "minimum")
        return ModifierOperation::minimum;
    if (value == "maximum")
        return ModifierOperation::maximum;
    throw std::runtime_error("Unknown modifier operation: " + value);
}
void DefinitionRegistry::loadArchetypes(const std::filesystem::path& path,
                                        const char* collection,
                                        EntityKind kind) {
    auto data = document(path);
    if (!data.HasMember(collection) || !data[collection].IsObject())
        throw std::runtime_error("Gameplay definition file has no '" + std::string(collection) +
                                 "' object: " + path.string());
    for (auto item = data[collection].MemberBegin();
         item != data[collection].MemberEnd();
         ++item) {
        EntityArchetype archetype;
        archetype.id = item->name.GetString();
        archetype.kind = kind;
        if (!item->value.IsObject())
            throw std::runtime_error("Invalid definition '" + archetype.id + "' in " +
                                     path.string());
        if (!item->value.HasMember("collisionRadius") ||
            !item->value["collisionRadius"].IsNumber() ||
            item->value["collisionRadius"].GetFloat() < 0.0F)
            throw std::runtime_error("Definition '" + archetype.id +
                                     "' requires a non-negative collisionRadius in " + path.string());
        archetype.collisionRadius = item->value["collisionRadius"].GetFloat();
        if (item->value.HasMember("interactionMargin"))
            archetype.interactionMargin = requiredNumber(
                item->value, "interactionMargin", "Definition '" + archetype.id + "'");
        if (item->value.HasMember("spawn")) {
            const auto& spawn = item->value["spawn"];
            if (!spawn.IsObject() || !spawn.HasMember("clearance") ||
                !spawn["clearance"].IsNumber() || !spawn.HasMember("candidateCount") ||
                !spawn["candidateCount"].IsUint() || spawn["candidateCount"].GetUint() == 0)
                throw std::runtime_error("Definition '" + archetype.id + "' has invalid spawn");
            archetype.spawnClearance = spawn["clearance"].GetFloat();
            archetype.spawnCandidateCount = spawn["candidateCount"].GetUint();
        }
        if (item->value.HasMember("upgradeTo") && item->value["upgradeTo"].IsString())
            archetype.upgradeTo = item->value["upgradeTo"].GetString();
        if (item->value.HasMember("resourceType") && item->value["resourceType"].IsString())
            archetype.resourceType = item->value["resourceType"].GetString();
        if (item->value.HasMember("capacity"))
            archetype.resourceCapacity = requiredNumber(
                item->value, "capacity", "Definition '" + archetype.id + "'");
        if (item->value.HasMember("generation")) {
            const auto& generation = item->value["generation"];
            if (!generation.IsObject() || !generation.HasMember("stream") ||
                !generation["stream"].IsString() || !generation.HasMember("clusterPairs") ||
                !generation["clusterPairs"].IsUint() ||
                !generation.HasMember("nodesPerCluster") ||
                !generation["nodesPerCluster"].IsUint() ||
                !generation.HasMember("attemptsPerCluster") ||
                !generation["attemptsPerCluster"].IsUint() || !generation.HasMember("spread") ||
                !generation["spread"].IsNumber() || !generation.HasMember("centerExtent") ||
                !generation["centerExtent"].IsNumber())
                throw std::runtime_error("Definition '" + archetype.id +
                                         "' has invalid generation settings");
            archetype.generation = EntityArchetype::Generation{
                generation["stream"].GetString(),
                generation["clusterPairs"].GetUint(),
                generation["nodesPerCluster"].GetUint(),
                generation["attemptsPerCluster"].GetUint(),
                generation["spread"].GetFloat(),
                generation["centerExtent"].GetFloat()};
        }
        if (item->value.HasMember("nameKey") && item->value["nameKey"].IsString())
            archetype.nameKey = item->value["nameKey"].GetString();
        if (item->value.HasMember("presentation") && item->value["presentation"].IsString())
            archetype.presentation = item->value["presentation"].GetString();
        if (archetype.presentation.empty())
            archetype.presentation = archetype.id;
        if (archetype.nameKey.empty())
            archetype.nameKey = "entity." + archetype.id;
        for (const std::string& tag : strings(item->value, "tags"))
            archetype.tags.insert(tag);
        for (const std::string& component : strings(item->value, "components"))
            archetype.components.insert(component);
        static const std::unordered_set<std::string> knownComponents{"health",
                                                                     "vision",
                                                                     "unit",
                                                                     "gatherer",
                                                                     "combat",
                                                                     "resource",
                                                                     "production",
                                                                     "buildingUpgrades",
                                                                     "upgrades"};
        for (const std::string& component : archetype.components)
            if (!knownComponents.contains(component))
                throw std::runtime_error("Definition '" + archetype.id +
                                         "' has unknown component '" + component + "' in " +
                                         path.string());
        archetype.availableUpgrades = strings(item->value, "availableUpgrades");
        for (const std::string& weapon : strings(item->value, "weapons"))
            archetype.weapons.emplace_back(weapon);
        if (item->value.HasMember("health") && item->value["health"].IsNumber())
            archetype.stats[GameplayStat::health] = item->value["health"].GetFloat();
        if (item->value.HasMember("vision") && item->value["vision"].IsObject()) {
            const auto& vision = item->value["vision"];
            archetype.stats[GameplayStat::sightRange] =
                requiredNumber(vision, "range", "Definition '" + archetype.id + "' vision");
        }
        if (item->value.HasMember("movement") && item->value["movement"].IsObject()) {
            const auto& movement = item->value["movement"];
            if (movement.HasMember("type") && movement["type"].IsString())
                archetype.movement.type = movement["type"].GetString();
            if (archetype.movement.type != "ground" && archetype.movement.type != "flying")
                throw std::runtime_error("Definition '" + archetype.id +
                                         "' has unsupported movement type '" +
                                         archetype.movement.type + "'");
            archetype.movement.speed =
                requiredNumber(movement, "speed", "Definition '" + archetype.id + "' movement");
            archetype.stats[GameplayStat::movementSpeed] = archetype.movement.speed;
        }
        if (item->value.HasMember("cargo") && item->value["cargo"].IsObject())
            archetype.stats[GameplayStat::carryCapacity] = requiredNumber(
                item->value["cargo"], "capacity", "Definition '" + archetype.id + "' cargo");
        if (item->value.HasMember("battery") && item->value["battery"].IsObject()) {
            const auto& battery = item->value["battery"];
            archetype.battery = BatteryDefinition{
                requiredNumber(battery, "capacity", "Definition '" + archetype.id + "' battery"),
                requiredNumber(battery,
                               "movementDrainPerSecond",
                               "Definition '" + archetype.id + "' battery"),
                requiredNumber(
                    battery, "reserveThreshold", "Definition '" + archetype.id + "' battery")};
            if (archetype.battery->capacity <= 0.0F ||
                archetype.battery->movementDrainPerSecond < 0.0F ||
                archetype.battery->reserveThreshold < 0.0F ||
                archetype.battery->reserveThreshold > archetype.battery->capacity)
                throw std::runtime_error("Definition '" + archetype.id +
                                         "' has invalid battery values");
        }
        if (item->value.HasMember("powerDevice") && item->value["powerDevice"].IsString())
            archetype.powerDevice.emplace(item->value["powerDevice"].GetString());
        if (item->value.HasMember("stats") && item->value["stats"].IsObject())
            for (auto stat = item->value["stats"].MemberBegin();
                 stat != item->value["stats"].MemberEnd();
                 ++stat)
                if (stat->value.IsNumber())
                    archetype.stats[parseStat(stat->name.GetString())] = stat->value.GetFloat();
        const std::string id = archetype.id;
        if (!entities_.emplace(id, std::move(archetype)).second)
            throw std::runtime_error("Duplicate gameplay definition id '" + id + "'");
        auto& ids = kind == EntityKind::unit       ? unitIds_
                    : kind == EntityKind::building ? buildingIds_
                                                   : resourceIds_;
        ids.insert(id);
    }
}

void DefinitionRegistry::loadResources(const std::filesystem::path& path) {
    auto data = document(path);
    if (!data.HasMember("resources") || !data["resources"].IsObject())
        throw std::runtime_error("Resource definition file has no 'resources' object: " +
                                 path.string());
    for (auto item = data["resources"].MemberBegin(); item != data["resources"].MemberEnd(); ++item) {
        if (!item->value.IsObject())
            throw std::runtime_error("Invalid resource definition in " + path.string());
        ResourceDefinition definition;
        definition.id = item->name.GetString();
        const auto& value = item->value;
        if (!value.HasMember("nameKey") || !value["nameKey"].IsString() ||
            !value.HasMember("icon") || !value["icon"].IsString() ||
            !value.HasMember("storage") || !value["storage"].IsString())
            throw std::runtime_error("Resource '" + definition.id +
                                     "' requires nameKey, icon, and storage");
        definition.nameKey = value["nameKey"].GetString();
        definition.icon = value["icon"].GetString();
        for (const std::string& tag : strings(value, "tags"))
            definition.tags.insert(tag);
        const std::string storage = value["storage"].GetString();
        if (storage == "stockpile")
            definition.storage = ResourceStorageKind::stockpile;
        else if (storage == "network")
            definition.storage = ResourceStorageKind::network;
        else
            throw std::runtime_error("Resource '" + definition.id +
                                     "' has unknown storage kind '" + storage + "'");
        if (value.HasMember("enabled") && value["enabled"].IsBool())
            definition.enabled = value["enabled"].GetBool();
        if (!resourceTypes_.emplace(definition.id, std::move(definition)).second)
            throw std::runtime_error("Duplicate resource definition id '" +
                                     std::string(item->name.GetString()) + "'");
    }
}

void DefinitionRegistry::loadWeapons(const std::filesystem::path& path) {
    auto data = document(path);
    if (!data.HasMember("weapons") || !data["weapons"].IsObject())
        throw std::runtime_error("Weapon definition file has no 'weapons' object: " + path.string());
    for (auto item = data["weapons"].MemberBegin(); item != data["weapons"].MemberEnd(); ++item) {
        if (!item->value.IsObject())
            throw std::runtime_error("Invalid weapon definition in " + path.string());
        WeaponDefinition definition;
        definition.id = item->name.GetString();
        const auto& value = item->value;
        if (!value.HasMember("damage") || !value["damage"].IsObject())
            throw std::runtime_error("Weapon '" + definition.id + "' requires damage");
        const auto& damage = value["damage"];
        if (!damage.HasMember("type") || !damage["type"].IsString())
            throw std::runtime_error("Weapon '" + definition.id + "' requires damage.type");
        definition.damageType = damage["type"].GetString();
        static const std::unordered_set<std::string> damageTypes{
            "kinetic", "explosive", "incendiary", "electronic"};
        if (!damageTypes.contains(definition.damageType))
            throw std::runtime_error("Weapon '" + definition.id +
                                     "' has unknown damage type '" + definition.damageType + "'");
        for (const std::string& tag : strings(value, "tags"))
            definition.tags.insert(tag);
        definition.damage = requiredNumber(damage, "amount", "Weapon '" + definition.id + "'");
        definition.range = requiredNumber(value, "range", "Weapon '" + definition.id + "'");
        if (!value.HasMember("cooldownTicks") || !value["cooldownTicks"].IsUint() ||
            value["cooldownTicks"].GetUint() == 0)
            throw std::runtime_error("Weapon '" + definition.id +
                                     "' requires positive cooldownTicks");
        definition.cooldownTicks = value["cooldownTicks"].GetUint();
        definition.accuracy = requiredNumber(value, "accuracy", "Weapon '" + definition.id + "'");
        if (definition.damage < 0.0F || definition.range < 0.0F || definition.accuracy < 0.0F ||
            definition.accuracy > 1.0F)
            throw std::runtime_error("Weapon '" + definition.id + "' has an invalid value");
        for (const std::string& target : strings(value, "validTargets"))
            definition.validTargets.insert(target);
        static const std::unordered_set<std::string> targetTypes{"ground", "air", "structure"};
        for (const std::string& target : definition.validTargets)
            if (!targetTypes.contains(target))
                throw std::runtime_error("Weapon '" + definition.id +
                                         "' has unknown target type '" + target + "'");
        if (definition.validTargets.empty())
            throw std::runtime_error("Weapon '" + definition.id + "' requires validTargets");
        if (!weapons_.emplace(definition.id, std::move(definition)).second)
            throw std::runtime_error("Duplicate weapon definition id '" +
                                     std::string(item->name.GetString()) + "'");
    }
}

void DefinitionRegistry::loadPowerDevices(const std::filesystem::path& path) {
    auto data = document(path);
    if (!data.HasMember("devices") || !data["devices"].IsObject())
        throw std::runtime_error("Power definition file has no 'devices' object: " + path.string());
    for (auto item = data["devices"].MemberBegin(); item != data["devices"].MemberEnd(); ++item) {
        if (!item->value.IsObject())
            throw std::runtime_error("Invalid power device definition in " + path.string());
        PowerDeviceDefinition definition;
        definition.id = item->name.GetString();
        for (const std::string& tag : strings(item->value, "tags"))
            definition.tags.insert(tag);
        const auto optional = [&item](const char* key) {
            return item->value.HasMember(key) && item->value[key].IsNumber()
                       ? item->value[key].GetFloat()
                       : 0.0F;
        };
        definition.production = optional("production");
        definition.consumption = optional("consumption");
        definition.storage = optional("storage");
        definition.connectionRange = optional("connectionRange");
        definition.transferLimit = optional("transferLimit");
        definition.chargePerTick = optional("chargePerTick");
        if (definition.production < 0.0F || definition.consumption < 0.0F ||
            definition.storage < 0.0F || definition.connectionRange < 0.0F ||
            definition.transferLimit < 0.0F || definition.chargePerTick < 0.0F)
            throw std::runtime_error("Power device '" + definition.id + "' has a negative value");
        if (!powerDevices_.emplace(definition.id, std::move(definition)).second)
            throw std::runtime_error("Duplicate power device definition id '" +
                                     std::string(item->name.GetString()) + "'");
    }
}

void DefinitionRegistry::loadRecipes(const std::filesystem::path& path) {
    auto data = document(path);
    if (!data.HasMember("recipes") || !data["recipes"].IsArray())
        throw std::runtime_error("Recipe definition file has no 'recipes' array: " + path.string());
    for (const auto& value : data["recipes"].GetArray()) {
        if (!value.IsObject() || !value.HasMember("id") || !value["id"].IsString() ||
            !value.HasMember("product") || !value["product"].IsObject() ||
            !value.HasMember("durationTicks") || !value["durationTicks"].IsUint() ||
            value["durationTicks"].GetUint() == 0)
            throw std::runtime_error("Invalid recipe definition in " + path.string());
        RecipeDefinition definition;
        definition.id = value["id"].GetString();
        if (value.HasMember("producer")) {
            if (!value["producer"].IsString())
                throw std::runtime_error("Recipe producer must be a string");
            definition.producer = value["producer"].GetString();
        }
        definition.durationTicks = value["durationTicks"].GetUint();
        if (value.HasMember("constructionPower")) definition.constructionPower = requiredNumber(value, "constructionPower", "Recipe '" + definition.id + "'");
        if (value.HasMember("workStep")) definition.workStep = requiredNumber(value, "workStep", "Recipe '" + definition.id + "'");
        if (value.HasMember("dronePowerPerStep")) definition.dronePowerPerStep = requiredNumber(value, "dronePowerPerStep", "Recipe '" + definition.id + "'");
        for (const std::string& tag : strings(value, "tags"))
            definition.tags.insert(tag);
        const auto& product = value["product"];
        if (product.HasMember("unit") && product["unit"].IsString()) {
            definition.product.kind = RecipeProductKind::unit;
            definition.product.id = product["unit"].GetString();
        } else if (product.HasMember("building") && product["building"].IsString()) {
            definition.product.kind = RecipeProductKind::building;
            definition.product.id = product["building"].GetString();
        } else
            throw std::runtime_error("Recipe '" + definition.id + "' has an invalid product");
        if (product.HasMember("amount")) {
            if (!product["amount"].IsUint() || product["amount"].GetUint() == 0)
                throw std::runtime_error("Recipe '" + definition.id + "' has invalid amount");
            definition.product.amount = product["amount"].GetUint();
        }
        if (value.HasMember("cost")) {
            if (!value["cost"].IsObject())
                throw std::runtime_error("Recipe '" + definition.id + "' has invalid cost");
            for (auto cost = value["cost"].MemberBegin(); cost != value["cost"].MemberEnd(); ++cost) {
                if (!cost->value.IsNumber() || cost->value.GetFloat() < 0.0F)
                    throw std::runtime_error("Recipe '" + definition.id + "' has invalid cost");
                definition.cost.emplace(cost->name.GetString(), cost->value.GetFloat());
            }
        }
        if (!recipes_.emplace(definition.id, std::move(definition)).second)
            throw std::runtime_error("Duplicate recipe definition id '" +
                                     std::string(value["id"].GetString()) + "'");
    }
}

void DefinitionRegistry::loadReferenceKeys(const std::filesystem::path& presentationPath,
                                           const std::filesystem::path& localizationPath) {
    auto presentations = document(presentationPath);
    if (!presentations.HasMember("entities") || !presentations["entities"].IsObject())
        throw std::runtime_error("Presentation catalogue has no 'entities' object: " +
                                 presentationPath.string());
    for (auto item = presentations["entities"].MemberBegin();
         item != presentations["entities"].MemberEnd();
         ++item)
        presentationIds_.insert(item->name.GetString());
    auto localization = document(localizationPath);
    for (auto item = localization.MemberBegin(); item != localization.MemberEnd(); ++item)
        localizationKeys_.insert(item->name.GetString());
}

void DefinitionRegistry::validateReferences() const {
    for (const auto& [id, entity] : entities_) {
        if (!presentationIds_.contains(entity.presentation))
            throw std::runtime_error("Entity '" + id + "' references unknown presentation '" +
                                     entity.presentation + "'");
        if (!localizationKeys_.contains(entity.nameKey))
            throw std::runtime_error("Entity '" + id + "' references unknown localization key '" +
                                     entity.nameKey + "'");
        for (const WeaponId& weaponId : entity.weapons)
            if (!weapons_.contains(weaponId.value))
                throw std::runtime_error("Entity '" + id + "' references unknown weapon '" +
                                         weaponId.value + "'");
        if (entity.powerDevice && !powerDevices_.contains(entity.powerDevice->value))
            throw std::runtime_error("Entity '" + id + "' references unknown power device '" +
                                     entity.powerDevice->value + "'");
        if (!entity.upgradeTo.empty() && !buildingIds_.contains(entity.upgradeTo))
            throw std::runtime_error("Entity '" + id + "' references unknown upgrade target '" +
                                     entity.upgradeTo + "'");
        if (!entity.resourceType.empty() && !resourceTypes_.contains(entity.resourceType))
            throw std::runtime_error("Entity '" + id + "' references unknown resource type '" +
                                     entity.resourceType + "'");
    }
    if (!unitIds_.contains(matchRules_.trainingUpgradeProduct))
        throw std::runtime_error("Match rules reference unknown training upgrade product '" +
                                 matchRules_.trainingUpgradeProduct + "'");
    for (const std::string& id : matchRules_.buildPalette)
        if (!entities_.contains(id))
            throw std::runtime_error("Build palette references unknown entity '" + id + "'");
    for (const std::string& id : matchRules_.generatedResourceNodes)
        if (!resourceIds_.contains(id) || !entities_.at(id).generation)
            throw std::runtime_error("Generated resource list references invalid node '" + id + "'");
    for (const auto* starts : {&matchRules_.playerOne, &matchRules_.playerTwo})
        for (const StartingEntityDefinition& start : *starts) {
            if (!entities_.contains(start.archetype))
                throw std::runtime_error("Starting entity references unknown archetype '" +
                                         start.archetype + "'");
            if (!localizationKeys_.contains(start.nameKey))
                throw std::runtime_error("Starting entity references unknown localization key '" +
                                         start.nameKey + "'");
        }
    for (const auto& [id, resource] : resourceTypes_) {
        if (!localizationKeys_.contains(resource.nameKey))
            throw std::runtime_error("Resource '" + id +
                                     "' references unknown localization key '" +
                                     resource.nameKey + "'");
        bool iconExists = false;
        for (const char* extension : {".png", ".ppm", ".jpg", ".jpeg"})
            iconExists = iconExists ||
                         std::filesystem::exists(textureRoot_ / (resource.icon + extension));
        if (!iconExists)
            throw std::runtime_error("Resource '" + id + "' references missing icon '" +
                                     resource.icon + "'");
    }
    for (const auto& [id, definition] : upgrades_) {
        // Research recipes are validated when the recipe catalogue is present; fixture-specific
        // catalogues may intentionally omit unrelated upgrades.
        if (!localizationKeys_.contains(definition.nameKey))
            throw std::runtime_error("Upgrade '" + id + "' references unknown localization key");
        bool iconExists = false;
        for (const char* extension : {".png", ".ppm", ".jpg", ".jpeg"})
            iconExists = iconExists || std::filesystem::exists(textureRoot_ / (definition.icon + extension));
        if (!iconExists)
            throw std::runtime_error("Upgrade '" + id + "' references missing icon '" + definition.icon + "'");
        for (const auto& modifier : definition.modifiers)
            if (!modifier.target.entity.empty() && !entities_.contains(modifier.target.entity))
                throw std::runtime_error("Upgrade '" + id + "' modifier targets unknown entity");
    }
    std::unordered_set<std::string> productionRelationships;
    for (const auto& [id, definition] : recipes_) {
        if (definition.product.kind == RecipeProductKind::unit && definition.producer.empty())
            throw std::runtime_error("Unit recipe '" + id + "' requires a producer");
        if (!definition.producer.empty() && !buildingIds_.contains(definition.producer))
            throw std::runtime_error("Recipe '" + id + "' references unknown producer '" +
                                     definition.producer + "'");
        const bool validProduct = definition.product.kind == RecipeProductKind::unit
                                      ? unitIds_.contains(definition.product.id)
                                      : buildingIds_.contains(definition.product.id);
        if (!validProduct)
            throw std::runtime_error("Recipe '" + id + "' references unknown product '" +
                                     definition.product.id + "'");
        if (!definition.producer.empty()) {
            const std::string relationship = definition.producer + "\n" + definition.product.id;
            if (!productionRelationships.insert(relationship).second)
                throw std::runtime_error("Duplicate producer/product recipe for '" +
                                         definition.producer + "' and '" +
                                         definition.product.id + "'");
        }
        for (const auto& [resource, amount] : definition.cost) {
            (void)amount;
            const auto found = resourceTypes_.find(resource);
            if (found == resourceTypes_.end())
                throw std::runtime_error("Recipe '" + id + "' references unknown resource '" +
                                         resource + "'");
            if (!found->second.enabled || found->second.storage != ResourceStorageKind::stockpile)
                throw std::runtime_error("Recipe '" + id + "' cannot reserve resource '" +
                                         resource + "'");
        }
    }
    for (const auto& [source, groups] : {std::pair{"country", &countries_},
                                        std::pair{"specialization", &specializations_}})
        for (const auto& [group, modifiers] : *groups)
            for (const GameplayModifier& modifier : modifiers) {
                const auto& target = modifier.target;
                if (!target.entity.empty() && !entities_.contains(target.entity))
                    throw std::runtime_error(std::string(source) + " '" + group +
                                             "' modifier '" + modifier.id +
                                             "' targets unknown entity '" + target.entity + "'");
                if (!target.producer.empty() && !buildingIds_.contains(target.producer))
                    throw std::runtime_error(std::string(source) + " '" + group +
                                             "' modifier '" + modifier.id +
                                             "' targets unknown producer '" + target.producer + "'");
                for (const std::string& resource : target.resourceTypes)
                    if (!resourceTypes_.contains(resource))
                        throw std::runtime_error(std::string(source) + " '" + group +
                                                 "' modifier '" + modifier.id +
                                                 "' targets unknown resource '" + resource + "'");
                for (const std::string& tag : target.weaponTags) {
                    const bool known = std::any_of(
                        weapons_.begin(), weapons_.end(), [&](const auto& entry) {
                            return entry.second.tags.contains(tag);
                        });
                    if (!known)
                        throw std::runtime_error(std::string(source) + " '" + group +
                                                 "' modifier '" + modifier.id +
                                                 "' targets unknown weapon tag '" + tag + "'");
                }
                for (const std::string& tag : target.recipeTags) {
                    const bool known =
                        std::any_of(recipes_.begin(), recipes_.end(), [&](const auto& entry) {
                            return entry.second.tags.contains(tag);
                        });
                    if (!known)
                        throw std::runtime_error(std::string(source) + " '" + group +
                                                 "' modifier '" + modifier.id +
                                                 "' targets unknown recipe tag '" + tag + "'");
                }
                for (const std::string& tag : target.powerDeviceTags) {
                    const bool known = std::any_of(
                        powerDevices_.begin(), powerDevices_.end(), [&](const auto& entry) {
                            return entry.second.tags.contains(tag);
                        });
                    if (!known)
                        throw std::runtime_error(std::string(source) + " '" + group +
                                                 "' modifier '" + modifier.id +
                                                 "' targets unknown power-device tag '" + tag +
                                                 "'");
                }
            }
    for (const CountryDefinition& country : countryList_)
        if (!localizationKeys_.contains(country.nameKey))
            throw std::runtime_error("Country '" + country.id +
                                     "' references unknown localization key '" + country.nameKey +
                                     "'");
        else if (!specializations_.contains(country.specializationId))
            throw std::runtime_error("Country '" + country.id +
                                     "' references unknown specialization '" +
                                     country.specializationId + "'");
}

void DefinitionRegistry::loadUpgrades(const std::filesystem::path& path) {
    const auto data = document(path);
    if (!data.HasMember("upgrades") || !data["upgrades"].IsArray())
        throw std::runtime_error("Upgrade definitions require an upgrades array");
    for (const auto& value : data["upgrades"].GetArray()) {
        if (!value.IsObject() || !value.HasMember("id") || !value["id"].IsString() ||
            !value.HasMember("researchRecipe") || !value["researchRecipe"].IsString() ||
            !value.HasMember("maximumLevel") || !value["maximumLevel"].IsUint() ||
            !value.HasMember("nameKey") || !value["nameKey"].IsString() ||
            !value.HasMember("icon") || !value["icon"].IsString() ||
            !value.HasMember("modifiers") || !value["modifiers"].IsArray())
            throw std::runtime_error("Invalid upgrade definition");
        UpgradeDefinition upgrade;
        upgrade.id = value["id"].GetString();
        upgrade.researchRecipe = value["researchRecipe"].GetString();
        upgrade.maximumLevel = value["maximumLevel"].GetUint();
        upgrade.affectsProducer = value.HasMember("affectsProducer") && value["affectsProducer"].IsBool() && value["affectsProducer"].GetBool();
        upgrade.nameKey = value["nameKey"].GetString();
        upgrade.icon = value["icon"].GetString();
        if (value.HasMember("exclusiveGroup") && value["exclusiveGroup"].IsString())
            upgrade.exclusiveGroup = value["exclusiveGroup"].GetString();
        upgrade.prerequisites = strings(value, "prerequisites");
        upgrade.allowedResearchers = strings(value, "allowedResearchers");
        for (const auto& item : value["modifiers"].GetArray()) {
            if (!item.IsObject() || !item.HasMember("id") || !item["id"].IsString() ||
                !item.HasMember("stat") || !item["stat"].IsString() ||
                !item.HasMember("operation") || !item["operation"].IsString() ||
                !item.HasMember("value") || !item["value"].IsNumber())
                throw std::runtime_error("Invalid modifier in upgrade '" + upgrade.id + "'");
            GameplayModifier modifier{item["id"].GetString(), parseStat(item["stat"].GetString()),
                                      {}, parseOperation(item["operation"].GetString()),
                                      item["value"].GetFloat()};
            if (item.HasMember("priority")) modifier.priority = item["priority"].GetInt();
            if (item.HasMember("target") && item["target"].IsObject()) {
                const auto& target = item["target"];
                if (target.HasMember("entity") && target["entity"].IsString())
                    modifier.target.entity = target["entity"].GetString();
                modifier.target.tags = strings(target, "tags");
                modifier.target.producerTags = strings(target, "producerTags");
                modifier.target.productTags = strings(target, "productTags");
            }
            upgrade.modifiers.push_back(std::move(modifier));
        }
        if (!upgrades_.emplace(upgrade.id, std::move(upgrade)).second)
            throw std::runtime_error("Duplicate upgrade definition");
    }
}

void DefinitionRegistry::loadRules(const std::filesystem::path& path) {
    const auto data = document(path);
    const auto number = [&](const char* key) {
        return requiredNumber(data, key, "Match rules");
    };
    matchRules_.terrainEdgeMargin = number("terrainEdgeMargin");
    matchRules_.minimumResourceHeight = number("minimumResourceHeight");
    matchRules_.maximumResourceHeight = number("maximumResourceHeight");
    matchRules_.maximumResourceSlope = number("maximumResourceSlope");
    matchRules_.baseExclusionRadius = number("baseExclusionRadius");
    if (!data.HasMember("trainingUpgradeProduct") ||
        !data["trainingUpgradeProduct"].IsString())
        throw std::runtime_error("Match rules require trainingUpgradeProduct");
    matchRules_.trainingUpgradeProduct = data["trainingUpgradeProduct"].GetString();
    matchRules_.buildPalette = strings(data, "buildPalette");
    matchRules_.generatedResourceNodes = strings(data, "generatedResourceNodes");
    const auto loadStart = [&](const char* key, std::vector<StartingEntityDefinition>& output) {
        if (!data.HasMember(key) || !data[key].IsArray())
            throw std::runtime_error("Match rules require array '" + std::string(key) + "'");
        for (const auto& value : data[key].GetArray()) {
            if (!value.IsObject() || !value.HasMember("archetype") ||
                !value["archetype"].IsString() || !value.HasMember("nameKey") ||
                !value["nameKey"].IsString() || !value.HasMember("position") ||
                !value["position"].IsArray() || value["position"].Size() != 3)
                throw std::runtime_error("Invalid starting entity in '" + std::string(key) + "'");
            StartingEntityDefinition start;
            start.archetype = value["archetype"].GetString();
            start.nameKey = value["nameKey"].GetString();
            start.position = {value["position"][0].GetFloat(),
                              value["position"][1].GetFloat(),
                              value["position"][2].GetFloat()};
            start.directlyControllable = value.HasMember("directlyControllable") &&
                                         value["directlyControllable"].IsBool() &&
                                         value["directlyControllable"].GetBool();
            output.push_back(std::move(start));
        }
    };
    loadStart("playerOneStart", matchRules_.playerOne);
    loadStart("playerTwoStart", matchRules_.playerTwo);
}

DefinitionRegistry::DefinitionRegistry(const std::filesystem::path& unitPath,
                                       const std::filesystem::path& buildingPath,
                                       const std::filesystem::path& resourceNodePath,
                                       const std::filesystem::path& resourcePath,
                                       const std::filesystem::path& weaponPath,
                                       const std::filesystem::path& powerDevicePath,
                                       const std::filesystem::path& recipePath,
                                       const std::filesystem::path& countryPath,
                                       const std::filesystem::path& specializationPath,
                                       const std::filesystem::path& presentationPath,
                                       const std::filesystem::path& localizationPath,
                                       const std::filesystem::path& textureRoot,
                                       const std::filesystem::path& rulesPath,
                                       const std::filesystem::path& upgradePath)
    : textureRoot_(textureRoot) {
    loadReferenceKeys(presentationPath, localizationPath);
    loadArchetypes(unitPath, "units", EntityKind::unit);
    loadArchetypes(buildingPath, "buildings", EntityKind::building);
    loadArchetypes(resourceNodePath, "resourceNodes", EntityKind::resource);
    loadResources(resourcePath);
    loadWeapons(weaponPath);
    loadPowerDevices(powerDevicePath);
    loadRecipes(recipePath);
    loadRules(rulesPath);
    loadUpgrades(upgradePath);
    const auto loadModifiers =
        [this](const std::filesystem::path& path,
               const char* collection,
               std::unordered_map<std::string, std::vector<GameplayModifier>>& output) {
            auto data = document(path);
            if (!data.HasMember(collection) || !data[collection].IsArray())
                throw std::runtime_error("Missing modifier collection: " + std::string(collection));
            for (const auto& entry : data[collection].GetArray()) {
                if (!entry.IsObject() || !entry.HasMember("id") || !entry["id"].IsString())
                    continue;
                const std::string definitionId = entry["id"].GetString();
                if (output.contains(definitionId))
                    throw std::runtime_error("Duplicate " + std::string(collection) +
                                             " definition id '" + definitionId + "'");
                auto& list = output[definitionId];
                if (std::string_view(collection) == "countries") {
                    if (!entry.HasMember("nameKey") || !entry["nameKey"].IsString())
                        throw std::runtime_error("Country requires nameKey");
                    CountryDefinition country;
                    country.id = entry["id"].GetString();
                    country.nameKey = entry["nameKey"].GetString();
                    if (entry.HasMember("specialization") && entry["specialization"].IsString())
                        country.specializationId = entry["specialization"].GetString();
                    countryList_.push_back(std::move(country));
                }
                if (!entry.HasMember("modifiers") || !entry["modifiers"].IsArray())
                    continue;
                for (const auto& value : entry["modifiers"].GetArray()) {
                    if (!value.IsObject() || !value.HasMember("id") || !value["id"].IsString() ||
                        !value.HasMember("stat") || !value["stat"].IsString() ||
                        !value.HasMember("operation") || !value["operation"].IsString() ||
                        !value.HasMember("value") || !value["value"].IsNumber())
                        throw std::runtime_error("Invalid gameplay modifier");
                    GameplayModifier modifier{value["id"].GetString(),
                                              parseStat(value["stat"].GetString()),
                                              {},
                                              parseOperation(value["operation"].GetString()),
                                              value["value"].GetFloat()};
                    if (!modifierIds_.insert(modifier.id).second)
                        throw std::runtime_error("Duplicate gameplay modifier id '" + modifier.id +
                                                 "'");
                    if (value.HasMember("priority")) {
                        if (!value["priority"].IsInt())
                            throw std::runtime_error("Gameplay modifier priority must be an integer");
                        modifier.priority = value["priority"].GetInt();
                    }
                    if (value.HasMember("target") && value["target"].IsObject()) {
                        const auto& target = value["target"];
                        if (target.HasMember("entity") && target["entity"].IsString())
                            modifier.target.entity = target["entity"].GetString();
                        if (target.HasMember("producer") && target["producer"].IsString())
                            modifier.target.producer = target["producer"].GetString();
                        modifier.target.tags = strings(target, "tags");
                        modifier.target.producerTags = strings(target, "producerTags");
                        modifier.target.productTags = strings(target, "productTags");
                        modifier.target.weaponTags = strings(target, "weaponTags");
                        modifier.target.recipeTags = strings(target, "recipeTags");
                        modifier.target.resourceTypes = strings(target, "resourceTypes");
                        modifier.target.powerDeviceTags = strings(target, "powerDeviceTags");
                    }
                    list.push_back(std::move(modifier));
                }
                std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
                    return a.priority != b.priority ? a.priority < b.priority : a.id < b.id;
                });
            }
        };
    loadModifiers(countryPath, "countries", countries_);
    loadModifiers(specializationPath, "specializations", specializations_);
    if (countryList_.empty())
        throw std::runtime_error("Country catalogue contains no countries: " +
                                 countryPath.string());
    validateReferences();
}
bool DefinitionRegistry::matches(const GameplayModifier& modifier,
                                 const EntityArchetype& subject,
                                 const EntityArchetype* producer,
                                 const WeaponDefinition* weaponDefinition,
                                 const RecipeDefinition* recipeDefinition,
                                 const ResourceDefinition* resourceDefinition,
                                 const PowerDeviceDefinition* powerDefinition) const {
    if (!modifier.target.entity.empty() && modifier.target.entity != subject.id)
        return false;
    for (const auto& tag : modifier.target.tags)
        if (!subject.tags.contains(tag))
            return false;
    if (!modifier.target.productTags.empty())
        for (const auto& tag : modifier.target.productTags)
            if (!subject.tags.contains(tag))
                return false;
    if (!modifier.target.producer.empty() &&
        (!producer || producer->id != modifier.target.producer))
        return false;
    for (const auto& tag : modifier.target.producerTags)
        if (!producer || !producer->tags.contains(tag))
            return false;
    for (const auto& tag : modifier.target.weaponTags)
        if (!weaponDefinition || !weaponDefinition->tags.contains(tag))
            return false;
    for (const auto& tag : modifier.target.recipeTags)
        if (!recipeDefinition || !recipeDefinition->tags.contains(tag))
            return false;
    for (const auto& type : modifier.target.resourceTypes)
        if (!resourceDefinition || resourceDefinition->id != type)
            return false;
    for (const auto& tag : modifier.target.powerDeviceTags)
        if (!powerDefinition || !powerDefinition->tags.contains(tag))
            return false;
    return true;
}
float DefinitionRegistry::resolve(GameplayStat stat,
                                  const std::string& country,
                                  const std::string& specialization,
                                  const std::string& entity,
                                  const std::string& producer,
                                  const RuntimeModifierLayers& layers,
                                  ResourceId resourceContext) const {
    const auto subjectIt = entities_.find(entity);
    if (subjectIt == entities_.end())
        return 0.0F;
    const auto base = subjectIt->second.stats.find(stat);
    float result = base == subjectIt->second.stats.end() ? 0.0F : base->second;
    if (base == subjectIt->second.stats.end() && !subjectIt->second.weapons.empty()) {
        if (const WeaponDefinition* definition = weapon(subjectIt->second.weapons.front())) {
            if (stat == GameplayStat::attackDamage)
                result = definition->damage;
            else if (stat == GameplayStat::attackRange)
                result = definition->range;
            else if (stat == GameplayStat::attackCooldown)
                result = static_cast<float>(definition->cooldownTicks) / 30.0F;
        }
    }
    const EntityArchetype* producerType = nullptr;
    if (!producer.empty()) {
        const auto found = entities_.find(producer);
        if (found != entities_.end())
            producerType = &found->second;
    }
    const WeaponDefinition* weaponDefinition =
        subjectIt->second.weapons.empty() ? nullptr : weapon(subjectIt->second.weapons.front());
    const PowerDeviceDefinition* powerDefinition = subjectIt->second.powerDevice
                                                        ? powerDevice(*subjectIt->second.powerDevice)
                                                        : nullptr;
    const ResourceDefinition* resourceDefinition =
        resourceContext.empty() ? nullptr : resourceType(resourceContext);
    const auto applySource = [&](const auto& source, const std::string& id) {
        if (const auto found = source.find(id); found != source.end())
            for (const auto& modifier : found->second)
                if (modifier.stat == stat &&
                    matches(modifier,
                            subjectIt->second,
                            producerType,
                            weaponDefinition,
                            nullptr,
                            resourceDefinition,
                            powerDefinition))
                    applyModifier(result, modifier);
    };
    // Modifier layers are intentionally applied independently and in authoritative order.
    // Upgrade, temporary-effect, and power-state layers plug in after these two catalogues.
    applySource(countries_, country);
    applySource(specializations_, specialization);
    const auto applyRuntimeLayer = [&](const std::vector<GameplayModifier>& source) {
        std::vector<const GameplayModifier*> ordered;
        ordered.reserve(source.size());
        for (const GameplayModifier& modifier : source)
            ordered.push_back(&modifier);
        std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
            return left->priority != right->priority ? left->priority < right->priority
                                                     : left->id < right->id;
        });
        for (const GameplayModifier* modifier : ordered)
            if (modifier->stat == stat &&
                matches(*modifier,
                        subjectIt->second,
                        producerType,
                        weaponDefinition,
                        nullptr,
                        resourceDefinition,
                        powerDefinition))
                applyModifier(result, *modifier);
    };
    applyRuntimeLayer(layers.permanentUpgrades);
    applyRuntimeLayer(layers.producingBuildingUpgrades);
    applyRuntimeLayer(layers.temporaryEffects);
    applyRuntimeLayer(layers.powerState);
    return result;
}
float DefinitionRegistry::productionDuration(const std::string& country,
                                             const std::string& specialization,
                                             const std::string& producer,
                                             const std::string& product,
                                             float producerMultiplier,
                                             const RuntimeModifierLayers& layers) const {
    const RecipeDefinition* production = productionRecipe(producer, product);
    if (!production)
        return 0.0F;
    const EntityArchetype* productType = archetype(product);
    const EntityArchetype* producerType = archetype(producer);
    float time = static_cast<float>(production->durationTicks) / 30.0F;
    const auto applySource = [&](const auto& source, const std::string& id) {
        if (const auto found = source.find(id); found != source.end())
            for (const auto& modifier : found->second)
                if (modifier.stat == GameplayStat::trainingTime && productType &&
                    matches(modifier, *productType, producerType, nullptr, production))
                    applyModifier(time, modifier);
    };
    applySource(countries_, country);
    applySource(specializations_, specialization);
    const auto applyRuntimeLayer = [&](const std::vector<GameplayModifier>& source) {
        std::vector<const GameplayModifier*> ordered;
        ordered.reserve(source.size());
        for (const GameplayModifier& modifier : source)
            ordered.push_back(&modifier);
        std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
            return left->priority != right->priority ? left->priority < right->priority
                                                     : left->id < right->id;
        });
        for (const GameplayModifier* modifier : ordered)
            if (modifier->stat == GameplayStat::trainingTime && productType &&
                matches(*modifier, *productType, producerType, nullptr, production))
                applyModifier(time, *modifier);
    };
    applyRuntimeLayer(layers.permanentUpgrades);
    applyRuntimeLayer(layers.producingBuildingUpgrades);
    applyRuntimeLayer(layers.temporaryEffects);
    applyRuntimeLayer(layers.powerState);
    const float speed =
        resolve(GameplayStat::productionSpeed, country, specialization, producer, producer) *
        producerMultiplier;
    return speed > 0.0F ? time / speed : time;
}
bool DefinitionRegistry::canTrain(const std::string& producer, const std::string& product) const {
    const RecipeDefinition* definition = productionRecipe(producer, product);
    return definition && definition->product.kind == RecipeProductKind::unit;
}
const EntityArchetype* DefinitionRegistry::archetype(const std::string& entity) const {
    const auto found = entities_.find(entity);
    return found == entities_.end() ? nullptr : &found->second;
}
const UpgradeDefinition* DefinitionRegistry::upgrade(const std::string& id) const {
    const auto found = upgrades_.find(id);
    return found == upgrades_.end() ? nullptr : &found->second;
}
const UnitDefinition* DefinitionRegistry::unit(UnitArchetypeId id) const {
    return unitIds_.contains(id.value) ? archetype(id.value) : nullptr;
}

const BuildingDefinition* DefinitionRegistry::building(BuildingArchetypeId id) const {
    return buildingIds_.contains(id.value) ? archetype(id.value) : nullptr;
}

const ResourceNodeDefinition* DefinitionRegistry::resource(ResourceArchetypeId id) const {
    return resourceIds_.contains(id.value) ? archetype(id.value) : nullptr;
}

const ResourceDefinition* DefinitionRegistry::resourceType(ResourceId id) const {
    const auto found = resourceTypes_.find(id.value);
    return found == resourceTypes_.end() ? nullptr : &found->second;
}

const WeaponDefinition* DefinitionRegistry::weapon(WeaponId id) const {
    const auto found = weapons_.find(id.value);
    return found == weapons_.end() ? nullptr : &found->second;
}

const PowerDeviceDefinition* DefinitionRegistry::powerDevice(PowerDeviceId id) const {
    const auto found = powerDevices_.find(id.value);
    return found == powerDevices_.end() ? nullptr : &found->second;
}

const RecipeDefinition* DefinitionRegistry::recipe(RecipeId id) const {
    const auto found = recipes_.find(id.value);
    return found == recipes_.end() ? nullptr : &found->second;
}

const RecipeDefinition*
DefinitionRegistry::productionRecipe(const std::string& producer,
                                     const std::string& product) const {
    const RecipeDefinition* result = nullptr;
    for (const auto& [id, definition] : recipes_) {
        (void)id;
        if (definition.producer != producer || definition.product.id != product)
            continue;
        if (result != nullptr)
            throw std::runtime_error("Multiple recipes produce '" + product + "' in '" +
                                     producer + "'");
        result = &definition;
    }
    return result;
}

float DefinitionRegistry::collisionRadius(const std::string& entity) const {
    const EntityArchetype* definition = archetype(entity);
    if (!definition)
        throw std::runtime_error("Unknown entity definition for collision: " + entity);
    return definition->collisionRadius;
}

void DefinitionRegistry::initializeEntity(Entity& entity) const {
    const auto found = entities_.find(entity.archetype.value);
    if (found == entities_.end())
        return;
    const EntityArchetype& type = found->second;
    entity.archetype = EntityArchetypeId{type.id};
    entity.presentation = PresentationId{type.presentation};
    entity.kind = type.kind;
    const auto has = [&](const char* name) { return type.components.contains(name); };
    const auto value = [&](GameplayStat stat, float fallback) {
        const auto item = type.stats.find(stat);
        return item == type.stats.end() ? fallback : item->second;
    };
    if (has("health")) {
        entity.health.emplace();
        entity.health.maximum = value(GameplayStat::health, entity.health.maximum);
        entity.health.current = entity.health.maximum;
    }
    if (has("vision")) {
        entity.vision.emplace();
        entity.vision.sightRange = value(GameplayStat::sightRange, entity.vision.sightRange);
    }
    if (has("unit")) {
        entity.unitControl.emplace();
        entity.unitControl.movementSpeed =
            value(GameplayStat::movementSpeed, entity.unitControl.movementSpeed);
    }
    if (has("gatherer")) {
        entity.gatherer.emplace();
        entity.gatherer.gatherPerSecond =
            value(GameplayStat::gatherRate, entity.gatherer.gatherPerSecond);
        entity.gatherer.carryCapacity =
            value(GameplayStat::carryCapacity, entity.gatherer.carryCapacity);
    }
    if (type.movement.type == "flying") {
        entity.flight.emplace();
        entity.flight.altitude = std::max(entity.transform.position.y, entity.flight.minimumAltitude);
        entity.transform.position.y = entity.flight.altitude;
    }
    if (type.battery) {
        entity.battery.emplace();
        entity.battery.capacity = type.battery->capacity;
        entity.battery.charge = type.battery->capacity;
        entity.battery.movementDrainPerSecond = type.battery->movementDrainPerSecond;
        entity.battery.reserveThreshold = type.battery->reserveThreshold;
    }
    if (has("combat")) {
        entity.combat.emplace();
        entity.combat.damage = resolve(GameplayStat::attackDamage, {}, {}, type.id);
        entity.combat.range = resolve(GameplayStat::attackRange, {}, {}, type.id);
        entity.combat.cooldownSeconds =
            resolve(GameplayStat::attackCooldown, {}, {}, type.id);
    }
    if (has("resource"))
        entity.resource.emplace();
    if (has("production")) {
        entity.production.emplace();
    }
    if (has("buildingUpgrades")) {
        entity.buildingUpgrades.emplace();
    }
    if (has("upgrades"))
        entity.upgrades.emplace();
}
} // namespace strategy
