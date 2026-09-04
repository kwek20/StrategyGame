#include "persistence/GameConfig.hpp"

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>

#include <fstream>
#include <algorithm>
#include <vector>
#include <stdexcept>

namespace strategy {

GameConfig GameConfig::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Could not open configuration file: " + path.string());
    }
    rapidjson::IStreamWrapper input{stream};
    rapidjson::Document document;
    document.ParseStream(input);
    if (document.HasParseError() || !document.IsObject()
        || !document.HasMember("configuration")
        || !document["configuration"].IsObject()) {
        throw std::runtime_error("Invalid configuration JSON: " + path.string());
    }

    const auto& configuration = document["configuration"];
    if (!configuration.HasMember("saveDirectory")
        || !configuration["saveDirectory"].IsString()
        || !configuration.HasMember("saveFile")
        || !configuration["saveFile"].IsString()) {
        throw std::runtime_error(
            "Configuration requires string saveDirectory and saveFile values");
    }

    GameConfig result;
    result.saveDirectory = configuration["saveDirectory"].GetString();
    result.saveFile = configuration["saveFile"].GetString();
    if(configuration.HasMember("video")&&configuration["video"].IsObject()){const auto& video=configuration["video"];if(video.HasMember("width")&&video["width"].IsInt())result.resolutionWidth=video["width"].GetInt();if(video.HasMember("height")&&video["height"].IsInt())result.resolutionHeight=video["height"].GetInt();if(video.HasMember("fullscreen")&&video["fullscreen"].IsBool())result.fullscreen=video["fullscreen"].GetBool();}
    if(configuration.HasMember("audio")&&configuration["audio"].IsObject()&&configuration["audio"].HasMember("masterVolume")&&configuration["audio"]["masterVolume"].IsNumber())result.masterVolume=std::clamp(configuration["audio"]["masterVolume"].GetFloat(),0.0F,1.0F);
    if(configuration.HasMember("keybinds")&&configuration["keybinds"].IsObject())for(auto item=configuration["keybinds"].MemberBegin();item!=configuration["keybinds"].MemberEnd();++item)if(item->value.IsInt())result.keybinds[item->name.GetString()]=item->value.GetInt();
    if (result.saveDirectory.is_absolute()) {
        throw std::runtime_error("saveDirectory must be relative to the game directory");
    }
    if (std::filesystem::path(result.saveFile).filename() != result.saveFile) {
        throw std::runtime_error("saveFile must be a filename, not a path");
    }
    return result;
}

void GameConfig::write(const std::filesystem::path& path)const{std::filesystem::create_directories(path.parent_path());std::ofstream stream(path,std::ios::trunc);if(!stream)throw std::runtime_error("Could not write configuration file: "+path.string());rapidjson::OStreamWrapper output(stream);rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(output);writer.StartObject();writer.Key("configuration");writer.StartObject();writer.Key("saveDirectory");writer.String(saveDirectory.generic_string().c_str());writer.Key("saveFile");writer.String(saveFile.c_str());writer.Key("video");writer.StartObject();writer.Key("width");writer.Int(resolutionWidth);writer.Key("height");writer.Int(resolutionHeight);writer.Key("fullscreen");writer.Bool(fullscreen);writer.EndObject();writer.Key("audio");writer.StartObject();writer.Key("masterVolume");writer.Double(masterVolume);writer.EndObject();writer.Key("keybinds");writer.StartObject();std::vector<std::pair<std::string,std::int32_t>> sorted(keybinds.begin(),keybinds.end());std::sort(sorted.begin(),sorted.end());for(const auto& [name,key]:sorted){writer.Key(name.c_str());writer.Int(key);}writer.EndObject();writer.EndObject();writer.EndObject();}

std::filesystem::path GameConfig::savePath() const {
    return saveDirectory / saveFile;
}

} // namespace strategy
