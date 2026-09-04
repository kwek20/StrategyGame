#include "persistence/SaveGame.hpp"

#include "world/World.hpp"
#include "players/PlayerRegistry.hpp"
#include "gameplay/GameplayCatalogue.hpp"

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace strategy {
namespace {

template<class Writer>
void writeVector(Writer& writer, const char* name, const glm::vec3& value) {
    writer.Key(name);
    writer.StartArray();
    writer.Double(value.x);
    writer.Double(value.y);
    writer.Double(value.z);
    writer.EndArray();
}

glm::vec3 readVector(const rapidjson::Value& object, const char* name) {
    if (!object.HasMember(name) || !object[name].IsArray()
        || object[name].Size() != 3 || !object[name][0].IsNumber()
        || !object[name][1].IsNumber() || !object[name][2].IsNumber()) {
        throw std::runtime_error(std::string("Invalid entity vector: ") + name);
    }
    return {object[name][0].GetFloat(), object[name][1].GetFloat(),
            object[name][2].GetFloat()};
}

} // namespace

void SaveGame::write(const std::filesystem::path& path, std::uint32_t terrainSeed,
                     const World& world,const PlayerRegistry* players) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("Could not write save file: " + path.string());
    }
    rapidjson::OStreamWrapper output{stream};
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer{output};
    writer.StartObject();
    writer.Key("formatVersion"); writer.Uint(12);
    writer.Key("terrainSeed"); writer.Uint(terrainSeed);
    writer.Key("players"); writer.StartArray();
    if(players) for(const Player& player:players->players()) { writer.StartObject();writer.Key("id");writer.Uint64(player.id);writer.Key("country");writer.String(player.countryId.c_str());writer.Key("specialization");writer.String(player.specializationId.c_str());writer.Key("wood");writer.Double(player.wood);writer.Key("stone");writer.Double(player.stone);writer.Key("gold");writer.Double(player.gold);std::string explored;explored.reserve(player.discovered.size());for(auto cell:player.discovered)explored.push_back(cell?'1':'0');writer.Key("discovered");writer.String(explored.c_str(),static_cast<rapidjson::SizeType>(explored.size()));writer.Key("intelligence");writer.StartArray();for(const LastKnownEntity& known:player.intelligence){writer.StartObject();writer.Key("id");writer.Uint64(known.id);writer.Key("model");writer.String(known.modelKey.c_str());writeVector(writer,"position",known.position);writeVector(writer,"rotation",known.rotationDegrees);writeVector(writer,"scale",known.scale);writer.Key("building");writer.Bool(known.building);writer.EndObject();}writer.EndArray();writer.EndObject(); }
    writer.EndArray();
    writer.Key("entities"); writer.StartArray();
    for (const Entity& entity : world.entities()) {
        writer.StartObject();
        writer.Key("id"); writer.Uint64(entity.id);
        writer.Key("name"); writer.String(entity.name.c_str());
        writer.Key("model"); writer.String(entity.modelKey.c_str());
        writer.Key("owner"); writer.Uint64(entity.authority.owner);
        writer.Key("directController"); writer.Uint64(entity.authority.directController);
        writeVector(writer, "position", entity.transform.position);
        writeVector(writer, "rotation", entity.transform.rotationDegrees);
        writeVector(writer, "scale", entity.transform.scale);
        writer.Key("components");writer.StartObject();
        if(entity.health){writer.Key("health");writer.StartObject();writer.Key("current");writer.Double(entity.health.current);writer.Key("maximum");writer.Double(entity.health.maximum);writer.EndObject();}
        if(entity.vision){writer.Key("vision");writer.StartObject();writer.EndObject();}
        if(entity.unitControl){writer.Key("unit");writer.StartObject();writer.Key("directlyControllable");writer.Bool(entity.unitControl.directlyControllable);writer.Key("directController");writer.Uint64(entity.authority.directController);writer.Key("directInput");writer.StartArray();writer.Double(entity.unitControl.directInput.x);writer.Double(entity.unitControl.directInput.y);writer.EndArray();writer.Key("running");writer.Bool(entity.unitControl.running);writeVector(writer,"destination",entity.unitControl.strategicDestination);writer.Key("hasDestination");writer.Bool(entity.unitControl.hasStrategicDestination);writer.Key("order");writer.Uint(static_cast<unsigned>(entity.unitControl.order));writer.Key("orderTarget");writer.Uint64(entity.unitControl.orderTarget);writer.EndObject();}
        if(entity.gatherer){writer.Key("gatherer");writer.StartObject();writer.Key("carriedKind");writer.Uint(static_cast<unsigned>(entity.unitControl.carriedKind));writer.Key("carriedAmount");writer.Double(entity.unitControl.carriedAmount);writer.EndObject();}
        if(entity.combat){writer.Key("combat");writer.StartObject();writer.Key("cooldownRemaining");writer.Double(entity.combat.cooldownRemaining);writer.EndObject();}
        if(entity.resource){writer.Key("resource");writer.StartObject();writer.Key("kind");writer.Uint(static_cast<unsigned>(entity.resource.kind));writer.Key("remaining");writer.Double(entity.resource.remaining);writer.EndObject();}
        if(entity.production){writer.Key("production");writer.StartObject();writer.Key("speedMultiplier");writer.Double(entity.production.productionSpeedMultiplier);writer.Key("speedUpgrades");writer.Uint(entity.production.productionSpeedUpgrades);writer.Key("queue");writer.StartArray();for(const ProductionOrder& order:entity.production.queue){writer.StartObject();writer.Key("kind");writer.Uint(static_cast<unsigned>(order.kind));writer.Key("duration");writer.Double(order.durationSeconds);writer.Key("remaining");writer.Double(order.remainingSeconds);writer.EndObject();}writer.EndArray();writer.EndObject();}
        if(entity.buildingUpgrades){writer.Key("buildingUpgrades");writer.StartObject();writer.Key("level");writer.Uint(entity.production.level);writer.EndObject();}
        if(entity.upgrades){writer.Key("upgrades");writer.StartObject();writer.Key("levels");writer.StartObject();for(const auto& [id,level]:entity.upgrades.levels){writer.Key(id.c_str());writer.Uint(level);}writer.EndObject();writer.EndObject();}
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
    if (document.HasParseError() || !document.IsObject()
        || !document.HasMember("formatVersion") || !document["formatVersion"].IsUint()
        || (document["formatVersion"].GetUint() != 1
            && document["formatVersion"].GetUint() != 2
            && document["formatVersion"].GetUint() != 3
            && document["formatVersion"].GetUint() != 4
            && document["formatVersion"].GetUint() != 5
            && document["formatVersion"].GetUint() != 6
            && document["formatVersion"].GetUint() != 7
            && document["formatVersion"].GetUint() != 8
            && document["formatVersion"].GetUint() != 9
            && document["formatVersion"].GetUint() != 10
            && document["formatVersion"].GetUint() != 11
            && document["formatVersion"].GetUint() != 12)
        || !document.HasMember("terrainSeed") || !document["terrainSeed"].IsUint()
        || !document.HasMember("entities") || !document["entities"].IsArray()) {
        throw std::runtime_error("Invalid or unsupported save file: " + path.string());
    }

    SaveData result;
    const unsigned int version = document["formatVersion"].GetUint();
    result.terrainSeed = document["terrainSeed"].GetUint();
    if(version>=5&&document.HasMember("players")&&document["players"].IsArray()) {
        for(const auto& player:document["players"].GetArray()) if(player.IsObject()&&player.HasMember("id")&&player["id"].IsUint64()&&player.HasMember("country")&&player["country"].IsString()) {
            const std::string specialization=player.HasMember("specialization")&&player["specialization"].IsString()?player["specialization"].GetString():"unassigned";
            const auto id=player["id"].GetUint64();
            if(id==1){result.playerOneCountry=player["country"].GetString();result.playerOneSpecialization=specialization;}
            else if(id==2){result.playerTwoCountry=player["country"].GetString();result.playerTwoSpecialization=specialization;}
            if(version>=8&&id>=1&&id<=2) {const std::size_t index=static_cast<std::size_t>(id-1);if(player.HasMember("wood")&&player["wood"].IsNumber())result.wood[index]=player["wood"].GetFloat();if(player.HasMember("stone")&&player["stone"].IsNumber())result.stone[index]=player["stone"].GetFloat();if(player.HasMember("gold")&&player["gold"].IsNumber())result.gold[index]=player["gold"].GetFloat();if(player.HasMember("discovered")&&player["discovered"].IsString()){const std::string cells=player["discovered"].GetString();result.discovered[index].reserve(cells.size());for(char cell:cells)result.discovered[index].push_back(cell=='1'?255:0);}}
            if(version>=10&&id>=1&&id<=2&&player.HasMember("intelligence")&&player["intelligence"].IsArray()){auto& records=result.intelligence[static_cast<std::size_t>(id-1)];for(const auto& item:player["intelligence"].GetArray()){if(!item.IsObject()||!item.HasMember("id")||!item["id"].IsUint64()||!item.HasMember("model")||!item["model"].IsString()||!item.HasMember("building")||!item["building"].IsBool())throw std::runtime_error("Invalid intelligence record");records.push_back({item["id"].GetUint64(),item["model"].GetString(),readVector(item,"position"),readVector(item,"rotation"),readVector(item,"scale"),item["building"].GetBool()});}}
        }
    }
    const GameplayCatalogue gameplayCatalogue;
    for (const auto& value : document["entities"].GetArray()) {
        if (!value.IsObject() || !value.HasMember("id") || !value["id"].IsUint64()
            || !value.HasMember("name") || !value["name"].IsString()
            || !value.HasMember("model") || !value["model"].IsString()) {
            throw std::runtime_error("Invalid entity in save file");
        }
        Entity entity;
        entity.id = value["id"].GetUint64();
        entity.name = value["name"].GetString();
        entity.modelKey = value["model"].GetString();
        if(version<12)gameplayCatalogue.initializeEntity(entity);
        entity.transform.position = readVector(value, "position");
        entity.transform.rotationDegrees = readVector(value, "rotation");
        entity.transform.scale = readVector(value, "scale");
        if(version==12) {
            if(!value.HasMember("owner")||!value["owner"].IsUint64()||!value.HasMember("components")||!value["components"].IsObject())throw std::runtime_error("Invalid component entity");
            entity.authority.owner=value["owner"].GetUint64();gameplayCatalogue.initializeEntity(entity);const auto& components=value["components"];
            if(components.HasMember("health")){const auto& item=components["health"];if(!item.IsObject()||!item.HasMember("current")||!item["current"].IsNumber()||!item.HasMember("maximum")||!item["maximum"].IsNumber())throw std::runtime_error("Invalid health component");entity.health.emplace();entity.health.current=item["current"].GetFloat();entity.health.maximum=item["maximum"].GetFloat();}
            if(components.HasMember("unit")){const auto& item=components["unit"];entity.unitControl.emplace();if(!item.IsObject()||!item.HasMember("directlyControllable")||!item["directlyControllable"].IsBool()||!item.HasMember("directInput")||!item["directInput"].IsArray()||item["directInput"].Size()!=2||!item.HasMember("order")||!item["order"].IsUint()||!item.HasMember("orderTarget")||!item["orderTarget"].IsUint64())throw std::runtime_error("Invalid unit component");entity.unitControl.directlyControllable=item["directlyControllable"].GetBool();entity.authority.directController=item.HasMember("directController")&&item["directController"].IsUint64()?item["directController"].GetUint64():0;entity.unitControl.directInput={item["directInput"][0].GetFloat(),item["directInput"][1].GetFloat()};entity.unitControl.running=item.HasMember("running")&&item["running"].IsBool()&&item["running"].GetBool();entity.unitControl.strategicDestination=readVector(item,"destination");entity.unitControl.hasStrategicDestination=item.HasMember("hasDestination")&&item["hasDestination"].IsBool()&&item["hasDestination"].GetBool();entity.unitControl.order=static_cast<UnitOrderKind>(item["order"].GetUint());entity.unitControl.orderTarget=item["orderTarget"].GetUint64();}
            if(components.HasMember("gatherer")){const auto& item=components["gatherer"];entity.gatherer.emplace();if(!item.IsObject()||!item.HasMember("carriedKind")||!item["carriedKind"].IsUint()||!item.HasMember("carriedAmount")||!item["carriedAmount"].IsNumber())throw std::runtime_error("Invalid gatherer component");entity.unitControl.carriedKind=static_cast<ResourceKind>(item["carriedKind"].GetUint());entity.unitControl.carriedAmount=item["carriedAmount"].GetFloat();}
            if(components.HasMember("resource")){const auto& item=components["resource"];entity.resource.emplace();if(!item.IsObject()||!item.HasMember("kind")||!item["kind"].IsUint()||!item.HasMember("remaining")||!item["remaining"].IsNumber())throw std::runtime_error("Invalid resource component");entity.resource.kind=static_cast<ResourceKind>(item["kind"].GetUint());entity.resource.remaining=item["remaining"].GetFloat();}
            if(components.HasMember("production")){const auto& item=components["production"];entity.production.emplace();if(!item.IsObject()||!item.HasMember("speedMultiplier")||!item["speedMultiplier"].IsNumber()||!item.HasMember("speedUpgrades")||!item["speedUpgrades"].IsUint()||!item.HasMember("queue")||!item["queue"].IsArray())throw std::runtime_error("Invalid production component");entity.production.productionSpeedMultiplier=item["speedMultiplier"].GetFloat();entity.production.productionSpeedUpgrades=item["speedUpgrades"].GetUint();for(const auto& order:item["queue"].GetArray()){if(!order.IsObject()||!order.HasMember("kind")||!order["kind"].IsUint()||!order.HasMember("duration")||!order["duration"].IsNumber()||!order.HasMember("remaining")||!order["remaining"].IsNumber())throw std::runtime_error("Invalid production order");entity.production.queue.push_back({static_cast<ProductionKind>(order["kind"].GetUint()),order["duration"].GetFloat(),order["remaining"].GetFloat()});}}
            if(components.HasMember("buildingUpgrades")){const auto& item=components["buildingUpgrades"];entity.buildingUpgrades.emplace();if(!item.IsObject()||!item.HasMember("level")||!item["level"].IsUint())throw std::runtime_error("Invalid building upgrade component");entity.buildingUpgrades.level=item["level"].GetUint();entity.production.level=entity.buildingUpgrades.level;}
            if(components.HasMember("upgrades")){entity.upgrades.emplace();const auto& item=components["upgrades"];if(item.IsObject()&&item.HasMember("levels")&&item["levels"].IsObject())for(auto upgrade=item["levels"].MemberBegin();upgrade!=item["levels"].MemberEnd();++upgrade)if(upgrade->value.IsUint())entity.upgrades.levels[upgrade->name.GetString()]=upgrade->value.GetUint();}
            result.entities.push_back(std::move(entity));continue;
        }
        if (version >= 2) {
            if (!value.HasMember("owner") || !value["owner"].IsUint64()
                || !value.HasMember("directController")
                || !value["directController"].IsUint64()
                || !value.HasMember("directlyControllable")
                || !value["directlyControllable"].IsBool()
                || !value.HasMember("directInput") || !value["directInput"].IsArray()
                || value["directInput"].Size() != 2
                || !value["directInput"][0].IsNumber()
                || !value["directInput"][1].IsNumber()
                || !value.HasMember("hasStrategicDestination")
                || !value["hasStrategicDestination"].IsBool()
                || !value.HasMember("movementSpeed")
                || !value["movementSpeed"].IsNumber()) {
                throw std::runtime_error("Invalid unit authority/control in save file");
            }
            entity.authority.owner = value["owner"].GetUint64();
            entity.authority.directController = value["directController"].GetUint64();
            entity.unitControl.directlyControllable =
                value["directlyControllable"].GetBool();
            entity.unitControl.directInput = {value["directInput"][0].GetFloat(),
                                              value["directInput"][1].GetFloat()};
            if (version >= 3) {
                if (!value.HasMember("running") || !value["running"].IsBool())
                    throw std::runtime_error("Invalid running state in save file");
                entity.unitControl.running = value["running"].GetBool();
            }
            entity.unitControl.strategicDestination =
                readVector(value, "strategicDestination");
            entity.unitControl.hasStrategicDestination =
                value["hasStrategicDestination"].GetBool();
                entity.unitControl.movementSpeed = value["movementSpeed"].GetFloat();
            if(version>=8) {
                if(!value.HasMember("sightRange")||!value["sightRange"].IsNumber())throw std::runtime_error("Invalid unit sight range");
                entity.unitControl.sightRange=value["sightRange"].GetFloat();
            }
            if(version>=9) {
                if(!value.HasMember("unitOrder")||!value["unitOrder"].IsUint()||value["unitOrder"].GetUint()>static_cast<unsigned>(UnitOrderKind::attack)||!value.HasMember("orderTarget")||!value["orderTarget"].IsUint64()||!value.HasMember("carriedKind")||!value["carriedKind"].IsUint()||value["carriedKind"].GetUint()>static_cast<unsigned>(ResourceKind::gold)||!value.HasMember("carriedAmount")||!value["carriedAmount"].IsNumber()||!value.HasMember("carryCapacity")||!value["carryCapacity"].IsNumber()||!value.HasMember("gatherPerSecond")||!value["gatherPerSecond"].IsNumber())throw std::runtime_error("Invalid unit order state");
                entity.unitControl.order=static_cast<UnitOrderKind>(value["unitOrder"].GetUint());entity.unitControl.orderTarget=value["orderTarget"].GetUint64();entity.unitControl.carriedKind=static_cast<ResourceKind>(value["carriedKind"].GetUint());entity.unitControl.carriedAmount=value["carriedAmount"].GetFloat();entity.unitControl.carryCapacity=value["carryCapacity"].GetFloat();entity.unitControl.gatherPerSecond=value["gatherPerSecond"].GetFloat();
            }
            if (version >= 4) {
                if (!value.HasMember("health") || !value["health"].IsNumber()
                    || !value.HasMember("maximumHealth") || !value["maximumHealth"].IsNumber())
                    throw std::runtime_error("Invalid health state in save file");
                entity.health.current=value["health"].GetFloat();
                entity.health.maximum=value["maximumHealth"].GetFloat();
                if(version>=8) {
                    if(!value.HasMember("resourceKind")||!value["resourceKind"].IsUint()||value["resourceKind"].GetUint()>static_cast<unsigned>(ResourceKind::gold)||!value.HasMember("resourceRemaining")||!value["resourceRemaining"].IsNumber())throw std::runtime_error("Invalid resource node state");
                    entity.resource.kind=static_cast<ResourceKind>(value["resourceKind"].GetUint());entity.resource.remaining=value["resourceRemaining"].GetFloat();
                } else if(entity.modelKey=="tree"||entity.modelKey=="stone"||entity.modelKey=="gold") {
                    entity.resource.kind=entity.modelKey=="tree"?ResourceKind::wood:entity.modelKey=="stone"?ResourceKind::stone:ResourceKind::gold;entity.resource.remaining=100.0F;
                }
            }
            if(version==6) {
                if(!value.HasMember("townHallLevel")||!value["townHallLevel"].IsUint()
                    ||!value.HasMember("queuedCharacters")||!value["queuedCharacters"].IsUint()
                    ||!value.HasMember("characterBuildSeconds")||!value["characterBuildSeconds"].IsNumber()
                    ||!value.HasMember("remainingBuildSeconds")||!value["remainingBuildSeconds"].IsNumber())
                    throw std::runtime_error("Invalid building production state in save file");
                entity.production.level=value["townHallLevel"].GetUint();
                entity.production.characterBuildSeconds=value["characterBuildSeconds"].GetFloat();
                const auto count=value["queuedCharacters"].GetUint();
                for(unsigned i=0;i<count;++i) {
                    const float remaining=i==0?value["remainingBuildSeconds"].GetFloat():entity.production.characterBuildSeconds;
                    entity.production.queue.push_back({ProductionKind::trainCharacter,entity.production.characterBuildSeconds,remaining});
                }
            } else if(version>=7) {
                if(!value.HasMember("townHallLevel")||!value["townHallLevel"].IsUint()
                    ||!value.HasMember("characterBuildSeconds")||!value["characterBuildSeconds"].IsNumber()
                    ||!value.HasMember("productionQueue")||!value["productionQueue"].IsArray())
                    throw std::runtime_error("Invalid building production queue in save file");
                entity.production.level=value["townHallLevel"].GetUint();
                entity.production.characterBuildSeconds=value["characterBuildSeconds"].GetFloat();
                if(version>=11) {
                    if(!value.HasMember("productionSpeedMultiplier")||!value["productionSpeedMultiplier"].IsNumber()
                        ||!value.HasMember("productionSpeedUpgrades")||!value["productionSpeedUpgrades"].IsUint())
                        throw std::runtime_error("Invalid production speed state in save file");
                    entity.production.productionSpeedMultiplier=value["productionSpeedMultiplier"].GetFloat();
                    entity.production.productionSpeedUpgrades=value["productionSpeedUpgrades"].GetUint();
                }
                for(const auto& item:value["productionQueue"].GetArray()) {
                    if(!item.IsObject()||!item.HasMember("kind")||!item["kind"].IsUint()
                        ||item["kind"].GetUint()>static_cast<unsigned>(ProductionKind::improveTraining)
                        ||!item.HasMember("durationSeconds")||!item["durationSeconds"].IsNumber()
                        ||!item.HasMember("remainingSeconds")||!item["remainingSeconds"].IsNumber())
                        throw std::runtime_error("Invalid production queue entry in save file");
                    entity.production.queue.push_back({static_cast<ProductionKind>(item["kind"].GetUint()),item["durationSeconds"].GetFloat(),item["remainingSeconds"].GetFloat()});
                }
            }
        }
        result.entities.push_back(std::move(entity));
    }
    return result;
}

} // namespace strategy
