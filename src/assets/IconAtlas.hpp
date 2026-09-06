#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace strategy {

struct IconRegion {
    int x{0}, y{0}, width{0}, height{0};
};

class IconAtlas final {
  public:
    static IconAtlas load(const std::filesystem::path& path);
    [[nodiscard]] const IconRegion* region(const std::string& id) const;
    [[nodiscard]] const std::string& texture() const { return texture_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

  private:
    std::string texture_;
    int width_{0}, height_{0};
    std::unordered_map<std::string, IconRegion> regions_;
};

} // namespace strategy
