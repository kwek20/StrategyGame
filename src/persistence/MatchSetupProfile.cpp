#include "persistence/MatchSetupProfile.hpp"

#include <algorithm>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <stdexcept>

namespace strategy {
namespace {

rapidjson::Document readDocument(const std::filesystem::path& path) {
    rapidjson::Document document;
    std::ifstream stream(path);
    if (!stream) {
        document.SetObject();
        return document;
    }
    rapidjson::IStreamWrapper input(stream);
    document.ParseStream(input);
    if (document.HasParseError() || !document.IsObject())
        throw std::runtime_error("Invalid match setup profile: " + path.string());
    return document;
}

void setMember(rapidjson::Value& object, const char* name, std::uint32_t value,
               rapidjson::Document::AllocatorType& allocator) {
    if (object.HasMember(name)) object.RemoveMember(name);
    rapidjson::Value key{name, allocator};
    rapidjson::Value stored;
    stored.SetUint(value);
    object.AddMember(key, stored, allocator);
}

void setMember(rapidjson::Value& object, const char* name, float value,
               rapidjson::Document::AllocatorType& allocator) {
    if (object.HasMember(name)) object.RemoveMember(name);
    rapidjson::Value key{name, allocator};
    rapidjson::Value stored;
    stored.SetFloat(value);
    object.AddMember(key, stored, allocator);
}

void setString(rapidjson::Value& object, const char* name, const std::string& value,
               rapidjson::Document::AllocatorType& allocator) {
    if (object.HasMember(name)) object.RemoveMember(name);
    object.AddMember(rapidjson::Value{name, allocator},
                     rapidjson::Value{value.c_str(), allocator}, allocator);
}

} // namespace

MatchSetupProfile MatchSetupProfileStore::load(const std::filesystem::path& path) {
    MatchSetupProfile result;
    const rapidjson::Document document = readDocument(path);
    if (!document.HasMember("matchSetup")) return result;
    const rapidjson::Value& setup = document["matchSetup"];
    if (!setup.IsObject())
        throw std::runtime_error("matchSetup must be an object: " + path.string());
    if (setup.HasMember("terrainSeed") && setup["terrainSeed"].IsUint())
        result.terrainSeed = setup["terrainSeed"].GetUint();
    if (setup.HasMember("playerOneCountry") && setup["playerOneCountry"].IsString())
        result.playerOneCountry = setup["playerOneCountry"].GetString();
    if (setup.HasMember("playerTwoCountry") && setup["playerTwoCountry"].IsString())
        result.playerTwoCountry = setup["playerTwoCountry"].GetString();
    if (setup.HasMember("mapChunksPerSide") && setup["mapChunksPerSide"].IsUint())
        result.mapChunksPerSide = std::clamp(setup["mapChunksPerSide"].GetUint(), 10U, 20U);
    if (setup.HasMember("startingResourcesScale") &&
        setup["startingResourcesScale"].IsNumber())
        result.startingResourcesScale =
            std::clamp(setup["startingResourcesScale"].GetFloat(), 0.0F, 4.0F);
    if (setup.HasMember("resourceAbundanceScale") &&
        setup["resourceAbundanceScale"].IsNumber())
        result.resourceAbundanceScale =
            std::clamp(setup["resourceAbundanceScale"].GetFloat(), 0.5F, 2.0F);
    if (setup.HasMember("terrainLayout") && setup["terrainLayout"].IsString())
        result.terrainLayout = setup["terrainLayout"].GetString();
    return result;
}

void MatchSetupProfileStore::write(const std::filesystem::path& path,
                                   const MatchSetupProfile& profile) {
    rapidjson::Document document = readDocument(path);
    auto& allocator = document.GetAllocator();
    if (!document.HasMember("matchSetup"))
        document.AddMember("matchSetup", rapidjson::Value{rapidjson::kObjectType}, allocator);
    if (!document["matchSetup"].IsObject()) document["matchSetup"].SetObject();
    rapidjson::Value& setup = document["matchSetup"];
    setMember(setup, "terrainSeed", profile.terrainSeed, allocator);
    setString(setup, "playerOneCountry", profile.playerOneCountry, allocator);
    setString(setup, "playerTwoCountry", profile.playerTwoCountry, allocator);
    setMember(setup, "mapChunksPerSide", profile.mapChunksPerSide, allocator);
    setMember(setup, "startingResourcesScale", profile.startingResourcesScale, allocator);
    setMember(setup, "resourceAbundanceScale", profile.resourceAbundanceScale, allocator);
    setString(setup, "terrainLayout", profile.terrainLayout, allocator);
    // Deliberately do not recreate the object: unknown options (for example future biome
    // overrides) remain untouched.
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::trunc);
    if (!stream)
        throw std::runtime_error("Could not write match setup profile: " + path.string());
    rapidjson::OStreamWrapper output(stream);
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(output);
    document.Accept(writer);
}

} // namespace strategy
