#include "persistence/SaveGame.hpp"

#include "gameplay/GameplayCatalogue.hpp"
#include "players/PlayerRegistry.hpp"
#include "world/World.hpp"

#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace strategy {
namespace {

template <class Writer> void writeVector(Writer& writer, const char* name, const glm::vec3& value) {
    writer.Key(name);
    writer.StartArray();
    writer.Double(value.x);
    writer.Double(value.y);
    writer.Double(value.z);
    writer.EndArray();
}

glm::vec3 readVector(const rapidjson::Value& object, const char* name) {
    if (!object.HasMember(name) || !object[name].IsArray() || object[name].Size() != 3 ||
        !object[name][0].IsNumber() || !object[name][1].IsNumber() || !object[name][2].IsNumber()) {
        throw std::runtime_error(std::string("Invalid entity vector: ") + name);
    }
    return {object[name][0].GetFloat(), object[name][1].GetFloat(), object[name][2].GetFloat()};
}

} // namespace

void SaveGame::write(const std::filesystem::path& path,
                     std::uint32_t terrainSeed,
                     const World& world,
                     const PlayerRegistry* players,
                     std::uint32_t mapChunksPerSide) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("Could not write save file: " + path.string());
    }
    rapidjson::OStreamWrapper output{stream};
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer{output};
    writer.StartObject();
    writer.Key("formatVersion");
    writer.Uint(7);
    writer.Key("terrainSeed");
    writer.Uint(terrainSeed);
    writer.Key("mapChunksPerSide");
    writer.Uint(mapChunksPerSide);
    writer.Key("foundations");
    writer.StartArray();
    for (const TerrainFoundation& foundation : world.foundations()) {
        writer.StartObject();
        writeVector(writer, "center", foundation.center);
        writer.Key("innerRadius");
        writer.Double(foundation.innerRadius);
        writer.Key("outerRadius");
        writer.Double(foundation.outerRadius);
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("players");
    writer.StartArray();
    if (players)
        for (const Player& player : players->players()) {
            writer.StartObject();
            writer.Key("id");
            writer.Uint64(player.id);
            writer.Key("country");
            writer.String(player.countryId.c_str());
            writer.Key("specialization");
            writer.String(player.specializationId.c_str());
            writer.Key("resources");
            writer.StartObject();
            for (const auto& [id, amount] : player.resources) {
                writer.Key(id.c_str());
                writer.Double(amount);
            }
            writer.EndObject();
            std::string explored;
            explored.reserve(player.discovered.size());
            for (auto cell : player.discovered)
                explored.push_back(cell ? '1' : '0');
            writer.Key("discovered");
            writer.String(explored.c_str(), static_cast<rapidjson::SizeType>(explored.size()));
            writer.Key("intelligence");
            writer.StartArray();
            for (const LastKnownEntity& known : player.intelligence) {
                writer.StartObject();
                writer.Key("id");
                writer.Uint64(known.id);
                writer.Key("model");
                writer.String(known.modelKey.c_str());
                writeVector(writer, "position", known.position);
                writeVector(writer, "rotation", known.rotationDegrees);
                writeVector(writer, "scale", known.scale);
                writer.Key("building");
                writer.Bool(known.building);
                writer.EndObject();
            }
            writer.EndArray();
            writer.EndObject();
        }
    writer.EndArray();
    writer.Key("entities");
    writer.StartArray();
    for (const Entity& entity : world.entities()) {
        writer.StartObject();
        writer.Key("id");
        writer.Uint64(entity.id);
        writer.Key("name");
        writer.String(entity.name.c_str());
        const std::string& archetypeId = entity.archetype.value;
        const std::string& presentationId = entity.presentation.value;
        writer.Key("archetype");
        writer.String(archetypeId.c_str());
        writer.Key("presentation");
        writer.String(presentationId.c_str());
        writer.Key("owner");
        writer.Uint64(entity.authority.owner);
        writer.Key("directController");
        writer.Uint64(entity.authority.directController);
        writeVector(writer, "position", entity.transform.position);
        writeVector(writer, "rotation", entity.transform.rotationDegrees);
        writeVector(writer, "scale", entity.transform.scale);
        writer.Key("components");
        writer.StartObject();
        if (entity.health) {
            writer.Key("health");
            writer.StartObject();
            writer.Key("current");
            writer.Double(entity.health.current);
            writer.Key("maximum");
            writer.Double(entity.health.maximum);
            writer.EndObject();
        }
        if (entity.vision) {
            writer.Key("vision");
            writer.StartObject();
            writer.EndObject();
        }
        if (entity.unitControl) {
            writer.Key("unit");
            writer.StartObject();
            writer.Key("directlyControllable");
            writer.Bool(entity.unitControl.directlyControllable);
            writer.Key("directController");
            writer.Uint64(entity.authority.directController);
            writer.Key("directInput");
            writer.StartArray();
            writer.Double(entity.unitControl.directInput.x);
            writer.Double(entity.unitControl.directInput.y);
            writer.EndArray();
            writer.Key("running");
            writer.Bool(entity.unitControl.running);
            writeVector(writer, "destination", entity.unitControl.strategicDestination);
            writer.Key("hasDestination");
            writer.Bool(entity.unitControl.hasStrategicDestination);
            writer.Key("order");
            writer.Uint(static_cast<unsigned>(entity.unitControl.order));
            writer.Key("orderTarget");
            writer.Uint64(entity.unitControl.orderTarget);
            writer.EndObject();
        }
        if (entity.gatherer) {
            writer.Key("gatherer");
            writer.StartObject();
            writer.Key("carriedResource");
            writer.String(entity.gatherer.carriedResource.c_str());
            writer.Key("carriedAmount");
            writer.Double(entity.gatherer.carriedAmount);
            writer.Key("sourceTarget"); writer.Uint64(entity.gatherer.sourceTarget);
            writer.Key("deliveryTarget"); writer.Uint64(entity.gatherer.deliveryTarget);
            writer.Key("preferredProcessor"); writer.Uint64(entity.gatherer.preferredProcessor);
            writer.Key("preferredOutput"); writer.String(entity.gatherer.preferredOutput.c_str());
            writer.Key("repeatGathering"); writer.Bool(entity.gatherer.repeatGathering);
            writer.Key("waitingForProcessor"); writer.Bool(entity.gatherer.waitingForProcessor);
            writer.EndObject();
        }
        if (entity.flight) {
            writer.Key("flight");
            writer.StartObject();
            writer.Key("altitude"); writer.Double(entity.flight.altitude);
            writer.EndObject();
        }
        if (entity.battery) {
            writer.Key("battery");
            writer.StartObject();
            writer.Key("charge"); writer.Double(entity.battery.charge);
            writer.Key("returningToCharge"); writer.Bool(entity.battery.returningToCharge);
            writer.Key("hasSuspendedOrder"); writer.Bool(entity.battery.hasSuspendedOrder);
            writer.Key("suspendedOrder");
            writer.Uint(static_cast<unsigned>(entity.battery.suspendedOrder));
            writer.Key("suspendedTarget"); writer.Uint64(entity.battery.suspendedTarget);
            writeVector(writer, "suspendedDestination", entity.battery.suspendedDestination);
            writer.Key("suspendedHasDestination");
            writer.Bool(entity.battery.suspendedHasDestination);
            writer.Key("chargerTarget"); writer.Uint64(entity.battery.chargerTarget);
            writer.EndObject();
        }
        if (entity.combat) {
            writer.Key("combat");
            writer.StartObject();
            writer.Key("cooldownRemaining");
            writer.Double(entity.combat.cooldownRemaining);
            writer.EndObject();
        }
        if (entity.resource) {
            writer.Key("resource");
            writer.StartObject();
            writer.Key("type");
            writer.String(entity.resource.type.c_str());
            writer.Key("remaining");
            writer.Double(entity.resource.remaining);
            writer.EndObject();
        }
        if (entity.processor) {
            writer.Key("processor");
            writer.StartObject();
            writer.Key("bufferedInputs");
            writer.StartObject();
            for (const auto& [id, amount] : entity.processor.bufferedInputs) {
                writer.Key(id.c_str());
                writer.Double(amount);
            }
            writer.EndObject();
            writer.Key("waitingForPower"); writer.Bool(entity.processor.waitingForPower);
            writer.Key("lastConversionTick"); writer.Uint64(entity.processor.lastConversionTick);
            writer.Key("activityTicksRemaining"); writer.Uint(entity.processor.activityTicksRemaining);
            writer.Key("state"); writer.Uint(static_cast<unsigned>(entity.processor.state));
            writer.EndObject();
        }
        if (entity.power) {
            writer.Key("power");
            writer.StartObject();
            writer.Key("generation"); writer.Double(entity.power.generation);
            writer.Key("demand"); writer.Double(entity.power.demand);
            writer.Key("supplied"); writer.Double(entity.power.supplied);
            writer.Key("priority"); writer.Int(entity.power.priority);
            writer.Key("state"); writer.Uint(static_cast<unsigned>(entity.power.state));
            writer.EndObject();
        }
        if (entity.construction) {
            writer.Key("construction");
            writer.StartObject();
            writer.Key("recipe"); writer.String(entity.construction.recipeId.c_str());
            writer.Key("powerRequired"); writer.Double(entity.construction.powerRequired);
            writer.Key("powerProgress"); writer.Double(entity.construction.powerProgress);
            writer.Key("state"); writer.Uint(static_cast<unsigned>(entity.construction.state));
            writer.Key("placementValid"); writer.Bool(entity.construction.placementValid);
            writer.EndObject();
        }
        if (entity.production) {
            writer.Key("production");
            writer.StartObject();
            writer.Key("speedMultiplier");
            writer.Double(entity.production.productionSpeedMultiplier);
            writer.Key("speedUpgrades");
            writer.Uint(entity.production.productionSpeedUpgrades);
            writer.Key("queue");
            writer.StartArray();
            for (const ProductionOrder& order : entity.production.queue) {
                writer.StartObject();
                writer.Key("kind");
                writer.Uint(static_cast<unsigned>(order.kind));
                writer.Key("recipe");
                writer.String(order.recipeId.c_str());
                writer.Key("product");
                writer.String(order.productId.c_str());
                writer.Key("upgrade");
                writer.String(order.upgradeId.c_str());
                writer.Key("icon");
                writer.String(order.iconId.c_str());
                writer.Key("amount");
                writer.Uint(order.amount);
                writer.Key("durationTicks");
                writer.Uint(order.durationTicks);
                writer.Key("remainingTicks");
                writer.Uint(order.remainingTicks);
                writer.Key("reservedCosts");
                writer.StartObject();
                for (const auto& [id, amount] : order.reservedCosts) {
                    writer.Key(id.c_str());
                    writer.Double(amount);
                }
                writer.EndObject();
                writer.EndObject();
            }
            writer.EndArray();
            writer.EndObject();
        }
        if (entity.buildingUpgrades) {
            writer.Key("buildingUpgrades");
            writer.StartObject();
            writer.Key("level");
            writer.Uint(entity.buildingUpgrades.level);
            writer.EndObject();
        }
        if (entity.upgrades) {
            writer.Key("upgrades");
            writer.StartObject();
            writer.Key("levels");
            writer.StartObject();
            for (const auto& [id, level] : entity.upgrades.levels) {
                writer.Key(id.c_str());
                writer.Uint(level);
            }
            writer.EndObject();
            writer.EndObject();
        }
        writer.EndObject();
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
}

SaveData SaveGame::read(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Could not open save file: " + path.string());
    }
    rapidjson::IStreamWrapper input{stream};
    rapidjson::Document document;
    document.ParseStream(input);
    if (document.HasParseError() || !document.IsObject() || !document.HasMember("formatVersion") ||
        !document["formatVersion"].IsUint() || document["formatVersion"].GetUint() != 7 ||
        !document.HasMember("terrainSeed") || !document["terrainSeed"].IsUint() ||
        !document.HasMember("mapChunksPerSide") || !document["mapChunksPerSide"].IsUint() ||
        !document.HasMember("players") || !document["players"].IsArray() ||
        !document.HasMember("entities") || !document["entities"].IsArray()) {
        throw std::runtime_error("Invalid or unsupported save file: " + path.string());
    }

    SaveData result;
    result.terrainSeed = document["terrainSeed"].GetUint();
    result.mapChunksPerSide = document["mapChunksPerSide"].GetUint();
    if (result.mapChunksPerSide < 10 || result.mapChunksPerSide > 20)
        throw std::runtime_error("Invalid save map chunk count");
    if (!document.HasMember("foundations") || !document["foundations"].IsArray())
        throw std::runtime_error("Invalid save foundations");
    for (const auto& item : document["foundations"].GetArray()) {
        if (!item.IsObject() || !item.HasMember("innerRadius") ||
            !item["innerRadius"].IsNumber() || !item.HasMember("outerRadius") ||
            !item["outerRadius"].IsNumber())
            throw std::runtime_error("Invalid terrain foundation");
        TerrainFoundation foundation;
        foundation.center = readVector(item, "center");
        foundation.innerRadius = item["innerRadius"].GetFloat();
        foundation.outerRadius = item["outerRadius"].GetFloat();
        if (foundation.innerRadius <= 0.0F || foundation.outerRadius < foundation.innerRadius)
            throw std::runtime_error("Invalid terrain foundation radii");
        result.foundations.push_back(foundation);
    }
    {
        for (const auto& player : document["players"].GetArray())
            if (player.IsObject() && player.HasMember("id") && player["id"].IsUint64() &&
                player.HasMember("country") && player["country"].IsString()) {
                const std::string specialization =
                    player.HasMember("specialization") && player["specialization"].IsString()
                        ? player["specialization"].GetString()
                        : "unassigned";
                const auto id = player["id"].GetUint64();
                if (id == 1) {
                    result.playerOneCountry = player["country"].GetString();
                    result.playerOneSpecialization = specialization;
                } else if (id == 2) {
                    result.playerTwoCountry = player["country"].GetString();
                    result.playerTwoSpecialization = specialization;
                }
                if (id >= 1 && id <= 2) {
                    const std::size_t index = static_cast<std::size_t>(id - 1);
                    if (!player.HasMember("resources") || !player["resources"].IsObject())
                        throw std::runtime_error("Invalid player resources");
                    if (player.HasMember("resources") && player["resources"].IsObject())
                        for (auto resource = player["resources"].MemberBegin();
                             resource != player["resources"].MemberEnd();
                             ++resource) {
                            if (!resource->value.IsNumber() || resource->value.GetFloat() < 0.0F)
                                throw std::runtime_error("Invalid player resource amount");
                            result.resources[index][resource->name.GetString()] =
                                resource->value.GetFloat();
                        }
                    if (player.HasMember("discovered") && player["discovered"].IsString()) {
                        const std::string cells = player["discovered"].GetString();
                        result.discovered[index].reserve(cells.size());
                        for (char cell : cells)
                            result.discovered[index].push_back(cell == '1' ? 255 : 0);
                    }
                }
                if (id >= 1 && id <= 2 && player.HasMember("intelligence") &&
                    player["intelligence"].IsArray()) {
                    auto& records = result.intelligence[static_cast<std::size_t>(id - 1)];
                    for (const auto& item : player["intelligence"].GetArray()) {
                        if (!item.IsObject() || !item.HasMember("id") || !item["id"].IsUint64() ||
                            !item.HasMember("model") || !item["model"].IsString() ||
                            !item.HasMember("building") || !item["building"].IsBool())
                            throw std::runtime_error("Invalid intelligence record");
                        records.push_back({item["id"].GetUint64(),
                                           item["model"].GetString(),
                                           readVector(item, "position"),
                                           readVector(item, "rotation"),
                                           readVector(item, "scale"),
                                           item["building"].GetBool()});
                    }
                }
            }
    }
    const GameplayCatalogue gameplayCatalogue;
    for (const auto& value : document["entities"].GetArray()) {
        if (!value.IsObject() || !value.HasMember("id") || !value["id"].IsUint64() ||
            !value.HasMember("name") || !value["name"].IsString() ||
            !value.HasMember("archetype") || !value["archetype"].IsString() ||
            !value.HasMember("presentation") || !value["presentation"].IsString()) {
            throw std::runtime_error("Invalid entity in save file");
        }
        Entity entity;
        entity.id = value["id"].GetUint64();
        entity.name = value["name"].GetString();
        entity.archetype = EntityArchetypeId{value["archetype"].GetString()};
        entity.presentation = PresentationId{value["presentation"].GetString()};
        entity.transform.position = readVector(value, "position");
        entity.transform.rotationDegrees = readVector(value, "rotation");
        entity.transform.scale = readVector(value, "scale");
        {
            if (!value.HasMember("owner") || !value["owner"].IsUint64() ||
                !value.HasMember("components") || !value["components"].IsObject())
                throw std::runtime_error("Invalid component entity");
            entity.authority.owner = value["owner"].GetUint64();
            gameplayCatalogue.initializeEntity(entity);
            const auto& components = value["components"];
            if (components.HasMember("health")) {
                const auto& item = components["health"];
                if (!item.IsObject() || !item.HasMember("current") || !item["current"].IsNumber() ||
                    !item.HasMember("maximum") || !item["maximum"].IsNumber())
                    throw std::runtime_error("Invalid health component");
                entity.health.emplace();
                entity.health.current = item["current"].GetFloat();
                entity.health.maximum = item["maximum"].GetFloat();
            }
            if (components.HasMember("unit")) {
                const auto& item = components["unit"];
                entity.unitControl.emplace();
                if (!item.IsObject() || !item.HasMember("directlyControllable") ||
                    !item["directlyControllable"].IsBool() || !item.HasMember("directInput") ||
                    !item["directInput"].IsArray() || item["directInput"].Size() != 2 ||
                    !item.HasMember("order") || !item["order"].IsUint() ||
                    !item.HasMember("orderTarget") || !item["orderTarget"].IsUint64())
                    throw std::runtime_error("Invalid unit component");
                entity.unitControl.directlyControllable = item["directlyControllable"].GetBool();
                entity.authority.directController =
                    item.HasMember("directController") && item["directController"].IsUint64()
                        ? item["directController"].GetUint64()
                        : 0;
                entity.unitControl.directInput = {item["directInput"][0].GetFloat(),
                                                  item["directInput"][1].GetFloat()};
                entity.unitControl.running = item.HasMember("running") &&
                                             item["running"].IsBool() && item["running"].GetBool();
                entity.unitControl.strategicDestination = readVector(item, "destination");
                entity.unitControl.hasStrategicDestination = item.HasMember("hasDestination") &&
                                                             item["hasDestination"].IsBool() &&
                                                             item["hasDestination"].GetBool();
                entity.unitControl.order = static_cast<UnitOrderKind>(item["order"].GetUint());
                entity.unitControl.orderTarget = item["orderTarget"].GetUint64();
                if (item["order"].GetUint() > static_cast<unsigned>(UnitOrderKind::stranded))
                    throw std::runtime_error("Invalid unit order");
            }
            if (components.HasMember("gatherer")) {
                const auto& item = components["gatherer"];
                entity.gatherer.emplace();
                if (!item.IsObject() || !item.HasMember("carriedResource") ||
                    !item["carriedResource"].IsString() || !item.HasMember("carriedAmount") ||
                    !item["carriedAmount"].IsNumber() || !item.HasMember("sourceTarget") ||
                    !item["sourceTarget"].IsUint64() || !item.HasMember("deliveryTarget") ||
                    !item["deliveryTarget"].IsUint64() ||
                    !item.HasMember("preferredProcessor") ||
                    !item["preferredProcessor"].IsUint64() ||
                    !item.HasMember("preferredOutput") || !item["preferredOutput"].IsString() ||
                    !item.HasMember("repeatGathering") || !item["repeatGathering"].IsBool() ||
                    !item.HasMember("waitingForProcessor") ||
                    !item["waitingForProcessor"].IsBool())
                    throw std::runtime_error("Invalid gatherer component");
                entity.gatherer.carriedResource = item["carriedResource"].GetString();
                entity.gatherer.carriedAmount = item["carriedAmount"].GetFloat();
                entity.gatherer.sourceTarget = item["sourceTarget"].GetUint64();
                entity.gatherer.deliveryTarget = item["deliveryTarget"].GetUint64();
                entity.gatherer.preferredProcessor = item["preferredProcessor"].GetUint64();
                entity.gatherer.preferredOutput = item["preferredOutput"].GetString();
                entity.gatherer.repeatGathering = item["repeatGathering"].GetBool();
                entity.gatherer.waitingForProcessor = item["waitingForProcessor"].GetBool();
            }
            if (components.HasMember("flight")) {
                const auto& item = components["flight"];
                if (!item.IsObject() || !item.HasMember("altitude") ||
                    !item["altitude"].IsNumber())
                    throw std::runtime_error("Invalid flight component");
                entity.flight.emplace();
                entity.flight.altitude = item["altitude"].GetFloat();
                if (entity.flight.altitude < entity.flight.minimumAltitude ||
                    entity.flight.altitude > entity.flight.maximumAltitude)
                    throw std::runtime_error("Invalid flight altitude");
                entity.transform.position.y = entity.flight.altitude;
            }
            if (components.HasMember("battery")) {
                const auto& item = components["battery"];
                if (!item.IsObject() || !item.HasMember("charge") || !item["charge"].IsNumber() ||
                    !item.HasMember("returningToCharge") || !item["returningToCharge"].IsBool() ||
                    !item.HasMember("hasSuspendedOrder") || !item["hasSuspendedOrder"].IsBool() ||
                    !item.HasMember("suspendedOrder") || !item["suspendedOrder"].IsUint() ||
                    !item.HasMember("suspendedTarget") || !item["suspendedTarget"].IsUint64() ||
                    !item.HasMember("suspendedHasDestination") ||
                    !item["suspendedHasDestination"].IsBool() ||
                    !item.HasMember("chargerTarget") || !item["chargerTarget"].IsUint64())
                    throw std::runtime_error("Invalid battery component");
                entity.battery.emplace();
                entity.battery.charge = item["charge"].GetFloat();
                entity.battery.returningToCharge = item["returningToCharge"].GetBool();
                entity.battery.hasSuspendedOrder = item["hasSuspendedOrder"].GetBool();
                entity.battery.suspendedOrder =
                    static_cast<UnitOrderKind>(item["suspendedOrder"].GetUint());
                entity.battery.suspendedTarget = item["suspendedTarget"].GetUint64();
                entity.battery.suspendedDestination = readVector(item, "suspendedDestination");
                entity.battery.suspendedHasDestination =
                    item["suspendedHasDestination"].GetBool();
                entity.battery.chargerTarget = item["chargerTarget"].GetUint64();
                if (entity.battery.charge < 0.0F ||
                    entity.battery.charge > entity.battery.capacity ||
                    item["suspendedOrder"].GetUint() >
                        static_cast<unsigned>(UnitOrderKind::stranded))
                    throw std::runtime_error("Invalid battery state");
            }
            if (components.HasMember("combat")) {
                const auto& item = components["combat"];
                if (!item.IsObject() || !item.HasMember("cooldownRemaining") ||
                    !item["cooldownRemaining"].IsNumber())
                    throw std::runtime_error("Invalid combat component");
                entity.combat.emplace();
                entity.combat.cooldownRemaining = item["cooldownRemaining"].GetFloat();
            }
            if (components.HasMember("resource")) {
                const auto& item = components["resource"];
                entity.resource.emplace();
                if (!item.IsObject() || !item.HasMember("type") || !item["type"].IsString() ||
                    !item.HasMember("remaining") || !item["remaining"].IsNumber())
                    throw std::runtime_error("Invalid resource component");
                entity.resource.type = item["type"].GetString();
                entity.resource.remaining = item["remaining"].GetFloat();
            }
            if (components.HasMember("processor")) {
                const auto& item = components["processor"];
                if (!item.IsObject() || !item.HasMember("bufferedInputs") ||
                    !item["bufferedInputs"].IsObject() ||
                    !item.HasMember("waitingForPower") || !item["waitingForPower"].IsBool() ||
                    !item.HasMember("lastConversionTick") || !item["lastConversionTick"].IsUint64() ||
                    !item.HasMember("activityTicksRemaining") || !item["activityTicksRemaining"].IsUint() ||
                    !item.HasMember("state") || !item["state"].IsUint() ||
                    item["state"].GetUint() > static_cast<unsigned>(ProcessorOperationalState::offline))
                    throw std::runtime_error("Invalid processor component");
                entity.processor.emplace();
                entity.processor.waitingForPower = item["waitingForPower"].GetBool();
                entity.processor.lastConversionTick = item["lastConversionTick"].GetUint64();
                entity.processor.activityTicksRemaining = item["activityTicksRemaining"].GetUint();
                entity.processor.state = static_cast<ProcessorOperationalState>(item["state"].GetUint());
                for (auto buffered = item["bufferedInputs"].MemberBegin();
                     buffered != item["bufferedInputs"].MemberEnd(); ++buffered) {
                    if (!buffered->value.IsNumber() || buffered->value.GetFloat() < 0.0F)
                        throw std::runtime_error("Invalid processor buffered input");
                    entity.processor.bufferedInputs[buffered->name.GetString()] =
                        buffered->value.GetFloat();
                }
            }
            if (components.HasMember("power")) {
                const auto& item = components["power"];
                if (!item.IsObject() || !item.HasMember("generation") ||
                    !item["generation"].IsNumber() || !item.HasMember("demand") ||
                    !item["demand"].IsNumber() || !item.HasMember("supplied") ||
                    !item["supplied"].IsNumber() || !item.HasMember("priority") ||
                    !item["priority"].IsInt() || !item.HasMember("state") ||
                    !item["state"].IsUint() ||
                    item["state"].GetUint() > static_cast<unsigned>(PowerOperationalState::offline))
                    throw std::runtime_error("Invalid power component");
                entity.power.emplace();
                entity.power.generation = item["generation"].GetFloat();
                entity.power.demand = item["demand"].GetFloat();
                entity.power.supplied = item["supplied"].GetFloat();
                entity.power.priority = item["priority"].GetInt();
                entity.power.state = static_cast<PowerOperationalState>(item["state"].GetUint());
                if (entity.power.generation < 0.0F || entity.power.demand < 0.0F ||
                    entity.power.supplied < 0.0F || entity.power.supplied > entity.power.demand)
                    throw std::runtime_error("Invalid saved power allocation");
            }
            if (components.HasMember("construction")) {
                const auto& item = components["construction"];
                if (!item.IsObject() || !item.HasMember("recipe") || !item["recipe"].IsString() ||
                    !item.HasMember("powerRequired") || !item["powerRequired"].IsNumber() ||
                    !item.HasMember("powerProgress") || !item["powerProgress"].IsNumber() ||
                    !item.HasMember("state") || !item["state"].IsUint() ||
                    !item.HasMember("placementValid") || !item["placementValid"].IsBool() ||
                    item["state"].GetUint() > static_cast<unsigned>(BuildingLifecycleState::destroyed))
                    throw std::runtime_error("Invalid construction component");
                entity.construction.emplace();
                entity.construction.recipeId = item["recipe"].GetString();
                entity.construction.powerRequired = item["powerRequired"].GetFloat();
                entity.construction.powerProgress = item["powerProgress"].GetFloat();
                entity.construction.state = static_cast<BuildingLifecycleState>(item["state"].GetUint());
                entity.construction.placementValid = item["placementValid"].GetBool();
                if (entity.construction.powerRequired < 0.0F || entity.construction.powerProgress < 0.0F ||
                    entity.construction.powerProgress > entity.construction.powerRequired)
                    throw std::runtime_error("Invalid construction progress");
            }
            if (components.HasMember("production")) {
                const auto& item = components["production"];
                entity.production.emplace();
                if (!item.IsObject() || !item.HasMember("speedMultiplier") ||
                    !item["speedMultiplier"].IsNumber() || !item.HasMember("speedUpgrades") ||
                    !item["speedUpgrades"].IsUint() || !item.HasMember("queue") ||
                    !item["queue"].IsArray())
                    throw std::runtime_error("Invalid production component");
                entity.production.productionSpeedMultiplier = item["speedMultiplier"].GetFloat();
                entity.production.productionSpeedUpgrades = item["speedUpgrades"].GetUint();
                for (const auto& order : item["queue"].GetArray()) {
                    if (!order.IsObject() || !order.HasMember("kind") || !order["kind"].IsUint())
                        throw std::runtime_error("Invalid production order");
                    ProductionOrder parsed;
                    parsed.kind = static_cast<ProductionKind>(order["kind"].GetUint());
                    {
                        if (!order.HasMember("recipe") || !order["recipe"].IsString() ||
                            !order.HasMember("product") || !order["product"].IsString() ||
                            !order.HasMember("upgrade") || !order["upgrade"].IsString() ||
                            !order.HasMember("icon") || !order["icon"].IsString() ||
                            !order.HasMember("amount") || !order["amount"].IsUint() ||
                            !order.HasMember("durationTicks") || !order["durationTicks"].IsUint() ||
                            !order.HasMember("remainingTicks") ||
                            !order["remainingTicks"].IsUint() ||
                            !order.HasMember("reservedCosts") ||
                            !order["reservedCosts"].IsObject())
                            throw std::runtime_error("Invalid recipe production order");
                        parsed.recipeId = order["recipe"].GetString();
                        parsed.productId = order["product"].GetString();
                        parsed.upgradeId = order["upgrade"].GetString();
                        parsed.iconId = order["icon"].GetString();
                        parsed.amount = order["amount"].GetUint();
                        parsed.durationTicks = order["durationTicks"].GetUint();
                        parsed.remainingTicks = order["remainingTicks"].GetUint();
                        if (parsed.amount == 0 || parsed.durationTicks == 0 ||
                            parsed.remainingTicks > parsed.durationTicks)
                            throw std::runtime_error("Invalid recipe production tick state");
                        for (auto cost = order["reservedCosts"].MemberBegin();
                             cost != order["reservedCosts"].MemberEnd();
                             ++cost) {
                            if (!cost->value.IsNumber() || cost->value.GetFloat() < 0.0F)
                                throw std::runtime_error("Invalid reserved recipe cost");
                            parsed.reservedCosts[cost->name.GetString()] = cost->value.GetFloat();
                        }
                        if (parsed.kind == ProductionKind::trainCharacter) {
                            const RecipeDefinition* recipe =
                                gameplayCatalogue.recipe(RecipeId{parsed.recipeId});
                            if (!recipe || recipe->product.id != parsed.productId ||
                                recipe->product.amount != parsed.amount)
                                throw std::runtime_error(
                                    "Saved production order does not match its recipe");
                        }
                    }
                    entity.production.queue.push_back(std::move(parsed));
                }
            }
            if (components.HasMember("buildingUpgrades")) {
                const auto& item = components["buildingUpgrades"];
                entity.buildingUpgrades.emplace();
                if (!item.IsObject() || !item.HasMember("level") || !item["level"].IsUint())
                    throw std::runtime_error("Invalid building upgrade component");
                entity.buildingUpgrades.level = item["level"].GetUint();
            }
            if (components.HasMember("upgrades")) {
                entity.upgrades.emplace();
                const auto& item = components["upgrades"];
                if (item.IsObject() && item.HasMember("levels") && item["levels"].IsObject())
                    for (auto upgrade = item["levels"].MemberBegin();
                         upgrade != item["levels"].MemberEnd();
                         ++upgrade)
                        if (upgrade->value.IsUint())
                            entity.upgrades.levels[upgrade->name.GetString()] =
                                upgrade->value.GetUint();
            }
            result.entities.push_back(std::move(entity));
        }
    }
    return result;
}

} // namespace strategy
