#include "assets/IconAtlas.hpp"

#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <stdexcept>

namespace strategy {
IconAtlas IconAtlas::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Missing icon atlas: " + path.string());
    rapidjson::IStreamWrapper input(stream);
    rapidjson::Document doc; doc.ParseStream(input);
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("texture") ||
        !doc["texture"].IsString() || !doc.HasMember("width") || !doc.HasMember("height") ||
        !doc.HasMember("regions") || !doc["regions"].IsObject())
        throw std::runtime_error("Invalid icon atlas: " + path.string());
    IconAtlas atlas;
    atlas.texture_ = doc["texture"].GetString();
    atlas.width_ = doc["width"].GetInt(); atlas.height_ = doc["height"].GetInt();
    for (const auto& member : doc["regions"].GetObject()) {
        const auto& v = member.value;
        if (!v.IsObject() || !v.HasMember("x") || !v.HasMember("y") || !v.HasMember("width") || !v.HasMember("height")) continue;
        atlas.regions_.emplace(member.name.GetString(), IconRegion{v["x"].GetInt(), v["y"].GetInt(), v["width"].GetInt(), v["height"].GetInt()});
    }
    return atlas;
}
const IconRegion* IconAtlas::region(const std::string& id) const {
    const auto it = regions_.find(id); return it == regions_.end() ? nullptr : &it->second;
}
} // namespace strategy
