#include "terrain/TerrainTerraces.hpp"

#include "world/GenerationProgress.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <stdexcept>

namespace strategy {
namespace {
constexpr std::array<glm::ivec2, 4> neighbors{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
float quantize(float value, float step) {
    // Exact halves select the higher elevation, including negative heights.
    return std::floor(value / step + 0.5F) * step;
}
} // namespace
std::size_t TerrainTerraces::index(int x, int z) const {
    return static_cast<std::size_t>(std::clamp(z, 0, side_ - 1) * side_ +
                                    std::clamp(x, 0, side_ - 1));
}
std::uint32_t TerrainTerraces::isolatedCount() const {
    std::uint32_t result = 0;
    for (int z = 1; z < side_ - 1; ++z)
        for (int x = 1; x < side_ - 1; ++x) {
            const float h = cells_[index(x, z)].elevation;
            bool above = true, below = true;
            for (auto offset : neighbors) {
                const float n = cells_[index(x + offset.x, z + offset.y)].elevation;
                above &= h > n;
                below &= h < n;
            }
            result += above || below;
        }
    return result;
}
double TerrainTerraces::localScore(int x, int z, float candidate) const {
    const auto& source = cells_[index(x, z)].source;
    const double error = (candidate - source.mean) / settings_.elevationStep;
    double score = settings_.sourceWeight * error * error;
    const auto height = [&](int cx, int cz) {
        return cx == x && cz == z ? candidate : cells_[index(cx, cz)].elevation;
    };
    for (auto offset : neighbors) {
        const int nx = x + offset.x, nz = z + offset.y;
        if (nx < 0 || nz < 0 || nx >= side_ || nz >= side_)
            continue;
        const auto& neighbor = cells_[index(nx, nz)];
        const float difference = std::abs(candidate - neighbor.elevation) / settings_.elevationStep;
        // A fixed edge cost; never reward flat component area.
        score += settings_.boundaryWeight * std::min(difference, 1.0F);
        if (source.role == TerrainCellRole::ordinary &&
            neighbor.source.role == TerrainCellRole::ordinary && difference > 1.0F)
            score += (difference - 1.0F) * (difference - 1.0F);
        // Retain the direction of broad source contours.
        if ((source.mean - neighbor.source.mean) * (candidate - neighbor.elevation) < 0.0F)
            score += settings_.boundaryWeight;
    }
    // Include every isolation term changed by this candidate, not just its own cell.
    const auto isolation = [&](int cx, int cz) {
        if (cx < 1 || cz < 1 || cx >= side_ - 1 || cz >= side_ - 1)
            return 0.0;
        if (cells_[index(cx, cz)].source.role == TerrainCellRole::peak)
            return 0.0;
        bool above = true, below = true;
        for (auto offset : neighbors) {
            above &= height(cx, cz) > height(cx + offset.x, cz + offset.y);
            below &= height(cx, cz) < height(cx + offset.x, cz + offset.y);
        }
        return above || below ? static_cast<double>(settings_.isolatedWeight) : 0.0;
    };
    score += isolation(x, z);
    for (auto offset : neighbors)
        score += isolation(x + offset.x, z + offset.y);
    return score;
}
TerrainTerraces TerrainTerraces::resolve(std::span<const TerrainCellSource> source,
                                         int side,
                                         float extent,
                                         const TerrainCellSettings& settings,
                                         WorldGenerationProgress* progress) {
    settings.validate();
    if (side < 2 || side > 2048 || !std::isfinite(extent) ||
        std::abs(extent - side * settings.side) > 0.001F ||
        source.size() != static_cast<std::size_t>(side) * side)
        throw std::runtime_error("Invalid V2 cell grid dimensions");
    TerrainTerraces result;
    result.side_ = side;
    result.extent_ = extent;
    result.settings_ = settings;
    result.cells_.reserve(source.size());
    for (const auto& sample : source) {
        if (!std::isfinite(sample.mean) || !std::isfinite(sample.minimum) ||
            !std::isfinite(sample.maximum) || sample.minimum > sample.mean ||
            sample.mean > sample.maximum || !std::isfinite(sample.gradient.x) ||
            !std::isfinite(sample.gradient.y))
            throw std::runtime_error("Invalid V2 immutable source sample");
        result.cells_.push_back({sample, quantize(sample.mean, settings.elevationStep)});
    }
    result.diagnostics_.isolatedBefore = result.isolatedCount();
    for (std::uint32_t sweep = 0; sweep < settings.sweeps; ++sweep) {
        if (progress)
            progress->throwIfCancellationRequested();
        bool changed = false;
        for (int z = 0; z < side; ++z)
            for (int x = 0; x < side; ++x) {
                auto& cell = result.cells_[result.index(x, z)];
                if (cell.source.role == TerrainCellRole::opening ||
                    cell.source.role == TerrainCellRole::route)
                    continue;
                float best = cell.elevation;
                const double originalScore = result.localScore(x, z, best);
                double bestScore = originalScore;
                const int low =
                    static_cast<int>(std::ceil(cell.source.mean / settings.elevationStep - 1.0F));
                const int high =
                    static_cast<int>(std::floor(cell.source.mean / settings.elevationStep + 1.0F));
                for (int level = low; level <= high; ++level) {
                    const float candidate = level * settings.elevationStep;
                    const double score = result.localScore(x, z, candidate);
                    // Equal scores never change the current state. Candidate order resolves other
                    // ties.
                    const float deviation = std::abs(candidate - cell.source.mean);
                    const float bestDeviation = std::abs(best - cell.source.mean);
                    if (score < originalScore - 1e-9 &&
                        (score < bestScore - 1e-9 ||
                         (std::abs(score - bestScore) <= 1e-9 &&
                          (deviation < bestDeviation ||
                           (deviation == bestDeviation && candidate < best))))) {
                        best = candidate;
                        bestScore = score;
                    }
                }
                if (best != cell.elevation) {
                    cell.elevation = best;
                    changed = true;
                    ++result.diagnostics_.changedCells;
                }
            }
        ++result.diagnostics_.sweeps;
        if (!changed)
            break;
    }
    result.diagnostics_.isolatedAfter = result.isolatedCount();
    for (const auto& cell : result.cells_) {
        const float error = std::abs(cell.elevation - cell.source.mean);
        result.diagnostics_.meanSourceError += error;
        result.diagnostics_.maximumSourceError =
            std::max(result.diagnostics_.maximumSourceError, error);
    }
    result.diagnostics_.meanSourceError /= static_cast<float>(source.size());
    result.classifyEdges();
    return result;
}
void TerrainTerraces::classifyEdges() {
    const auto classify =
        [&](const TerrainResolvedCell& a, const TerrainResolvedCell& b, bool horizontal) {
            const float difference = b.elevation - a.elevation;
            if (difference == 0.0F)
                return TerrainEdgeKind::flat;
            if (std::abs(difference) > settings_.elevationStep + 0.001F)
                return TerrainEdgeKind::cliff;
            if (a.source.role == TerrainCellRole::shore || b.source.role == TerrainCellRole::shore)
                return TerrainEdgeKind::ramp;
            if (a.source.role == TerrainCellRole::basin || b.source.role == TerrainCellRole::basin)
                return TerrainEdgeKind::cliff;
            if (a.source.role == TerrainCellRole::route || b.source.role == TerrainCellRole::route)
                return TerrainEdgeKind::ramp;
            const glm::vec2 gradient = (a.source.gradient + b.source.gradient) * 0.5F;
            const float along = horizontal ? gradient.x : gradient.y;
            const float across = horizontal ? gradient.y : gradient.x;
            if (along * difference > 0.0F && std::abs(along) > std::abs(across) * 1.25F &&
                a.source.role != TerrainCellRole::peak && b.source.role != TerrainCellRole::peak)
                return TerrainEdgeKind::ramp;
            return TerrainEdgeKind::cliff;
        };
    for (int z = 0; z < side_; ++z)
        for (int x = 0; x < side_; ++x) {
            auto& cell = cells_[index(x, z)];
            if (x + 1 < side_)
                cell.east = classify(cell, cells_[index(x + 1, z)], true);
            if (z + 1 < side_)
                cell.south = classify(cell, cells_[index(x, z + 1)], false);
            for (auto edge : {cell.east, cell.south}) {
                diagnostics_.rampEdges += edge == TerrainEdgeKind::ramp;
                diagnostics_.cliffEdges += edge == TerrainEdgeKind::cliff;
            }
        }
}
float TerrainTerraces::surfaceAt(glm::vec2 position) const {
    // Shared vertex samples close the junction between four owned edge strips.
    const glm::vec2 grid =
        (position + glm::vec2{extent_ * 0.5F}) / settings_.side - glm::vec2{0.5F};
    const int x = static_cast<int>(std::floor(grid.x)), z = static_cast<int>(std::floor(grid.y));
    const float u = grid.x - x, v = grid.y - z;
    const auto blend = [&](float fraction, TerrainEdgeKind edge) {
        const float run = edge == TerrainEdgeKind::ramp ? settings_.rampRun : settings_.cliffRun;
        const float half = run / settings_.side * 0.5F;
        return std::clamp((fraction - 0.5F + half) / (half * 2.0F), 0.0F, 1.0F);
    };
    const auto& a = cells_[index(x, z)];
    const auto& b = cells_[index(x + 1, z)];
    const auto& c = cells_[index(x, z + 1)];
    const auto& d = cells_[index(x + 1, z + 1)];
    // A cliff touching a junction retains its narrow run; it cannot become a broad diagonal ramp.
    const auto east = (a.east == TerrainEdgeKind::cliff || c.east == TerrainEdgeKind::cliff)
                          ? TerrainEdgeKind::cliff
                      : a.east == TerrainEdgeKind::ramp || c.east == TerrainEdgeKind::ramp
                          ? TerrainEdgeKind::ramp
                          : TerrainEdgeKind::flat;
    const auto south = (a.south == TerrainEdgeKind::cliff || b.south == TerrainEdgeKind::cliff)
                           ? TerrainEdgeKind::cliff
                       : a.south == TerrainEdgeKind::ramp || b.south == TerrainEdgeKind::ramp
                           ? TerrainEdgeKind::ramp
                           : TerrainEdgeKind::flat;
    const float tx = blend(u, east), tz = blend(v, south);
    const float minimum = std::min({a.elevation, b.elevation, c.elevation, d.elevation});
    const float maximum = std::max({a.elevation, b.elevation, c.elevation, d.elevation});
    const bool alternating =
        a.elevation == d.elevation && b.elevation == c.elevation && a.elevation != b.elevation;
    // An explicit eight-triangle junction fan. Alternating cliff corners meet at the lower
    // surface, rather than inventing an intermediate-height diagonal landing.
    const float center =
        alternating && (east == TerrainEdgeKind::cliff || south == TerrainEdgeKind::cliff)
            ? minimum
            : (minimum + maximum) * 0.5F;
    const bool right = tx > 0.5F, bottom = tz > 0.5F;
    const float corner =
        bottom ? (right ? d.elevation : c.elevation) : (right ? b.elevation : a.elevation);
    const float horizontal =
        bottom ? (c.elevation + d.elevation) * 0.5F : (a.elevation + b.elevation) * 0.5F;
    const float vertical =
        right ? (b.elevation + d.elevation) * 0.5F : (a.elevation + c.elevation) * 0.5F;
    const float sx = (right ? 1.0F - tx : tx) * 2.0F, sz = (bottom ? 1.0F - tz : tz) * 2.0F;
    return sx >= sz ? corner + sx * (horizontal - corner) + sz * (center - horizontal)
                    : corner + sz * (vertical - corner) + sx * (center - vertical);
}
bool TerrainTerraces::crossesCliff(glm::vec2 start, glm::vec2 end) const {
    const glm::vec2 a = (start + glm::vec2{extent_ * 0.5F}) / settings_.side;
    const glm::vec2 b = (end + glm::vec2{extent_ * 0.5F}) / settings_.side;
    if (a.x < 0 || a.y < 0 || b.x < 0 || b.y < 0 || a.x >= side_ || a.y >= side_ || b.x >= side_ ||
        b.y >= side_)
        return true;
    const auto crossing = [&](bool horizontal) {
        const float from = horizontal ? a.x : a.y, to = horizontal ? b.x : b.y;
        if (std::abs(to - from) < 1e-7F)
            return false;
        const int low = std::max(1, static_cast<int>(std::ceil(std::min(from, to))));
        const int high = std::min(side_ - 1, static_cast<int>(std::floor(std::max(from, to))));
        for (int boundary = low; boundary <= high; ++boundary) {
            const float t = (boundary - from) / (to - from);
            const float other = horizontal ? std::lerp(a.y, b.y, t) : std::lerp(a.x, b.x, t);
            // Check both incident strips at an exact corner to prohibit diagonal shortcuts.
            for (const float epsilon : {-0.00001F, 0.00001F}) {
                const int along = static_cast<int>(std::floor(other + epsilon));
                const auto& cell = horizontal ? cells_[index(boundary - 1, along)]
                                              : cells_[index(along, boundary - 1)];
                if ((horizontal ? cell.east : cell.south) == TerrainEdgeKind::cliff)
                    return true;
            }
        }
        return false;
    };
    return crossing(true) || crossing(false);
}
} // namespace strategy
