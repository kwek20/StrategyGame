#pragma once

#include "terrain/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/vec2.hpp>

namespace strategy {

// Canonical conversion between match-space coordinates and normalized/grid space.
class MapArea final {
  public:
    explicit MapArea(std::uint32_t chunksPerSide)
        : chunksPerSide_(std::clamp(chunksPerSide, 1U,
              static_cast<std::uint32_t>(Terrain::chunksPerSide))) {}

    [[nodiscard]] std::uint32_t chunksPerSide() const { return chunksPerSide_; }
    [[nodiscard]] float extent() const {
        return static_cast<float>(chunksPerSide_ * Terrain::chunkCellCount) * Terrain::spacing;
    }
    [[nodiscard]] float halfExtent() const { return extent() * 0.5F; }
    [[nodiscard]] glm::vec2 normalized(glm::vec2 world) const {
        return world / extent() + glm::vec2{0.5F};
    }
    [[nodiscard]] glm::vec2 worldFromNormalized(glm::vec2 normalizedPosition) const {
        return (normalizedPosition - glm::vec2{0.5F}) * extent();
    }
    [[nodiscard]] glm::ivec2 gridCell(glm::vec2 world, int cellsPerSide) const {
        const glm::vec2 grid = normalized(world) * static_cast<float>(cellsPerSide);
        return {std::clamp(static_cast<int>(grid.x), 0, cellsPerSide - 1),
                std::clamp(static_cast<int>(grid.y), 0, cellsPerSide - 1)};
    }
    [[nodiscard]] glm::vec2 gridCellCenter(glm::ivec2 cell, int cellsPerSide) const {
        const glm::vec2 normalizedCenter =
            (glm::vec2{cell} + glm::vec2{0.5F}) / static_cast<float>(cellsPerSide);
        return worldFromNormalized(normalizedCenter);
    }
    [[nodiscard]] bool contains(glm::vec2 world, float margin = 0.0F) const {
        const float boundary = halfExtent() - margin;
        return std::abs(world.x) <= boundary && std::abs(world.y) <= boundary;
    }

  private:
    std::uint32_t chunksPerSide_;
};

} // namespace strategy
