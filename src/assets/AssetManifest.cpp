#include "assets/AssetManifest.hpp"

#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <stdexcept>

namespace strategy {
namespace {
void strings(const rapidjson::Value& object, const char* name, std::vector<std::string>& output) {
    if (!object.HasMember(name) || !object[name].IsArray())
        return;
    for (const auto& value : object[name].GetArray())
        if (value.IsString())
            output.emplace_back(value.GetString());
}
} // namespace

AssetManifest AssetManifest::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error("Missing asset manifest: " + path.string());
    rapidjson::IStreamWrapper input(stream);
    rapidjson::Document document;
    document.ParseStream(input);
    if (document.HasParseError() || !document.IsObject() || !document.HasMember("groups") ||
        !document["groups"].IsObject())
        throw std::runtime_error("Invalid asset manifest: " + path.string());
    AssetManifest result;
    for (const auto& member : document["groups"].GetObject()) {
        if (!member.value.IsObject())
            continue;
        AssetGroup group;
        strings(member.value, "models", group.models);
        strings(member.value, "textures", group.textures);
        strings(member.value, "shaders", group.shaders);
        strings(member.value, "sounds", group.sounds);
        strings(member.value, "definitions", group.definitions);
        result.groups_.emplace(member.name.GetString(), std::move(group));
    }
    return result;
}

const AssetGroup* AssetManifest::group(const std::string& name) const {
    const auto found = groups_.find(name);
    return found == groups_.end() ? nullptr : &found->second;
}

} // namespace strategy
