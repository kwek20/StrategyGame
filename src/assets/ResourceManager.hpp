#pragma once

#include "assets/AssetManifest.hpp"
#include "assets/Model.hpp"
#include "assets/ResourceHandle.hpp"
#include "assets/Texture.hpp"

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

struct AssetPreloadSet {
    std::vector<ModelHandle> models;
    std::vector<TextureHandle> textures;
};

class ResourceManager final {
  public:
    explicit ResourceManager(const std::filesystem::path& assetRoot);

    [[nodiscard]] ModelHandle requestModel(const std::string& key) const;
    [[nodiscard]] ModelHandle requestModel(PresentationId key) const {
        return requestModel(key.value);
    }
    [[nodiscard]] const Model* model(ModelHandle handle) const;
    [[nodiscard]] const Model* modelOrMarker(ModelHandle handle) const;
    [[nodiscard]] ResourceState state(ModelHandle handle) const;
    [[nodiscard]] std::string error(ModelHandle handle) const;
    [[nodiscard]] TextureHandle requestTexture(const std::string& key) const;
    [[nodiscard]] const Texture* texture(TextureHandle handle) const;
    [[nodiscard]] const Texture* textureOrMarker(TextureHandle handle) const;
    [[nodiscard]] ResourceState state(TextureHandle handle) const;
    [[nodiscard]] std::string error(TextureHandle handle) const;
    [[nodiscard]] AssetPreloadSet preloadGroup(const std::string& group) const;
    [[nodiscard]] AssetLoadProgress progress(const AssetPreloadSet& handles) const;
    // Completes ready CPU imports and uploads them on the render thread.
    void update() const;
    [[nodiscard]] bool containsModel(const std::string& key) const;
    [[nodiscard]] const EntityDefinition* entityDefinition(const std::string& key) const;
    [[nodiscard]] const EntityDefinition* entityDefinition(PresentationId key) const {
        return entityDefinition(key.value);
    }
    [[nodiscard]] std::size_t modelCount() const {
        return readyModelCount_;
    }
    [[nodiscard]] std::size_t indexedModelCount() const {
        return modelPaths_.size();
    }
    [[nodiscard]] std::size_t textureCount() const { return readyTextureCount_; }

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
    struct TextureSlot {
        std::uint32_t generation{1};
        std::string key;
        std::filesystem::path path;
        ResourceState state{ResourceState::queued};
        std::unique_ptr<Texture> texture;
        std::string error;
    };
    mutable std::vector<TextureSlot> textureSlots_;
    mutable std::unordered_map<std::string, TextureHandle> textureHandles_;
    mutable std::unordered_map<std::uint32_t,
                               std::future<std::shared_ptr<ModelTextureAsset>>>
        pendingTextures_;
    mutable std::deque<std::uint32_t> queuedTextures_;
    mutable std::size_t readyTextureCount_{0};
    std::unique_ptr<Model> loadingMarker_;
    std::unique_ptr<Model> failedMarker_;
    std::unique_ptr<Texture> loadingTexture_;
    std::unique_ptr<Texture> failedTexture_;
    std::unordered_map<std::string, std::filesystem::path> modelPaths_;
    std::unordered_map<std::string, std::filesystem::path> texturePaths_;
    std::unordered_map<std::string, EntityDefinition> entityDefinitions_;
    AssetManifest manifest_;
    std::thread::id renderThread_;

    static std::string normalizedKey(std::string key);
    void indexModels(const std::filesystem::path& directory);
    void indexTextures(const std::filesystem::path& directory);
    void loadEntityDefinitions(const std::filesystem::path& path);
    [[nodiscard]] std::string resolveKey(const std::string& key) const;
    void launchQueuedImports() const;
};

} // namespace strategy
