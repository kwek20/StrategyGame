#include "terrain/Terrain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace strategy {
namespace {

float fade(float value) {
    return value * value * value * (value * (value * 6.0F - 15.0F) + 10.0F);
}

std::uint32_t hashCoordinates(int x, int z, std::uint32_t seed) {
    std::uint32_t hash = seed;
    hash ^= static_cast<std::uint32_t>(x) * 0x9E3779B9U;
    hash = (hash << 13U) | (hash >> 19U);
    hash ^= static_cast<std::uint32_t>(z) * 0x85EBCA6BU;
    hash ^= hash >> 16U;
    hash *= 0x7FEB352DU;
    hash ^= hash >> 15U;
    hash *= 0x846CA68BU;
    return hash ^ (hash >> 16U);
}

float randomSigned(int x, int z, std::uint32_t seed) {
    return static_cast<float>(hashCoordinates(x, z, seed) & 0x00FFFFFFU)
         / static_cast<float>(0x007FFFFFU) - 1.0F;
}

float smoothstep(float edge0, float edge1, float value) {
    const float t = glm::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

} // namespace

Terrain::Terrain(std::uint32_t seed) {
    generate(seed);
}

float Terrain::valueNoise(float x, float z, std::uint32_t seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const float tx = fade(x - static_cast<float>(x0));
    const float tz = fade(z - static_cast<float>(z0));
    const float top = glm::mix(randomSigned(x0, z0, seed),
                               randomSigned(x0 + 1, z0, seed), tx);
    const float bottom = glm::mix(randomSigned(x0, z0 + 1, seed),
                                  randomSigned(x0 + 1, z0 + 1, seed), tx);
    return glm::mix(top, bottom, tz);
}

float Terrain::fractalNoise(float x, float z, std::uint32_t seed,
                            int octaves, float persistence) {
    float total = 0.0F;
    float amplitude = 1.0F;
    float frequency = 1.0F;
    float amplitudeSum = 0.0F;
    for (int octave = 0; octave < octaves; ++octave) {
        total += valueNoise(x * frequency, z * frequency,
                            seed + static_cast<std::uint32_t>(octave) * 1013U)
               * amplitude;
        amplitudeSum += amplitude;
        amplitude *= persistence;
        frequency *= 2.0F;
    }
    return total / amplitudeSum;
}

void Terrain::generate(std::uint32_t seed) {
    heights_.resize(static_cast<std::size_t>(vertexCount * vertexCount));
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();

    for (int z = 0; z < vertexCount; ++z) {
        for (int x = 0; x < vertexCount; ++x) {
            const float nx = static_cast<float>(x) / static_cast<float>(cellCount);
            const float nz = static_cast<float>(z) / static_cast<float>(cellCount);

            // Domain warping supplies the old turbulence stage.
            const float warpX = fractalNoise(nx * 2.2F, nz * 2.2F, seed + 17U, 3, 0.5F);
            const float warpZ = fractalNoise(nx * 2.2F, nz * 2.2F, seed + 31U, 3, 0.5F);
            const float px = nx * 3.0F + warpX * 0.16F;
            const float pz = nz * 3.0F + warpZ * 0.16F;

            const float base = fractalNoise(px, pz, seed + 101U, 4, 0.44F);
            const float billow = std::abs(fractalNoise(px * 1.45F, pz * 1.45F,
                                                       seed + 211U, 4, 0.5F)) * 2.0F - 1.0F;
            const float ridgeSource = fractalNoise(px * 1.2F, pz * 1.2F,
                                                    seed + 307U, 5, 0.48F);
            const float ridges = std::pow(1.0F - std::abs(ridgeSource), 2.8F);
            const float region = fractalNoise(nx * 1.35F, nz * 1.35F,
                                               seed + 401U, 3, 0.55F);
            const float mountainWeight = smoothstep(0.02F, 0.52F, region);
            const float plains = base * 0.32F + billow * 0.04F;
            const float mountains = base * 0.28F + ridges * 0.46F;
            const float value = glm::mix(plains, mountains, mountainWeight);

            heights_[static_cast<std::size_t>(z * vertexCount + x)] = value;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }

    const float range = maximum - minimum;
    for (float& height : heights_) {
        height = (height - minimum) / range;
        // Broaden lowlands and reserve less area for the highest peaks.
        height = std::pow(height, 1.12F);
    }

    // Weighted blur removes single-cell ridges while retaining broad landforms.
    std::vector<float> smoothed(heights_.size());
    for (int pass = 0; pass < 4; ++pass) {
        for (int z = 0; z < vertexCount; ++z) {
            for (int x = 0; x < vertexCount; ++x) {
                float total = 0.0F;
                float weightTotal = 0.0F;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sampleX = std::clamp(x + dx, 0, cellCount);
                        const int sampleZ = std::clamp(z + dz, 0, cellCount);
                        const float weight = (dx == 0 ? 2.0F : 1.0F)
                                           * (dz == 0 ? 2.0F : 1.0F);
                        total += heights_[static_cast<std::size_t>(sampleZ * vertexCount + sampleX)] * weight;
                        weightTotal += weight;
                    }
                }
                smoothed[static_cast<std::size_t>(z * vertexCount + x)] = total / weightTotal;
            }
        }
        heights_.swap(smoothed);
    }
}

