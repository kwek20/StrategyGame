#pragma once

#include "assets/AssetManifest.hpp"
#include "assets/Model.hpp"
#include "assets/ResourceHandle.hpp"

#include <filesystem>
#include <deque>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace strategy {

struct EntityDefinition {
    std::string model;
    float scale{1.0F};
    float selectionRadius{0.5F};
    float selectionHeight{1.8F};
    std::unordered_map<std::string, std::string> animations;
};

class ResourceManager final {
  public:
    explicit ResourceManager(const std::filesystem::path& assetRoot);

    [[nodiscard]] ModelHandle requestModel(const std::string& key) const;
    [[nodiscard]] const Model* model(ModelHandle handle) const;
    [[nodiscard]] const Model* modelOrMarker(ModelHandle handle) const;
    [[nodiscard]] ResourceState state(ModelHandle handle) const;
    [[nodiscard]] std::string error(ModelHandle handle) const;
    [[nodiscard]] std::vector<ModelHandle> preloadGroup(const std::string& group) const;
    [[nodiscard]] AssetLoadProgress progress(const std::vector<ModelHandle>& handles) const;
    // Completes ready CPU imports and uploads them on the render thread.
    void update() const;
    [[nodiscard]] bool containsModel(const std::string& key) const;
    [[nodiscard]] const EntityDefinition* entityDefinition(const std::string& key) const;
    [[nodiscard]] std::size_t modelCount() const {
        return readyModelCount_;
    }
    [[nodiscard]] std::size_t indexedModelCount() const {
        return modelPaths_.size();
    }

  private:
    struct ModelSlot {
        std::uint32_t generation{1};
        std::string key;
        std::filesystem::path path;
        ResourceState state{ResourceState::queued};
        std::unique_ptr<Model> model;
        std::string error;
    };
    mutable std::vector<ModelSlot> modelSlots_;
    mutable std::unordered_map<std::string, ModelHandle> modelHandles_;
    mutable std::unordered_map<std::uint32_t, std::future<std::shared_ptr<ModelAsset>>>
        pendingModels_;
    mutable std::deque<std::uint32_t> queuedModels_;
    mutable std::size_t readyModelCount_{0};
    std::unique_ptr<Model> loadingMarker_;
    std::unique_ptr<Model> failedMarker_;
    std::unordered_map<std::string, std::filesystem::path> modelPaths_;
    std::unordered_map<std::string, EntityDefinition> entityDefinitions_;
    AssetManifest manifest_;
    std::thread::id renderThread_;

    static std::string normalizedKey(std::string key);
    void indexModels(const std::filesystem::path& directory);
    void loadEntityDefinitions(const std::filesystem::path& path);
    [[nodiscard]] std::string resolveKey(const std::string& key) const;
    void launchQueuedImports() const;
};

} // namespace strategy
