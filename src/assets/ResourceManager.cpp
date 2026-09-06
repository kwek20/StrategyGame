#include "assets/ResourceManager.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <future>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <unordered_set>

namespace strategy {

ResourceManager::ResourceManager(const std::filesystem::path& assetRoot)
    : manifest_(AssetManifest::load(assetRoot / "asset_manifest.json")),
      renderThread_(std::this_thread::get_id()) {
    indexModels(assetRoot / "models");
    loadEntityDefinitions(assetRoot / "entities.json");
    loadingMarker_ = std::make_unique<Model>(makeMarkerModelAsset(false));
    failedMarker_ = std::make_unique<Model>(makeMarkerModelAsset(true));
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

ModelHandle ResourceManager::requestModel(const std::string& key) const {
    if (std::this_thread::get_id() != renderThread_)
        throw std::runtime_error("GPU model access must occur on the render thread");
    const std::string resolved = resolveKey(key);
    const std::string handleKey = resolved.empty() ? normalizedKey(key) : resolved;
    if (const auto found = modelHandles_.find(handleKey); found != modelHandles_.end())
        return found->second;
    const std::uint32_t index = static_cast<std::uint32_t>(modelSlots_.size());
    ModelSlot slot;
    slot.key = handleKey;
    if (resolved.empty()) {
        slot.state = ResourceState::failed;
        slot.error = "Model is not indexed";
    } else {
        slot.path = modelPaths_.at(resolved);
        queuedModels_.push_back(index);
    }
    modelSlots_.push_back(std::move(slot));
    const ModelHandle handle{index, modelSlots_.back().generation};
    modelHandles_.emplace(handleKey, handle);
    launchQueuedImports();
    return handle;
}

ResourceState ResourceManager::state(ModelHandle handle) const {
    if (!handle || handle.index >= modelSlots_.size() ||
        modelSlots_[handle.index].generation != handle.generation)
        return ResourceState::invalid;
    return modelSlots_[handle.index].state;
}

const Model* ResourceManager::model(ModelHandle handle) const {
    return state(handle) == ResourceState::ready ? modelSlots_[handle.index].model.get() : nullptr;
}

const Model* ResourceManager::modelOrMarker(ModelHandle handle) const {
    const ResourceState current = state(handle);
    if (current == ResourceState::ready)
        return modelSlots_[handle.index].model.get();
    return current == ResourceState::failed || current == ResourceState::invalid
               ? failedMarker_.get()
               : loadingMarker_.get();
}

std::string ResourceManager::error(ModelHandle handle) const {
    return state(handle) == ResourceState::failed ? modelSlots_[handle.index].error : std::string{};
}

std::vector<ModelHandle> ResourceManager::preloadGroup(const std::string& groupName) const {
    std::vector<ModelHandle> handles;
    if (const AssetGroup* group = manifest_.group(groupName)) {
        handles.reserve(group->models.size());
        for (const std::string& modelKey : group->models)
            handles.push_back(requestModel(modelKey));
    }
    return handles;
}

AssetLoadProgress ResourceManager::progress(const std::vector<ModelHandle>& handles) const {
    AssetLoadProgress result;
    result.total = handles.size();
    for (ModelHandle handle : handles) {
        const ResourceState current = state(handle);
        if (current == ResourceState::ready || current == ResourceState::failed) {
            ++result.completed;
            if (current == ResourceState::failed)
                ++result.failed;
        } else if (current == ResourceState::uploading || current == ResourceState::importing) {
            result.activeState = current;
        } else if (result.activeState == ResourceState::invalid) {
            result.activeState = current;
        }
    }
    return result;
}

void ResourceManager::launchQueuedImports() const {
    constexpr std::size_t maximumConcurrentImports = 2;
    while (pendingModels_.size() < maximumConcurrentImports && !queuedModels_.empty()) {
        const std::uint32_t index = queuedModels_.front();
        queuedModels_.pop_front();
        ModelSlot& slot = modelSlots_[index];
        slot.state = ResourceState::importing;
        pendingModels_.emplace(index, std::async(std::launch::async, [path = slot.path] {
                                   return importModelAsset(path);
                               }));
    }
}

void ResourceManager::update() const {
    if (std::this_thread::get_id() != renderThread_)
        throw std::runtime_error("GPU upload queue must be processed on the render thread");
    using namespace std::chrono_literals;
    for (auto pending = pendingModels_.begin(); pending != pendingModels_.end();) {
        if (pending->second.wait_for(0ms) != std::future_status::ready) {
            ++pending;
            continue;
        }
        const std::uint32_t index = pending->first;
        ModelSlot& slot = modelSlots_[index];
        try {
            std::shared_ptr<ModelAsset> asset = pending->second.get();
            slot.state = ResourceState::uploading;
            slot.model = std::make_unique<Model>(std::move(asset));
            slot.state = ResourceState::ready;
            ++readyModelCount_;
            std::cout << "Imported and uploaded model '" << slot.key << "'\n";
        } catch (const std::exception& error) {
            slot.state = ResourceState::failed;
            slot.error = error.what();
            std::cerr << "Could not prepare model '" << slot.key << "': " << error.what() << '\n';
        }
        pending = pendingModels_.erase(pending);
    }
    launchQueuedImports();
}

bool ResourceManager::containsModel(const std::string& key) const {
    return !resolveKey(key).empty();
}

} // namespace strategy
