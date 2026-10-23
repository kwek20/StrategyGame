#pragma once
#include "world/ResourceSiteMap.hpp"
namespace strategy {
class ResourceSiteTexture final {
  public:
    ~ResourceSiteTexture();
    ResourceSiteTexture() = default;
    ResourceSiteTexture(const ResourceSiteTexture&) = delete;
    ResourceSiteTexture& operator=(const ResourceSiteTexture&) = delete;
    void update(std::span<const ResourceSiteSource> sources, float extent);
    void beginPreparation(std::span<const ResourceSiteSource> sources, float extent);
    bool advancePreparation();
    float preparationProgress() const {
        return map_.updateProgress();
    }
    void clear();
    void bind() const;
    bool ready() const {
        return texture_ != 0;
    }
    std::size_t uploadedTiles() const {
        return uploadedTiles_;
    }

  private:
    void upload(int oldSide, std::span<const ResourceSiteTile> updates);
    ResourceSiteMap map_;
    std::uint32_t texture_{};
    std::size_t uploadedTiles_{};
};
} // namespace strategy
