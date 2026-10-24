#include "persistence/GameConfig.hpp"

#include <algorithm>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <stdexcept>
#include <vector>

namespace strategy {

GameConfig GameConfig::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Could not open configuration file: " + path.string());
    }
    rapidjson::IStreamWrapper input{stream};
    rapidjson::Document document;
    document.ParseStream(input);
    if (document.HasParseError() || !document.IsObject() || !document.HasMember("configuration") ||
        !document["configuration"].IsObject()) {
        throw std::runtime_error("Invalid configuration JSON: " + path.string());
    }

    const auto& configuration = document["configuration"];
    if (!configuration.HasMember("saveDirectory") || !configuration["saveDirectory"].IsString() ||
        !configuration.HasMember("saveFile") || !configuration["saveFile"].IsString()) {
        throw std::runtime_error("Configuration requires string saveDirectory and saveFile values");
    }

    GameConfig result;
    result.saveDirectory = configuration["saveDirectory"].GetString();
    result.saveFile = configuration["saveFile"].GetString();
    if (configuration.HasMember("matchSetupFile") && configuration["matchSetupFile"].IsString())
        result.matchSetupFile = configuration["matchSetupFile"].GetString();
    if (configuration.HasMember("video") && configuration["video"].IsObject()) {
        const auto& video = configuration["video"];
        if (video.HasMember("width") && video["width"].IsInt())
            result.resolutionWidth = video["width"].GetInt();
        if (video.HasMember("height") && video["height"].IsInt())
            result.resolutionHeight = video["height"].GetInt();
        if (video.HasMember("fullscreen") && video["fullscreen"].IsBool())
            result.fullscreen = video["fullscreen"].GetBool();
        if (video.HasMember("uiScale") && video["uiScale"].IsNumber())
            result.uiScale = std::clamp(video["uiScale"].GetFloat(), 0.75F, 1.5F);
    }
    if (configuration.HasMember("audio") && configuration["audio"].IsObject()) {
        const auto& audio = configuration["audio"];
        if (audio.HasMember("masterVolume") && audio["masterVolume"].IsNumber())
            result.masterVolume = std::clamp(audio["masterVolume"].GetFloat(), 0.0F, 1.0F);
        if (audio.HasMember("musicVolume") && audio["musicVolume"].IsNumber())
            result.musicVolume = std::clamp(audio["musicVolume"].GetFloat(), 0.0F, 1.0F);
        if (audio.HasMember("effectsVolume") && audio["effectsVolume"].IsNumber())
            result.effectsVolume = std::clamp(audio["effectsVolume"].GetFloat(), 0.0F, 1.0F);
        if (audio.HasMember("muted") && audio["muted"].IsBool())
            result.muted = audio["muted"].GetBool();
    }
    if (configuration.HasMember("keybinds") && configuration["keybinds"].IsObject()) {
        for (auto item = configuration["keybinds"].MemberBegin();
             item != configuration["keybinds"].MemberEnd();
             ++item) {
            const std::string id = item->name.GetString();
            if (!inputAction(id))
                continue;
            // Preserve existing user preferences when opening a pre-rewrite settings file.
            if (item->value.IsInt()) {
                result.keybinds.values[id][0] = {BindingDevice::key, item->value.GetInt()};
                continue;
            }
            if (!item->value.IsArray() || item->value.Size() != 2)
                throw std::runtime_error("Binding requires primary and secondary slots: " + id);
            BindingSlots slots{};
            for (unsigned i = 0; i < 2; ++i) {
                const auto& value = item->value[i];
                if (!value.IsObject() || !value.HasMember("device") || !value["device"].IsUint() ||
                    !value.HasMember("code") || !value["code"].IsInt() ||
                    !value.HasMember("modifiers") || !value["modifiers"].IsUint() ||
                    value["device"].GetUint() > static_cast<unsigned>(BindingDevice::modifier) ||
                    value["modifiers"].GetUint() > 15)
                    throw std::runtime_error("Invalid input binding: " + id);
                slots[i] = {static_cast<BindingDevice>(value["device"].GetUint()),
                            value["code"].GetInt(),
                            value["modifiers"].GetUint()};
                if ((slots[i].device == BindingDevice::modifier &&
                     (slots[i].code < 0 || slots[i].code > 15)) ||
                    ((slots[i].device == BindingDevice::mouse ||
                      slots[i].device == BindingDevice::drag) &&
                     (slots[i].code < 0 || slots[i].code > 5)) ||
                    (slots[i].device == BindingDevice::wheel && slots[i].code != -1 &&
                     slots[i].code != 0 && slots[i].code != 1))
                    throw std::runtime_error("Invalid input binding code: " + id);
            }
            for (const auto& binding : slots)
                if (inputAction(id)->requiresRelease && binding.device == BindingDevice::wheel &&
                    binding)
                    throw std::runtime_error("Continuous action cannot use the mouse wheel: " + id);
            result.keybinds.values[id] = slots;
        }
    }
    if (result.saveDirectory.is_absolute()) {
        throw std::runtime_error("saveDirectory must be relative to the game directory");
    }
    if (std::filesystem::path(result.saveFile).filename() != result.saveFile) {
        throw std::runtime_error("saveFile must be a filename, not a path");
    }
    if (result.matchSetupFile.is_absolute())
        throw std::runtime_error("matchSetupFile must be relative to the game directory");
    return result;
}

void GameConfig::write(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::trunc);
    if (!stream)
        throw std::runtime_error("Could not write configuration file: " + path.string());
    rapidjson::OStreamWrapper output(stream);
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(output);
    writer.StartObject();
    writer.Key("configuration");
    writer.StartObject();
    writer.Key("saveDirectory");
    writer.String(saveDirectory.generic_string().c_str());
    writer.Key("saveFile");
    writer.String(saveFile.c_str());
    writer.Key("matchSetupFile");
    writer.String(matchSetupFile.generic_string().c_str());
    writer.Key("video");
    writer.StartObject();
    writer.Key("width");
    writer.Int(resolutionWidth);
    writer.Key("height");
    writer.Int(resolutionHeight);
    writer.Key("fullscreen");
    writer.Bool(fullscreen);
    writer.Key("uiScale");
    writer.Double(uiScale);
    writer.EndObject();
    writer.Key("audio");
    writer.StartObject();
    writer.Key("masterVolume");
    writer.Double(masterVolume);
    writer.Key("musicVolume");
    writer.Double(musicVolume);
    writer.Key("effectsVolume");
    writer.Double(effectsVolume);
    writer.Key("muted");
    writer.Bool(muted);
    writer.EndObject();
    writer.Key("keybinds");
    writer.StartObject();
    for (const auto& [name, slots] : keybinds.values) {
        writer.Key(name.c_str());
        writer.StartArray();
        for (const auto& binding : slots) {
            writer.StartObject();
            writer.Key("device");
            writer.Uint(static_cast<unsigned>(binding.device));
            writer.Key("code");
            writer.Int(binding.code);
            writer.Key("modifiers");
            writer.Uint(binding.modifiers);
            writer.EndObject();
        }
        writer.EndArray();
    }
    writer.EndObject();
    writer.EndObject();
    writer.EndObject();
}

std::filesystem::path GameConfig::savePath() const {
    return saveDirectory / saveFile;
}

} // namespace strategy
