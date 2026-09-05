#include "assets/ResourceManager.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <unordered_set>

namespace strategy {

ResourceManager::ResourceManager(const std::filesystem::path& assetRoot) {
    indexModels(assetRoot / "models");
    loadEntityDefinitions(assetRoot / "entities.json");
    std::cout << "Indexed " << modelPaths_.size() << " model assets\n";
}

void ResourceManager::loadEntityDefinitions(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        std::cerr << "No entity catalogue at " << path.string() << '\n';
        return;
    }
    rapidjson::IStreamWrapper input(stream);
    rapidjson::Document document;
    document.ParseStream(input);
    if (document.HasParseError() || !document.HasMember("entities") ||
        !document["entities"].IsObject()) {
        throw std::runtime_error("Invalid entity catalogue: " + path.string());
    }
    for (const auto& member : document["entities"].GetObject()) {
        if (!member.value.IsObject() || !member.value.HasMember("model") ||
            !member.value["model"].IsString()) {
            continue;
        }
        EntityDefinition definition;
        definition.model = member.value["model"].GetString();
        if (member.value.HasMember("scale") && member.value["scale"].IsNumber()) {
            definition.scale = member.value["scale"].GetFloat();
        }
        if (member.value.HasMember("selection") && member.value["selection"].IsObject()) {
            const auto& selection = member.value["selection"];
            if (selection.HasMember("radius") && selection["radius"].IsNumber())
                definition.selectionRadius = selection["radius"].GetFloat();
            if (selection.HasMember("height") && selection["height"].IsNumber())
                definition.selectionHeight = selection["height"].GetFloat();
        }
        if (member.value.HasMember("animations") && member.value["animations"].IsObject()) {
            for (const auto& animation : member.value["animations"].GetObject()) {
                if (animation.value.IsString()) {
                    definition.animations.insert_or_assign(
                        normalizedKey(animation.name.GetString()), animation.value.GetString());
                }
            }
        }
        entityDefinitions_.insert_or_assign(normalizedKey(member.name.GetString()),
                                            std::move(definition));
    }
}

std::string ResourceManager::normalizedKey(std::string key) {
    std::replace(key.begin(), key.end(), '\\', '/');
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return key;
}

void ResourceManager::indexModels(const std::filesystem::path& directory) {
    static const std::unordered_set<std::string> extensions{
        ".obj", ".fbx", ".dae", ".3ds", ".gltf", ".glb", ".ply", ".stl"};
    if (!std::filesystem::is_directory(directory)) {
        std::cout << "No model asset directory at " << directory.string() << '\n';
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = normalizedKey(entry.path().extension().string());
        if (!extensions.contains(extension)) {
            continue;
        }
        const std::string relativeKey =
            normalizedKey(std::filesystem::relative(entry.path(), directory)
                              .replace_extension()
                              .generic_string());
        modelPaths_.insert_or_assign(relativeKey, entry.path());
    }
}

std::string ResourceManager::resolveKey(const std::string& key) const {
    std::string normalized = normalizedKey(key);
    if (const auto definition = entityDefinitions_.find(normalized);
        definition != entityDefinitions_.end()) {
        normalized = normalizedKey(definition->second.model);
    }
    if (modelPaths_.contains(normalized)) {
        return normalized;
    }
    for (const auto& [storedKey, path] : modelPaths_) {
        if (std::filesystem::path(storedKey).filename().string() == normalized) {
            return storedKey;
        }
    }
    return {};
}

const EntityDefinition* ResourceManager::entityDefinition(const std::string& key) const {
    const auto found = entityDefinitions_.find(normalizedKey(key));
    return found == entityDefinitions_.end() ? nullptr : &found->second;
}

const Model* ResourceManager::model(const std::string& key) const {
    const std::string resolved = resolveKey(key);
    if (resolved.empty()) {
        return nullptr;
    }
    if (const auto loaded = models_.find(resolved); loaded != models_.end()) {
        return loaded->second.get();
    }
    try {
        auto imported = std::make_unique<Model>(modelPaths_.at(resolved));
        const Model* result = imported.get();
        models_.insert_or_assign(resolved, std::move(imported));
        std::cout << "Imported model '" << resolved << "'\n";
        return result;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return nullptr;
    }
}

bool ResourceManager::containsModel(const std::string& key) const {
    return !resolveKey(key).empty();
}

} // namespace strategy
