#include "world/ResourceSiteMap.hpp"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <map>
#include <stdexcept>
#include <tuple>

namespace strategy {
namespace {
constexpr int tileSize = 32;
constexpr float maximumLink = 22.0F;
constexpr float maximumFieldLink = 6.0F;
float smooth(float x) {
    x = std::clamp(x, 0.0F, 1.0F);
    return x * x * (3 - 2 * x);
}
float hash(int x, int y) {
    std::uint32_t v =
        static_cast<std::uint32_t>(x) * 374761393U ^ static_cast<std::uint32_t>(y) * 668265263U;
    v = (v ^ (v >> 13)) * 1274126177U;
    return static_cast<float>((v ^ (v >> 16)) & 65535U) / 65535.0F;
}
float noise(glm::vec2 p) {
    const int x = static_cast<int>(std::floor(p.x)), y = static_cast<int>(std::floor(p.y));
    const float u = smooth(p.x - x), v = smooth(p.y - y);
    return std::lerp(std::lerp(hash(x, y), hash(x + 1, y), u),
                     std::lerp(hash(x, y + 1), hash(x + 1, y + 1), u),
                     v);
}
// Noise can move the unit ellipse edge out by at most .265. Use a conservative margin
// before evaluating noise/sqrt; retain the original arithmetic for contributing samples.
constexpr float brushReach = 1.3F;
} // namespace

void ResourceSiteMap::clear() {
    pending_.reset();
    sources_.clear();
    pixels_.clear();
    side_ = 0;
    extent_ = 0;
}
float ResourceSiteMap::densityAt(glm::vec2 position) const {
    if (!side_ || !std::isfinite(position.x) || !std::isfinite(position.y) ||
        std::abs(position.x) >= extent_ * .5F || std::abs(position.y) >= extent_ * .5F)
        return 0;
    const auto p = (position + glm::vec2{extent_ * .5F}) / extent_ * static_cast<float>(side_);
    const auto x = std::clamp(static_cast<int>(p.x), 0, side_ - 1);
    const auto y = std::clamp(static_cast<int>(p.y), 0, side_ - 1);
    const auto index = (static_cast<std::size_t>(y) * side_ + x) * 4;
    return std::max({pixels_[index], pixels_[index + 1], pixels_[index + 2]}) / 255.0F;
}

std::vector<ResourceSiteTile> ResourceSiteMap::update(std::span<const ResourceSiteSource> input,
                                                      float extent) {
    beginUpdate(input, extent);
    if (!pending_)
        return {};
    auto tiles = pending_->tiles;
    while (!advanceUpdate(tiles.size())) {
    }
    return tiles;
}

void ResourceSiteMap::beginUpdate(std::span<const ResourceSiteSource> input, float extent) {
    if (!std::isfinite(extent) || extent < 1 || extent > 4096)
        throw std::runtime_error("Invalid resource presentation extent");
    std::vector<ResourceSiteSource> next(input.begin(), input.end());
    std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (const auto& source : next)
        if (!std::isfinite(source.position.x) || !std::isfinite(source.position.y) ||
            std::abs(source.position.x) > extent * 2 || std::abs(source.position.y) > extent * 2 ||
            !std::isfinite(source.radius) || source.radius <= 0 || source.radius > 256 ||
            source.family > 2)
            throw std::runtime_error("Invalid resource site source");
    if (std::adjacent_find(next.begin(), next.end(), [](const auto& a, const auto& b) {
            return a.id == b.id;
        }) != next.end())
        throw std::runtime_error("Duplicate resource site identity");
    const bool resized = extent_ != extent || side_ == 0;
    const bool incomplete = pending_.has_value();
    pending_.reset();
    if (!resized && !incomplete && next == sources_)
        return;
    if (resized) {
        extent_ = extent;
        side_ = std::clamp(static_cast<int>(std::ceil(extent / .75F)), 32, 1024);
        pixels_.assign(static_cast<std::size_t>(side_) * side_ * 4, 0);
    }
    const int tiles = (side_ + tileSize - 1) / tileSize;
    std::vector<bool> dirty(tiles * tiles, resized || incomplete);
    const auto mark = [&](const ResourceSiteSource& s) {
        // Includes all old/new links involving this source. Unrelated tiles stay cached.
        const float reach = maximumLink * 1.5F + s.radius * 3.2F;
        auto a = (s.position - glm::vec2{reach} + glm::vec2{extent * .5F}) / extent *
                 static_cast<float>(side_);
        auto b = (s.position + glm::vec2{reach} + glm::vec2{extent * .5F}) / extent *
                 static_cast<float>(side_);
        for (int y = std::max(0, static_cast<int>(std::floor(a.y)) / tileSize);
             y <= std::min(tiles - 1, static_cast<int>(b.y) / tileSize);
             ++y)
            for (int x = std::max(0, static_cast<int>(std::floor(a.x)) / tileSize);
                 x <= std::min(tiles - 1, static_cast<int>(b.x) / tileSize);
                 ++x)
                dirty[y * tiles + x] = true;
    };
    if (!resized) {
        for (const auto& source : sources_)
            if (std::find(next.begin(), next.end(), source) == next.end())
                mark(source);
        for (const auto& source : next)
            if (std::find(sources_.begin(), sources_.end(), source) == sources_.end())
                mark(source);
    }
    sources_ = std::move(next);
    std::vector<Brush> brushes;
    for (const auto& source : sources_) {
        const float angle = static_cast<float>(source.id % 1024) * .618034F;
        brushes.push_back({source.position,
                           {std::cos(angle), std::sin(angle)},
                           source.radius * 2.4F,
                           source.radius * 1.7F,
                           .95F,
                           source.family});
        // Compact wet cores have their own channel: field links remain cracked earth,
        // rather than turning the whole connected field into one glossy puddle.
        if (source.family == 1)
            brushes.push_back({source.position,
                               {std::cos(angle), std::sin(angle)},
                               source.radius * 1.15F,
                               source.radius * .8F,
                               1.0F,
                               3});
    }
    // The maximum eligible separation is six units for fields, 22 for legacy unassigned
    // nodes. Include an extra bucket ring: subtraction/distance rounding can accept an exact
    // limit even when one coordinate is infinitesimally across the next bucket boundary.
    using Bucket = std::tuple<std::uint64_t, std::uint8_t, int, int>;
    std::map<Bucket, std::vector<std::size_t>> buckets;
    const auto bucketOf = [](const ResourceSiteSource& source) {
        const float side = source.fieldId ? maximumFieldLink : maximumLink;
        return glm::ivec2{static_cast<int>(std::floor(source.position.x / side)),
                          static_cast<int>(std::floor(source.position.y / side))};
    };
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        const auto& source = sources_[i];
        const auto cell = bucketOf(source);
        buckets[{source.fieldId, source.family, cell.x, cell.y}].push_back(i);
    }
    std::vector<std::size_t> nearby;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        const auto& a = sources_[i];
        const auto cell = bucketOf(a);
        nearby.clear();
        for (int z = -2; z <= 2; ++z)
            for (int x = -2; x <= 2; ++x) {
                const auto found = buckets.find({a.fieldId, a.family, cell.x + x, cell.y + z});
                if (found != buckets.end())
                    for (const auto j : found->second)
                        if (j > i)
                            nearby.push_back(j);
            }
        // Preserve the previous stable (source ID, source ID) brush order.
        std::sort(nearby.begin(), nearby.end());
        for (const auto j : nearby) {
            const auto& b = sources_[j];
            const float distance = glm::distance(a.position, b.position);
            if (distance < .01F ||
                distance > (a.fieldId ? std::min(maximumFieldLink, (a.radius + b.radius) * 3)
                                      : maximumLink))
                continue;
            const float radius = std::min(a.radius, b.radius);
            brushes.push_back({(a.position + b.position) * .5F,
                               (b.position - a.position) / distance,
                               distance * .5F + radius * 1.8F,
                               radius * 1.65F,
                               .68F,
                               a.family});
        }
    }
    for (auto& brush : brushes) {
        const auto direction = glm::abs(brush.direction);
        brush.halfBounds =
            (direction * brush.length + glm::vec2{direction.y, direction.x} * brush.width) *
            brushReach;
    }
    PendingUpdate job;
    job.brushes = std::move(brushes);
    for (int ty = 0; ty < tiles; ++ty)
        for (int tx = 0; tx < tiles; ++tx) {
            if (!dirty[ty * tiles + tx])
                continue;
            ResourceSiteTile tile{tx * tileSize,
                                  ty * tileSize,
                                  std::min(tileSize, side_ - tx * tileSize),
                                  std::min(tileSize, side_ - ty * tileSize)};
            job.tiles.push_back(tile);
        }
    if (!job.tiles.empty())
        pending_ = std::move(job);
}

