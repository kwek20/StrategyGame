#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace strategy {

struct AssetGroup {
    std::vector<std::string> models;
    std::vector<std::string> textures;
    std::vector<std::string> sounds;
    std::vector<std::string> definitions;
};

class AssetManifest final {
  public:
    static AssetManifest load(const std::filesystem::path& path);
    [[nodiscard]] const AssetGroup* group(const std::string& name) const;

  private:
    std::unordered_map<std::string, AssetGroup> groups_;
};

} // namespace strategy
