#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/vec2.hpp>
#include <map>
#include <stdexcept>
#include <utility>

namespace strategy {
// Sparse world-space fog. The playable rectangle is deliberately not part of this type.
class VisibilityGrid final {
  public:
    static constexpr int referenceCells = 128;
    static constexpr int chunkSide = 16;
    static constexpr int chunkCells = chunkSide * chunkSide;
    static constexpr std::uint8_t explored = 1, visible = 2;
    using Key = std::pair<int, int>;
    using Chunk = std::array<std::uint8_t, chunkCells>;

    VisibilityGrid() = default;
    explicit VisibilityGrid(float cellSize) : cellSize_(cellSize) {
        if (!std::isfinite(cellSize) || cellSize <= 0)
            throw std::runtime_error("Invalid visibility cell size");
    }
    float cellSize() const { return cellSize_; }
    const std::map<Key, Chunk>& chunks() const { return chunks_; }
    void clear() { chunks_.clear(); }
    void beginTick() {
        for (auto& [key, cells] : chunks_) {
            (void)key;
            for (auto& cell : cells) cell &= explored;
        }
    }
    glm::ivec2 cellAt(glm::vec2 point) const {
        return {static_cast<int>(std::floor(point.x / cellSize_)),
                static_cast<int>(std::floor(point.y / cellSize_))};
    }
    glm::vec2 cellCenter(glm::ivec2 cell) const {
        return (glm::vec2{cell} + .5F) * cellSize_;
    }
    std::uint8_t atCell(glm::ivec2 cell) const {
        const auto [key, index] = address(cell);
        const auto found = chunks_.find(key);
        return found == chunks_.end() ? 0 : found->second[index];
    }
    bool visibleAt(glm::vec2 point) const { return (atCell(cellAt(point)) & visible) != 0; }
    bool exploredAt(glm::vec2 point) const { return (atCell(cellAt(point)) & explored) != 0; }
    void setCell(glm::ivec2 cell, bool live) {
        const auto [key, index] = address(cell);
        chunks_[key][index] |= static_cast<std::uint8_t>(explored | (live ? visible : 0));
    }
    void hideCell(glm::ivec2 cell) {
        const auto [key, index] = address(cell);
        const auto found = chunks_.find(key);
        if (found != chunks_.end()) found->second[index] &= explored;
    }
    bool coversArea(glm::vec2 minimum, glm::vec2 maximum, bool live) const {
        const auto first = cellAt(minimum);
        const glm::ivec2 last{static_cast<int>(std::ceil(maximum.x / cellSize_)) - 1,
                              static_cast<int>(std::ceil(maximum.y / cellSize_)) - 1};
        const auto firstKey = address(first).first, lastKey = address(last).first;
        const auto mask = live ? visible : explored;
        for (int z = firstKey.second; z <= lastKey.second; ++z)
            for (int x = firstKey.first; x <= lastKey.first; ++x) {
                const auto found = chunks_.find({x, z});
                if (found == chunks_.end()) return false;
                for (int cz = std::max(first.y - z * chunkSide, 0); cz <= std::min(last.y - z * chunkSide, chunkSide - 1); ++cz)
                    for (int cx = std::max(first.x - x * chunkSide, 0); cx <= std::min(last.x - x * chunkSide, chunkSide - 1); ++cx)
                        if (!(found->second[cz * chunkSide + cx] & mask)) return false;
            }
        return true;
    }
    void revealCircle(glm::vec2 center, float radius) {
        if (radius <= 0) return;
        const auto first = cellAt(center - radius), last = cellAt(center + radius);
        for (int z = first.y; z <= last.y; ++z)
            for (int x = first.x; x <= last.x; ++x) {
                const auto delta = cellCenter({x, z}) - center;
                if (delta.x * delta.x + delta.y * delta.y <= radius * radius)
                    setCell({x, z}, true);
            }
    }
    void revealArea(glm::vec2 minimum, glm::vec2 maximum, bool live) {
        const glm::ivec2 first{static_cast<int>(std::ceil(minimum.x / cellSize_ - .5F)),
                               static_cast<int>(std::ceil(minimum.y / cellSize_ - .5F))};
        const glm::ivec2 last{static_cast<int>(std::ceil(maximum.x / cellSize_ - .5F)) - 1,
                              static_cast<int>(std::ceil(maximum.y / cellSize_ - .5F)) - 1};
        if (last.x < first.x || last.y < first.y) return;
        const auto firstKey = address(first).first, lastKey = address(last).first;
        const auto flags = static_cast<std::uint8_t>(explored | (live ? visible : 0));
        // Satellite coverage touches whole chunks: avoid one tree lookup per cell.
        for (int z = firstKey.second; z <= lastKey.second; ++z)
            for (int x = firstKey.first; x <= lastKey.first; ++x) {
                auto& cells = chunks_[{x, z}];
                for (int cz = std::max(first.y - z * chunkSide, 0); cz <= std::min(last.y - z * chunkSide, chunkSide - 1); ++cz)
                    for (int cx = std::max(first.x - x * chunkSide, 0); cx <= std::min(last.x - x * chunkSide, chunkSide - 1); ++cx)
                        cells[cz * chunkSide + cx] |= flags;
            }
    }
    void restoreChunk(Key key, const Chunk& exploredCells) {
        if (std::abs(static_cast<long long>(key.first)) > 65536 ||
            std::abs(static_cast<long long>(key.second)) > 65536 ||
            std::any_of(exploredCells.begin(), exploredCells.end(), [](auto cell) { return cell > explored; }) ||
            std::none_of(exploredCells.begin(), exploredCells.end(), [](auto cell) { return cell != 0; }) ||
            !chunks_.emplace(key, exploredCells).second)
            throw std::runtime_error("Invalid or duplicate visibility chunk");
    }

  private:
    static std::pair<Key, std::size_t> address(glm::ivec2 cell) {
        const auto divide = [](int value) { return value >= 0 ? value / chunkSide : (value + 1) / chunkSide - 1; };
        const Key key{divide(cell.x), divide(cell.y)};
        const int x = cell.x - key.first * chunkSide, z = cell.y - key.second * chunkSide;
        return {key, static_cast<std::size_t>(z * chunkSide + x)};
    }
    float cellSize_{3.75F};
    std::map<Key, Chunk> chunks_;
};
} // namespace strategy
