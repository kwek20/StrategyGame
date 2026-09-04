#pragma once

#include "assets/Model.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

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

    [[nodiscard]] const Model* model(const std::string& key) const;
    [[nodiscard]] bool containsModel(const std::string& key) const;
    [[nodiscard]] const EntityDefinition* entityDefinition(const std::string& key) const;
    [[nodiscard]] std::size_t modelCount() const { return models_.size(); }
    [[nodiscard]] std::size_t indexedModelCount() const { return modelPaths_.size(); }

private:
    mutable std::unordered_map<std::string, std::unique_ptr<Model>> models_;
    std::unordered_map<std::string, std::filesystem::path> modelPaths_;
    std::unordered_map<std::string, EntityDefinition> entityDefinitions_;

    static std::string normalizedKey(std::string key);
    void indexModels(const std::filesystem::path& directory);
    void loadEntityDefinitions(const std::filesystem::path& path);
    [[nodiscard]] std::string resolveKey(const std::string& key) const;
};

} // namespace strategy