float ResourceSiteMap::updateProgress() const {
    return pending_ ? static_cast<float>(pending_->next) / pending_->tiles.size() : 1.F;
}

bool ResourceSiteMap::advanceUpdate(std::size_t maximumTiles) {
    if (!pending_)
        return true;
    const auto end =
        std::min(pending_->tiles.size(),
                 pending_->next + std::min(maximumTiles, pending_->tiles.size() - pending_->next));
    for (; pending_->next < end; ++pending_->next) {
        const auto& tile = pending_->tiles[pending_->next];
        const float extent = extent_;
        const glm::vec2 center{(tile.x + tile.width * .5F) / side_ * extent - extent * .5F,
                               (tile.y + tile.height * .5F) / side_ * extent - extent * .5F};
        const float halfDiagonal = std::sqrt(2.0F) * tileSize * .5F / side_ * extent;
        std::vector<const Brush*> local;
        for (const auto& brush : pending_->brushes)
            if (glm::distance(center, brush.center) <
                std::max(brush.length, brush.width) * 1.3F + halfDiagonal)
                local.push_back(&brush);
        for (int y = tile.y; y < tile.y + tile.height; ++y)
            for (int x = tile.x; x < tile.x + tile.width; ++x) {
                const glm::vec2 p{(x + .5F) / side_ * extent - extent * .5F,
                                  (y + .5F) / side_ * extent - extent * .5F};
                float density[4]{};
                float irregular = 0;
                bool noiseReady = false;
                for (const auto* brush : local) {
                    const auto d = p - brush->center;
                    const auto absolute = glm::abs(d);
                    if (absolute.x > brush->halfBounds.x || absolute.y > brush->halfBounds.y)
                        continue;
                    const float along = glm::dot(d, brush->direction) / brush->length;
                    const float across =
                        glm::dot(d, glm::vec2{-brush->direction.y, brush->direction.x}) /
                        brush->width;
                    const float squared = along * along + across * across;
                    if (squared > brushReach * brushReach)
                        continue;
                    if (!noiseReady) {
                        irregular = (noise(p * .27F) - .5F) * .4F + (noise(p * .71F) - .5F) * .13F;
                        noiseReady = true;
                    }
                    const float value =
                        smooth((1 - std::sqrt(squared) + irregular) * 1.8F) * brush->strength;
                    density[brush->family] = std::max(density[brush->family], value);
                }
                const auto index = (static_cast<std::size_t>(y) * side_ + x) * 4;
                for (int channel = 0; channel < 4; ++channel)
                    pixels_[index + channel] =
                        static_cast<std::uint8_t>(std::clamp(density[channel], 0.0F, 1.0F) * 255);
            }
    }
    if (pending_->next != pending_->tiles.size())
        return false;
    pending_.reset();
    ++revision_;
    return true;
}
} // namespace strategy
