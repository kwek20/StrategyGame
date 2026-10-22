#include "terrain/TerrainV2Plan.hpp"

#include "simulation/DeterministicRandom.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace strategy {
namespace {
constexpr float tau = 2.0F * std::numbers::pi_v<float>;
float smooth(float t) {
    t = std::clamp(t, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}
glm::vec2 rotate(glm::vec2 point, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {c * point.x - s * point.y, s * point.x + c * point.y};
}
} // namespace

TerrainV2Plan TerrainV2Plan::create(std::uint32_t seed,
                                    TerrainV2Recipe recipe,
                                    float extent,
                                    std::size_t players) {
    if (recipe.waterPolicy != TerrainWaterPolicy::forbidden)
        recipe.waterCoverage = recipe.waterCoverage.intersect({0, 0.5F});
    recipe.validate(extent, players);
    TerrainV2Plan result;
    result.recipe_ = std::move(recipe);
    result.extent_ = extent;
    DeterministicRandom relief{seed, "terrain.v2.relief"};
    result.relief_ = relief.range(result.recipe_.relief.minimum, result.recipe_.relief.maximum);
    DeterministicRandom composition{seed, "terrain.v2.composition"};
    result.fieldAngle_ = composition.range(0, tau);
    result.fieldOffset_ = {composition.range(-extent, extent), composition.range(-extent, extent)};
    result.fieldStretch_ =
        composition.range(result.recipe_.ridgeWidth.minimum, result.recipe_.ridgeWidth.maximum) /
        ((result.recipe_.ridgeWidth.minimum + result.recipe_.ridgeWidth.maximum) * 0.5F);
    if (result.recipe_.waterPolicy != TerrainWaterPolicy::forbidden) {
        DeterministicRandom water{seed, "terrain.v2.water"};
        const float coverage =
            water.range(result.recipe_.waterCoverage.minimum, result.recipe_.waterCoverage.maximum);
        if (coverage > 0) {
            result.lakeAspect_ =
                water.range(result.recipe_.lakeAspect.minimum, result.recipe_.lakeAspect.maximum);
            result.lakeRotation_ = water.range(0, tau);
            result.lakePhase_ = water.range(0, tau);
            result.lakeCenter_ = {water.range(-extent * 0.025F, extent * 0.025F),
                                  water.range(-extent * 0.025F, extent * 0.025F)};
            result.lakeRadius_ = std::sqrt(coverage / std::numbers::pi_v<float>) * extent;
            // Bound the whole irregular basin against the map, not the old corner route ring.
            const float shoreLimit = extent * 0.5F - result.recipe_.bankWidth -
                                     glm::length(result.lakeCenter_) - result.recipe_.cells.side;
            result.lakeRadius_ = std::min(
                result.lakeRadius_,
                shoreLimit /
                    (1.355F * std::sqrt(std::max(result.lakeAspect_, 1.0F / result.lakeAspect_))));
            for (std::uint32_t i = 0; i < result.recipe_.isletCount; ++i) {
                const float angle = water.range(0, tau);
                const float radius = result.lakeRadius_ * water.range(0.18F, 0.52F);
                const glm::vec2 center = result.lakeCenter_ + rotate({std::cos(angle) * radius,
                                                                      std::sin(angle) * radius},
                                                                     result.lakeRotation_);
                if (result.waterDistance(center) < -result.recipe_.isletRadius * 2)
                    result.islets_.push_back(center);
            }
        }
    }
    const float margin = result.recipe_.openingRadius + result.recipe_.cells.side * 2;
    const float limit = extent * 0.5F - margin;
    const auto align = [&](float value) {
        return std::floor((value + extent * 0.5F) / result.recipe_.cells.side) *
                   result.recipe_.cells.side -
               extent * 0.5F + result.recipe_.cells.side * 0.5F;
    };
    DeterministicRandom starts{seed, "terrain.v2.openings"};
    for (std::size_t player = 0; player < players; ++player) {
        bool accepted = false;
        for (int attempt = 0; attempt < 512; ++attempt) {
            const glm::vec2 candidate{align(starts.range(-limit, limit)),
                                      align(starts.range(-limit, limit))};
            if (result.waterDistance(candidate) < margin)
                continue;
            if (std::any_of(result.openings_.begin(), result.openings_.end(), [&](glm::vec2 other) {
                    return glm::distance(candidate, other) <
                           extent * 0.5F + result.recipe_.openingSeparationPadding;
                }))
                continue;
            result.openings_.push_back(candidate);
            accepted = true;
            break;
        }
        if (!accepted) {
            // Finite placement escalation: this capacity arrangement fits the supported basin.
            const float low = align(-limit), high = align(limit);
            const std::vector<glm::vec2> fallback{
                {low, low}, {high, high}, {high, low}, {low, high}};
            result.openings_.assign(fallback.begin(), fallback.begin() + players);
            break;
        }
    }
    DeterministicRandom ridges{seed, "terrain.v2.landmarks"};
    const auto groups = static_cast<std::uint32_t>(std::lround(
        std::ceil(result.recipe_.peakCount * 0.4F) * (extent * extent / (240.0F * 240.0F))));
    for (std::uint32_t i = 0; i < groups; ++i) {
        const float angle = ridges.range(0, tau);
        const float width =
            ridges.range(result.recipe_.ridgeWidth.minimum, result.recipe_.ridgeWidth.maximum);
        glm::vec2 center{};
        bool placed = false;
        const float landmarkLimit = extent * 0.5F - width;
        for (int attempt = 0; attempt < 32 && !placed; ++attempt) {
            center = {ridges.range(-landmarkLimit, landmarkLimit),
                      ridges.range(-landmarkLimit, landmarkLimit)};
            placed =
                result.openingInfluence(center) == 0 && result.waterDistance(center) >= width &&
                std::none_of(result.ridges_.begin(), result.ridges_.end(), [&](const auto& ridge) {
                    return glm::distance(center, ridge.center) < (width + ridge.width) * 1.2F;
                });
        }
        if (!placed)
            continue;
        const std::size_t first = result.ridges_.size();
        result.ridges_.push_back(
            {center,
             {std::cos(angle), std::sin(angle)},
             ridges.range(result.recipe_.ridgeLength.minimum, result.recipe_.ridgeLength.maximum),
             width,
             ridges.range(result.recipe_.ridgeHeightScale.minimum,
                          result.recipe_.ridgeHeightScale.maximum),
             ridges.range(result.recipe_.ridgeBend.minimum, result.recipe_.ridgeBend.maximum),
             ridges.range(0, tau),
             ridges.range(0, 1) < 0.5F ? -1.0F : 1.0F});
        // Overlapping segments share a crest, rather than meeting at their zero-height tips.
        // Each arm bends independently; a chain's scarp remains on the same side.
        for (int arm : {-1, 1}) {
            const auto parent = result.ridges_[first];
            const auto direction = rotate(parent.direction, ridges.range(-0.55F, 0.55F));
            const float length = parent.length * ridges.range(0.75F, 1.05F);
            const auto nextCenter =
                parent.center +
                (parent.direction * parent.length + direction * length) * (0.30F * arm);
            if (std::abs(nextCenter.x) > extent * 0.5F || std::abs(nextCenter.y) > extent * 0.5F ||
                result.openingInfluence(nextCenter) > 0.8F || result.waterDistance(nextCenter) < 0)
                continue;
            result.ridges_.push_back({nextCenter,
                                      direction,
                                      length,
                                      width * 0.85F,
                                      parent.heightScale * 0.85F,
                                      -parent.bend,
                                      parent.phase + arm,
                                      parent.scarpSide});
        }
        const auto parent = result.ridges_[first];
        if (parent.phase > std::numbers::pi_v<float>) {
            const auto direction = rotate(parent.direction, parent.scarpSide * 0.85F);
            result.ridges_.push_back({parent.center + direction * parent.length * 0.23F,
                                      direction,
                                      parent.length * 0.7F,
                                      width * 0.65F,
                                      parent.heightScale * 0.65F,
                                      parent.bend,
                                      parent.phase + 1,
                                      parent.scarpSide});
        }
    }
    return result;
}

TerrainV2Plan TerrainV2Plan::constructiveFallback() const {
    auto result = *this;
    result.ridges_.clear();
    result.islets_.clear();
    result.relief_ = 0;
    result.lakePhase_ = 0;
    result.lakeAspect_ = 1;
    result.lakeRotation_ = 0;
    result.lakeCenter_ = {0, 0};
    if (result.hasWater()) {
        const float coverage =
            (recipe_.waterCoverage.minimum + recipe_.waterCoverage.maximum) * 0.5F;
        float capacityRadius = extent_ * 0.5F - recipe_.bankWidth;
        for (auto opening : openings_)
            capacityRadius = std::min(
                capacityRadius, glm::length(opening) - recipe_.openingRadius - recipe_.bankWidth);
        result.lakeRadius_ = std::max(
            recipe_.cells.side,
            std::min(std::sqrt(coverage / std::numbers::pi_v<float>) * extent_, capacityRadius));
    }
    return result;
}

glm::vec2 TerrainV2Plan::fieldPosition(glm::vec2 position) const {
    auto p = rotate(position, fieldAngle_);
    p.x *= fieldStretch_;
    p.y /= fieldStretch_;
    return p + fieldOffset_;
}

float TerrainV2Plan::isletInfluence(glm::vec2 position) const {
    float result = 0;
    for (auto center : islets_)
        result =
            std::max(result, smooth(1.0F - glm::distance(position, center) / recipe_.isletRadius));
    return result;
}

float TerrainRidge::influence(glm::vec2 position) const {
    const auto delta = position - center;
    const float along = glm::dot(delta, direction) / (length * 0.5F);
    const glm::vec2 perpendicular{-direction.y, direction.x};
    if (std::abs(along) >= 1)
        return 0;
    const float bendOffset = std::cos(along * std::numbers::pi_v<float> * 0.5F) * bend * width;
    const float across = (glm::dot(delta, perpendicular) - bendOffset) / width * scarpSide;
    const float envelope = smooth((1.0F - std::abs(along)) / 0.35F);
    // A broad climbing back meets a narrow, near-vertical escarpment. Crest height
    // varies along the chain, while the connected shoulder remains above the valley.
    const float back = smooth((across + 1.4F) / 1.4F);
    const float face = 1.0F - smooth((across - 0.08F) / 0.22F);
    const float peaks = 0.82F + 0.18F * std::cos(along * 8 + phase);
    return envelope * back * face * peaks * heightScale;
}

float TerrainV2Plan::peakInfluence(glm::vec2 position) const {
    const float land = hasWater() ? smooth(waterDistance(position) / recipe_.bankWidth) : 1.0F;
    if (land == 0)
        return 0;
    float influence = 0;
    for (const auto& ridge : ridges_)
        influence = std::max(influence, ridge.influence(position));
    return influence * land * (1.0F - openingInfluence(position));
}

float TerrainV2Plan::openingInfluence(glm::vec2 position) const {
    float influence = 0;
    for (auto opening : openings_) {
        const float padding = recipe_.cells.side * std::sqrt(0.5F);
        const float t = (glm::distance(position, opening) - recipe_.openingRadius - padding) /
                        recipe_.openingBlendWidth;
        influence = std::max(influence, 1.0F - smooth(t));
    }
    return influence;
}

void TerrainV2Plan::setAccessMask(int side, std::vector<std::uint8_t> mask) {
    if (side < 1 || mask.size() != static_cast<std::size_t>(side) * side)
        throw std::runtime_error("Invalid V2 access mask");
    accessSide_ = side;
    accessMask_ = std::move(mask);
}
bool TerrainV2Plan::reservedRoute(glm::vec2 position) const {
    if (accessMask_.empty() || std::abs(position.x) >= extent_ * 0.5F ||
        std::abs(position.y) >= extent_ * 0.5F)
        return false;
    const auto p = (position + glm::vec2{extent_ * 0.5F}) / recipe_.cells.side;
    return accessMask_[static_cast<std::size_t>(static_cast<int>(p.y) * accessSide_ +
                                                static_cast<int>(p.x))] != 0;
}
float TerrainV2Plan::routeInfluence(glm::vec2 position) const {
    return reservedRoute(position) ? 1.0F : 0.0F;
}
float TerrainV2Plan::waterDistance(glm::vec2 position) const {
    if (!hasWater())
        return std::numeric_limits<float>::infinity();
    const auto delta = rotate(position - lakeCenter_, -lakeRotation_);
    const glm::vec2 p{delta.x / std::sqrt(lakeAspect_), delta.y * std::sqrt(lakeAspect_)};
    const float angle = std::atan2(p.y, p.x);
    const float radius = lakeRadius_ * (1 + 0.18F * std::sin(3 * angle + lakePhase_) +
                                        0.12F * std::sin(5 * angle - lakePhase_) +
                                        0.055F * std::sin(9 * angle + lakePhase_));
    return glm::length(p) - radius;
}
} // namespace strategy
