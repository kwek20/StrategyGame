#include "terrain/TerrainLayoutGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace strategy {
namespace {

float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

std::uint32_t hash(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

float unit(std::uint32_t value) {
    return static_cast<float>(hash(value) & 0x00FFFFFFU) / 16777215.0F;
}

} // namespace

void TerrainLayoutGenerator::apply(std::span<float> heights, int side, std::uint32_t seed,
                                   const TerrainLayoutDefinition& layout, float waterLevel) {
    if (layout.shape == TerrainLayoutShape::natural) return;
    struct Island { float x; float z; float radius; };
    std::vector<Island> islands;
    if (layout.shape == TerrainLayoutShape::islands) {
        islands.reserve(layout.islandCount);
        for (std::uint32_t index = 0; index < layout.islandCount; ++index)
            islands.push_back({unit(seed ^ (index * 3U + 1U)) * 1.5F - 0.75F,
                               unit(seed ^ (index * 3U + 2U)) * 1.5F - 0.75F,
                               layout.featureRadius * (0.65F +
                                   unit(seed ^ (index * 3U + 3U)) * 0.55F)});
    }
    for (int z = 0; z < side; ++z) {
        const float nz = static_cast<float>(z) / (side - 1) * 2.0F - 1.0F;
        for (int x = 0; x < side; ++x) {
            const float nx = static_cast<float>(x) / (side - 1) * 2.0F - 1.0F;
            float& height = heights[static_cast<std::size_t>(z * side + x)];
            const float radius = std::sqrt(nx * nx + nz * nz);
            if (layout.shape == TerrainLayoutShape::plains) {
                height = std::lerp(height, waterLevel + 0.15F, layout.featureStrength);
            } else if (layout.shape == TerrainLayoutShape::hills) {
                const float waves = std::sin((nx + nz) * 5.0F * std::numbers::pi_v<float> +
                                             unit(seed) * 6.0F) * 0.5F + 0.5F;
                height += (waves - 0.5F) * layout.featureStrength;
            } else if (layout.shape == TerrainLayoutShape::centralHill) {
                height += (1.0F - smoothstep(0.0F, layout.featureRadius, radius)) *
                          layout.featureStrength;
            } else if (layout.shape == TerrainLayoutShape::centralWater) {
                height -= (1.0F - smoothstep(0.0F, layout.featureRadius, radius)) *
                          layout.featureStrength;
            } else if (layout.shape == TerrainLayoutShape::islands) {
                float landMask = 0.0F;
                for (const Island& island : islands) {
                    const float distance = std::hypot(nx - island.x, nz - island.z);
                    landMask = std::max(landMask,
                                        1.0F - smoothstep(island.radius * 0.35F,
                                                          island.radius, distance));
                }
                const float islandHeight = waterLevel - 0.08F +
                                           landMask * (0.16F + layout.featureStrength);
                height = std::lerp(height, islandHeight, 0.82F);
            }
            height = std::clamp(height, 0.0F, 1.0F);
        }
    }
}

} // namespace strategy