float Terrain::normalizedHeight(int x, int z) const {
    x = std::clamp(x, 0, cellCount);
    z = std::clamp(z, 0, cellCount);
    return heights_[static_cast<std::size_t>(z * vertexCount + x)];
}

float Terrain::vertexHeight(int x, int z) const {
    return normalizedHeight(x, z) * heightScale;
}

float Terrain::heightAt(float worldX, float worldZ) const {
    const float halfExtent = worldExtent() * 0.5F;
    const float gridX = (worldX + halfExtent) / spacing;
    const float gridZ = (worldZ + halfExtent) / spacing;
    if (gridX < 0.0F || gridZ < 0.0F
        || gridX > static_cast<float>(cellCount)
        || gridZ > static_cast<float>(cellCount)) {
        return 0.0F;
    }

    const int x0 = std::min(static_cast<int>(std::floor(gridX)), cellCount - 1);
    const int z0 = std::min(static_cast<int>(std::floor(gridZ)), cellCount - 1);
    const float u = gridX - static_cast<float>(x0);
    const float v = gridZ - static_cast<float>(z0);
    const float h00 = vertexHeight(x0, z0);
    const float h10 = vertexHeight(x0 + 1, z0);
    const float h01 = vertexHeight(x0, z0 + 1);
    const float h11 = vertexHeight(x0 + 1, z0 + 1);

    if (u + v <= 1.0F) {
        return h00 + u * (h10 - h00) + v * (h01 - h00);
    }
    return h11 + (1.0F - u) * (h01 - h11)
               + (1.0F - v) * (h10 - h11);
}

glm::vec3 Terrain::normalAt(int x, int z) const {
    const float left = vertexHeight(x - 1, z);
    const float right = vertexHeight(x + 1, z);
    const float down = vertexHeight(x, z - 1);
    const float up = vertexHeight(x, z + 1);
    return glm::normalize(glm::vec3{left - right, 2.0F * spacing, down - up});
}

glm::vec3 Terrain::colorAt(float height) const {
    struct Stop { float height; glm::vec3 color; };
    static const std::array<Stop, 5> stops{{
        {0.00F, {32.0F, 70.0F, 80.0F}},
        {0.18F, {32.0F, 160.0F, 0.0F}},
        {0.48F, {224.0F, 224.0F, 0.0F}},
        {0.72F, {128.0F, 128.0F, 128.0F}},
        {0.90F, {255.0F, 255.0F, 255.0F}}
    }};

    if (height <= stops.front().height) {
        return stops.front().color / 255.0F;
    }
    for (std::size_t index = 1; index < stops.size(); ++index) {
        if (height <= stops[index].height) {
            const float t = (height - stops[index - 1].height)
                          / (stops[index].height - stops[index - 1].height);
            return glm::mix(stops[index - 1].color, stops[index].color, t) / 255.0F;
        }
    }
    return stops.back().color / 255.0F;
}

float Terrain::worldExtent() const {
    return static_cast<float>(cellCount) * spacing;
}

} // namespace strategy
