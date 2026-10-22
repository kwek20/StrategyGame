#include "terrain/TerrainV2Access.hpp"

#include "world/GenerationProgress.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace strategy {
void connectV2Landscape(TerrainV2Plan& plan,
                        std::span<TerrainCellSource> source,
                        int side,
                        WorldGenerationProgress* progress) {
    const auto& settings = plan.recipe().cells;
    if (side < 2 || side > 2048 || source.size() != static_cast<std::size_t>(side) * side ||
        std::abs(plan.extent() - side * settings.side) > 0.001F)
        throw std::runtime_error("Invalid V2 connection grid dimensions");
    const auto point = [&](int index) {
        return -glm::vec2{plan.extent() * 0.5F} +
               (glm::vec2{index % side, index / side} + glm::vec2{0.5F}) * settings.side;
    };
    const auto index = [&](glm::vec2 position) {
        const auto cell = (position + glm::vec2{plan.extent() * 0.5F}) / settings.side;
        return std::clamp(static_cast<int>(cell.y), 0, side - 1) * side +
               std::clamp(static_cast<int>(cell.x), 0, side - 1);
    };
    std::vector<std::uint8_t> dry(source.size()), access(source.size());
    for (std::size_t i = 0; i < source.size(); ++i)
        dry[i] = !plan.hasWater() ||
                 plan.waterDistance(point(static_cast<int>(i))) > settings.rampRun * 2;
    constexpr std::array<glm::ivec2, 8> neighbors{
        {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
    for (std::size_t player = 1; player < plan.openings().size(); ++player) {
        const int start = index(plan.openings()[player]);
        const int goal = index(plan.openings()[0]);
        std::vector<float> cost(source.size(), std::numeric_limits<float>::infinity());
        std::vector<int> parent(source.size(), -1);
        using Entry = std::pair<float, int>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pending;
        cost[start] = 0;
        pending.emplace(0.0F, start);
        while (!pending.empty()) {
            const auto [distance, current] = pending.top();
            pending.pop();
            if (distance != cost[current])
                continue;
            if (progress && current % 256 == 0)
                progress->throwIfCancellationRequested();
            if (current == goal)
                break;
            const int x = current % side, z = current / side;
            for (auto offset : neighbors) {
                const int nx = x + offset.x, nz = z + offset.y;
                if (nx < 1 || nz < 1 || nx >= side - 1 || nz >= side - 1)
                    continue;
                const int next = nz * side + nx;
                if (!dry[next] || !dry[z * side + nx] || !dry[nz * side + x])
                    continue;
                const float rise =
                    std::abs(source[next].mean - source[current].mean) / settings.side;
                // Prefer existing valleys and gentle ground; steep ridges are expensive,
                // not absolute barriers that could invalidate a supported seed.
                const float step = (offset.x && offset.y ? std::sqrt(2.0F) : 1.0F) *
                                   (1 + rise * rise * 24 + (access[next] ? 0 : rise * 2));
                const float candidate = distance + step;
                if (candidate < cost[next]) {
                    cost[next] = candidate;
                    parent[next] = current;
                    pending.emplace(candidate, next);
                }
            }
        }
        if (parent[goal] < 0)
            throw std::runtime_error("V2 basin leaves no feasible dry connection");
        std::vector<int> path;
        for (int cell = goal; cell >= 0; cell = parent[cell]) {
            path.push_back(cell);
            if (cell == start)
                break;
        }
        std::vector<float> heights;
        heights.reserve(path.size());
        for (int cell : path)
            heights.push_back(std::round(source[cell].mean / settings.elevationStep) *
                              settings.elevationStep);
        // Preserve contour-following heights, easing only excessive rises. Both opening
        // endpoints remain on their protected plane, and each connection stays continuous.
        for (std::size_t i = 1; i < heights.size(); ++i)
            heights[i] = std::clamp(heights[i],
                                    heights[i - 1] - settings.elevationStep,
                                    heights[i - 1] + settings.elevationStep);
        heights.back() = plan.recipe().baseHeight;
        for (std::size_t i = heights.size() - 1; i > 0; --i)
            heights[i - 1] = std::clamp(heights[i - 1],
                                        heights[i] - settings.elevationStep,
                                        heights[i] + settings.elevationStep);
        const int radius =
            std::max(1, static_cast<int>(std::ceil(settings.rampRun / settings.side)));
        for (std::size_t i = 0; i < path.size(); ++i) {
            const int x = path[i] % side, z = path[i] / side;
            for (int dz = -radius; dz <= radius; ++dz)
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= side || nz >= side)
                        continue;
                    const int cell = nz * side + nx;
                    if (!dry[cell] || source[cell].role == TerrainCellRole::opening)
                        continue;
                    // Union strips in path order can otherwise introduce a two-step edge
                    // at bends. Clamp each accepted sample against the connection profile.
                    auto& sample = source[cell];
                    if (!access[cell]) {
                        const float natural = std::round(sample.mean / settings.elevationStep) *
                                              settings.elevationStep;
                        sample.mean = sample.minimum = sample.maximum =
                            std::clamp(natural,
                                       heights[i] - settings.elevationStep,
                                       heights[i] + settings.elevationStep);
                        sample.role = TerrainCellRole::route;
                        access[cell] = 1;
                    }
                }
        }
        // The actual centre samples retain the continuous contour profile. Nearby
        // terrain retains its own height wherever it already meets that profile.
        for (std::size_t i = 0; i < path.size(); ++i) {
            auto& sample = source[path[i]];
            if (sample.role != TerrainCellRole::opening)
                sample.mean = sample.minimum = sample.maximum = heights[i];
        }
    }
    for (int z = 1; z + 1 < side; ++z)
        for (int x = 1; x + 1 < side; ++x) {
            const int i = z * side + x;
            if (access[i])
                source[i].gradient = {
                    (source[i + 1].mean - source[i - 1].mean) / (settings.side * 2),
                    (source[i + side].mean - source[i - side].mean) / (settings.side * 2)};
        }
    plan.setAccessMask(side, std::move(access));
}
} // namespace strategy
