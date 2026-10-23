#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <optional>
#include <span>
#include <vector>

namespace strategy {
// Presentation reconstructed from authoritative entities; never saved or used for gathering.
struct ResourceSiteSource {
    std::uint64_t id{};
    glm::vec2 position{};
    float radius{};
    std::uint8_t family{}; // scrap, oil, uranium
    std::uint64_t fieldId{};
    bool operator==(const ResourceSiteSource&) const = default;
};
struct ResourceSiteTile {
    int x{}, y{}, width{}, height{};
};

class ResourceSiteMap final {
  public:
    // RGBA stores scrap earth, oil earth, uranium earth and compact oil wetness.
    // Returns only changed texture regions. Unchanged worlds perform no rasterization.
    std::vector<ResourceSiteTile> update(std::span<const ResourceSiteSource> sources, float extent);
    // Loading preparation uses the same painter, bounded to a caller-selected number of tiles.
    void beginUpdate(std::span<const ResourceSiteSource> sources, float extent);
    bool advanceUpdate(std::size_t maximumTiles = 1);
    float updateProgress() const;
    void clear();
    int side() const {
        return side_;
    }
    const std::vector<std::uint8_t>& pixels() const {
        return pixels_;
    }
    std::size_t revision() const {
        return revision_;
    }
    float densityAt(glm::vec2 position) const;

  private:
    struct Brush {
        glm::vec2 center, direction;
        float length, width, strength;
        std::uint8_t family;
        glm::vec2 halfBounds{};
    };
    struct PendingUpdate {
        std::vector<Brush> brushes;
        std::vector<ResourceSiteTile> tiles;
        std::size_t next{};
    };
    std::optional<PendingUpdate> pending_;
    float extent_{};
    int side_{};
    std::size_t revision_{};
    std::vector<ResourceSiteSource> sources_;
    std::vector<std::uint8_t> pixels_;
};
} // namespace strategy
