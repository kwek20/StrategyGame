#include "terrain/TerrainV2Plan.hpp"

#include "simulation/DeterministicRandom.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <numbers>

namespace strategy {
namespace {
float segmentDistanceSquared(glm::vec2 point, const TerrainRoute& route) {
    const auto direction = route.end - route.start;
    const float t = std::clamp(
        glm::dot(point - route.start, direction) / glm::dot(direction, direction), 0.0F, 1.0F);
    const auto delta = point - route.start - t * direction;
    return glm::dot(delta, delta);
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
    DeterministicRandom reliefRandom{seed, "terrain.v2.relief"};
    result.relief_ =
        reliefRandom.range(result.recipe_.relief.minimum, result.recipe_.relief.maximum);
    const float inset = extent * 0.5F - result.recipe_.openingInset;
    const auto align = [&](float value) {
        return std::floor((value + extent * 0.5F) / result.recipe_.cells.side) *
                   result.recipe_.cells.side -
               extent * 0.5F + result.recipe_.cells.side * 0.5F;
    };
    const float low = align(-inset), high = align(inset);
    const std::vector<glm::vec2> corners{{low, low}, {high, high}, {high, low}, {low, high}};
    result.openings_.assign(corners.begin(), corners.begin() + players);
    const std::vector<glm::vec2> ring{{low, low}, {high, low}, {high, high}, {low, high}};
    DeterministicRandom routeRandom{seed, "terrain.v2.routes"};
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const auto start = ring[i], end = ring[(i + 1) % ring.size()];
        const auto midpoint = (start + end) * 0.5F;
        const auto control = midpoint + glm::normalize(midpoint) * result.recipe_.routeWidth *
                                            routeRandom.range(1.0F, 2.0F);
        auto previous = start;
        constexpr int segments = 6;
        for (int j = 1; j <= segments; ++j) {
            const float t = static_cast<float>(j) / segments;
            const auto next = (1 - t) * (1 - t) * start + 2 * (1 - t) * t * control + t * t * end;
            result.routes_.push_back({previous, next});
            previous = next;
        }
    }
    if (result.recipe_.waterPolicy != TerrainWaterPolicy::forbidden) {
        DeterministicRandom waterRandom{seed, "terrain.v2.water"};
        const float coverage = waterRandom.range(result.recipe_.waterCoverage.minimum,
                                                 result.recipe_.waterCoverage.maximum);
        if (coverage > 0.0F) {
            result.lakeRadius_ = std::sqrt(coverage / std::numbers::pi_v<float>) * extent;
            result.lakeRadius_ = std::min(
                result.lakeRadius_, inset - result.recipe_.routeWidth - result.recipe_.cells.side);
            result.lakePhase_ = waterRandom.range(0.0F, 2.0F * std::numbers::pi_v<float>);
            for (std::uint32_t i = 0; i < result.recipe_.isletCount; ++i) {
                const float angle = result.lakePhase_ + 2.0F * std::numbers::pi_v<float> * i /
                                                            result.recipe_.isletCount;
                const float radius = result.lakeRadius_ * 0.36F;
                const glm::vec2 center{std::cos(angle) * radius, std::sin(angle) * radius};
                if (result.waterDistance(center) < -result.recipe_.isletRadius * 2)
                    result.islets_.push_back(center);
            }
        }
    }
    DeterministicRandom peakRandom{seed, "terrain.v2.landmarks"};
    for (std::uint32_t i = 0; i < result.recipe_.peakCount; ++i) {
        const float angle = peakRandom.range(0, 2.0F * std::numbers::pi_v<float>);
        const float radius = extent * peakRandom.range(0.32F, 0.42F);
        const glm::vec2 center{std::cos(angle) * radius, std::sin(angle) * radius};
        if (result.openingInfluence(center) == 0 && !result.reservedRoute(center) &&
            result.waterDistance(center) > result.recipe_.peakRadius)
            result.peaks_.push_back(center);
    }
    return result;
}
TerrainV2Plan TerrainV2Plan::constructiveFallback() const {
    auto result = *this;
    result.peaks_.clear();
    result.islets_.clear();
    result.relief_ = 0;
    if (result.hasWater()) {
        result.lakePhase_ = 0;
        const float coverage =
            (recipe_.waterCoverage.minimum + recipe_.waterCoverage.maximum) * 0.5F;
        result.lakeRadius_ = std::min(std::sqrt(coverage / std::numbers::pi_v<float>) * extent_,
                                      extent_ * 0.5F - recipe_.openingInset - recipe_.routeWidth -
                                          recipe_.cells.side);
    }
    return result;
}

float TerrainV2Plan::isletInfluence(glm::vec2 position) const {
    float result = 0;
    for (auto center : islets_) {
        const float t =
            std::clamp(1.0F - glm::length(position - center) / recipe_.isletRadius, 0.0F, 1.0F);
        result = std::max(result, t * t * (3.0F - 2.0F * t));
    }
    return result;
}
float TerrainV2Plan::peakInfluence(glm::vec2 position) const {
    float influence = 0;
    for (auto peak : peaks_) {
        const float t =
            std::clamp(1.0F - glm::length(position - peak) /
                                  (recipe_.peakRadius *
                                   (1.0F + 0.18F * std::sin(3.0F * std::atan2(position.y - peak.y,
                                                                              position.x - peak.x) +
                                                            lakePhase_))),
                       0.0F,
                       1.0F);
        influence = std::max(influence, t * t * (3.0F - 2.0F * t));
    }
    if (influence == 0.0F)
        return 0.0F;
    return influence * (1.0F - openingInfluence(position)) * (1.0F - routeInfluence(position));
}
float TerrainV2Plan::openingInfluence(glm::vec2 position) const {
    float influence = 0.0F;
    for (const auto opening : openings_) {
        const float distance = glm::length(position - opening);
        // Reserve complete sampling cells around the usable envelope. A boundary cell's
        // source average cannot lift the outer processor sites above the opening plane.
        const float samplePadding = recipe_.cells.side * std::sqrt(0.5F);
        const float t = std::clamp((distance - recipe_.openingRadius - samplePadding) /
                                       recipe_.openingBlendWidth,
                                   0.0F,
                                   1.0F);
        influence = std::max(influence, 1.0F - t * t * (3.0F - 2.0F * t));
    }
    return influence;
}
float TerrainV2Plan::routeInfluence(glm::vec2 position) const {
    // All corridor controls bow outwards from the opening ring. Interior samples
    // beyond the full falloff cannot reach any segment, regardless of the seed.
    const float interior =
        extent_ * 0.5F - recipe_.openingInset - recipe_.routeWidth * 1.5F - recipe_.cells.side;
    if (std::abs(position.x) < interior && std::abs(position.y) < interior)
        return 0;
    float distance = std::numeric_limits<float>::infinity();
    for (const auto& route : routes_)
        distance = std::min(distance, segmentDistanceSquared(position, route));
    const float t = std::clamp(
        (std::sqrt(distance) - recipe_.routeWidth * 0.5F) / recipe_.routeWidth, 0.0F, 1.0F);
    return 1.0F - t * t * (3.0F - 2.0F * t);
}
bool TerrainV2Plan::reservedRoute(glm::vec2 position) const {
    return std::any_of(routes_.begin(), routes_.end(), [&](const auto& route) {
        return segmentDistanceSquared(position, route) <=
               recipe_.routeWidth * recipe_.routeWidth * 0.25F;
    });
}
float TerrainV2Plan::waterDistance(glm::vec2 position) const {
    if (!hasWater())
        return std::numeric_limits<float>::infinity();
    const float angle = std::atan2(position.y, position.x);
    const float radius = lakeRadius_ * (1.0F + 0.18F * std::sin(3.0F * angle + lakePhase_) +
                                        0.12F * std::sin(5.0F * angle - lakePhase_) +
                                        0.055F * std::sin(9.0F * angle + lakePhase_));
    return glm::length(position) - radius;
}
} // namespace strategy
