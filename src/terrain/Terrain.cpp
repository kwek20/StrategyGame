#include "terrain/Terrain.hpp"
#include "terrain/TerrainBiomeGenerator.hpp"
#include "terrain/TerrainGeneration.hpp"
#include "terrain/TerrainGenerationPipeline.hpp"
#include "world/GenerationProgress.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <numeric>
#include <queue>
#include <stdexcept>

namespace strategy {
TerrainTagMask terrainTagFromName(std::string_view name) {
    if (name == "land") return terrainTagBit(TerrainTag::land);
    if (name == "water") return terrainTagBit(TerrainTag::water);
    if (name == "shallow-water") return terrainTagBit(TerrainTag::shallowWater);
    if (name == "deep-water") return terrainTagBit(TerrainTag::deepWater);
    if (name == "shoreline") return terrainTagBit(TerrainTag::shoreline);
    if (name == "submerged") return terrainTagBit(TerrainTag::submerged);
    if (name == "dry") return terrainTagBit(TerrainTag::dry);
    if (name == "vegetated") return terrainTagBit(TerrainTag::vegetated);
    if (name == "rocky") return terrainTagBit(TerrainTag::rocky);
    if (name == "buildable") return terrainTagBit(TerrainTag::buildable);
    if (name == "no-build") return terrainTagBit(TerrainTag::noBuild);
    if (name == "mountain-barrier") return terrainTagBit(TerrainTag::mountainBarrier);
    if (name == "mountain-pass") return terrainTagBit(TerrainTag::mountainPass);
    if (name == "universal-barrier") return terrainTagBit(TerrainTag::universalBarrier);
    if (name == "river") return terrainTagBit(TerrainTag::river);
    if (name == "lake") return terrainTagBit(TerrainTag::lake);
    if (name == "wetland") return terrainTagBit(TerrainTag::wetland);
    if (name == "beach") return terrainTagBit(TerrainTag::beach);
    if (name == "rocky-coast") return terrainTagBit(TerrainTag::rockyCoast);
    if (name == "river-bank") return terrainTagBit(TerrainTag::riverBank);
    if (name == "floodplain") return terrainTagBit(TerrainTag::floodplain);
    if (name == "sediment") return terrainTagBit(TerrainTag::sediment);
    if (name == "estuary") return terrainTagBit(TerrainTag::estuary);
    if (name == "confluence") return terrainTagBit(TerrainTag::confluence);
    return 0;
}
namespace {

float smoothstep(float edge0, float edge1, float value) {
    const float t = glm::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

void boxBlur(const std::vector<float>& source, std::vector<float>& destination,
             int side, int radius) {
    std::vector<float> horizontal(source.size());
    for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
            float total = 0.0F;
            int count = 0;
            for (int offset = -radius; offset <= radius; ++offset) {
                const int sampleX = std::clamp(x + offset, 0, side - 1);
                total += source[static_cast<std::size_t>(z * side + sampleX)];
                ++count;
            }
            horizontal[static_cast<std::size_t>(z * side + x)] = total / count;
        }
    }
    destination.resize(source.size());
    for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
            float total = 0.0F;
            int count = 0;
            for (int offset = -radius; offset <= radius; ++offset) {
                const int sampleZ = std::clamp(z + offset, 0, side - 1);
                total += horizontal[static_cast<std::size_t>(sampleZ * side + x)];
                ++count;
            }
            destination[static_cast<std::size_t>(z * side + x)] = total / count;
        }
    }
}

TerrainRegionalFields interpolateFields(const TerrainRegionalFields& topLeft,
                                        const TerrainRegionalFields& topRight,
                                        const TerrainRegionalFields& bottomLeft,
                                        const TerrainRegionalFields& bottomRight,
                                        float tx, float tz) {
    TerrainRegionalFields result;
    const auto interpolate = [&](float TerrainRegionalFields::*member) {
        return std::lerp(std::lerp(topLeft.*member, topRight.*member, tx),
                         std::lerp(bottomLeft.*member, bottomRight.*member, tx), tz);
    };
    result.continentalness = interpolate(&TerrainRegionalFields::continentalness);
    result.erosion = interpolate(&TerrainRegionalFields::erosion);
    result.peaks = interpolate(&TerrainRegionalFields::peaks);
    result.moisture = interpolate(&TerrainRegionalFields::moisture);
    result.temperature = interpolate(&TerrainRegionalFields::temperature);
    result.detail = interpolate(&TerrainRegionalFields::detail);
    result.plains = interpolate(&TerrainRegionalFields::plains);
    result.hills = interpolate(&TerrainRegionalFields::hills);
    result.mountainBelt = interpolate(&TerrainRegionalFields::mountainBelt);
    result.rockyOutcrops = interpolate(&TerrainRegionalFields::rockyOutcrops);
    result.coastalShelf = interpolate(&TerrainRegionalFields::coastalShelf);
    result.basin = interpolate(&TerrainRegionalFields::basin);
    return result;
}

const TerrainGenerationDefinitions& defaultGenerationDefinitions() {
    static const TerrainGenerationDefinitions definitions = TerrainGenerationDefinitions::load();
    return definitions;
}

TerrainTraversalClass traversalClass(const std::string& value) {
    if (value == "open")
        return TerrainTraversalClass::open;
    if (value == "difficult")
        return TerrainTraversalClass::difficult;
    if (value == "impassable")
        return TerrainTraversalClass::impassable;
    throw std::runtime_error("Unknown terrain traversal class: " + value);
}

TerrainBuildabilityClass buildabilityClass(const std::string& value) {
    if (value == "buildable")
        return TerrainBuildabilityClass::buildable;
    if (value == "restricted")
        return TerrainBuildabilityClass::restricted;
    if (value == "forbidden")
        return TerrainBuildabilityClass::forbidden;
    throw std::runtime_error("Unknown terrain buildability class: " + value);
}

} // namespace

Terrain::Terrain(std::uint32_t seed, WorldGenerationProgress* progress) {
    const TerrainGenerationDefinitions& definitions = defaultGenerationDefinitions();
    TerrainGenerationPipeline::generate(*this, seed, definitions.activeLayout(), definitions,
                                        progress);
}

Terrain::Terrain(std::uint32_t seed, const TerrainGeneratorDefinition& generator,
                 WorldGenerationProgress* progress) {
    TerrainGenerationPipeline::generateProcedural(
        *this, seed, generator, defaultGenerationDefinitions(), progress);
}

Terrain::Terrain(std::uint32_t seed, const TerrainGeneratorDefinition& generator,
                 bool waterEnabled, WorldGenerationProgress* progress) {
    TerrainGenerationPipeline::generateProcedural(
        *this, seed, generator, defaultGenerationDefinitions(), progress, waterEnabled);
}

Terrain::Terrain(std::uint32_t seed, TerrainLayoutId layout,
                 WorldGenerationProgress* progress) {
    const TerrainGenerationDefinitions& definitions = defaultGenerationDefinitions();
    TerrainGenerationPipeline::generate(*this, seed, definitions.layout(layout), definitions,
                                        progress);
}

void Terrain::generateHeightfield(std::uint32_t seed,
                                  const TerrainGeneratorDefinition& generator,
                                  WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::terrainFields, 0.0F);
    heights_.resize(static_cast<std::size_t>(vertexCount * vertexCount));
    std::vector<float> plainMasks(heights_.size(), 0.0F);
    std::vector<float> hillMasks(heights_.size(), 0.0F);
    std::vector<float> mountainMasks(heights_.size(), 0.0F);
    std::vector<float> coastalMasks(heights_.size(), 0.0F);
    const TerrainFieldGenerator fieldGenerator(seed, generator);
    const float halfExtent = worldExtent() * 0.5F;

    // Regional noise is intentionally sampled more coarsely than the render mesh. Bilinear
    // interpolation keeps the macro fields continuous while avoiding twelve fractal-noise stacks
    // at every render vertex.
    const int regionalStride = static_cast<int>(generator.postProcessing.regionalSampleStride);
    const int regionalSide = (cellCount + regionalStride - 1) / regionalStride + 1;
    std::vector<TerrainRegionalFields> regionalFields(
        static_cast<std::size_t>(regionalSide * regionalSide));
    for (int z = 0; z < regionalSide; ++z) {
        if (progress && z % 8 == 0)
            progress->report(WorldGenerationPhase::terrainFields,
                             0.25F * static_cast<float>(z) / regionalSide);
        const int vertexZ = std::min(z * regionalStride, cellCount);
        for (int x = 0; x < regionalSide; ++x) {
            const int vertexX = std::min(x * regionalStride, cellCount);
            regionalFields[static_cast<std::size_t>(z * regionalSide + x)] =
                fieldGenerator.sample(static_cast<float>(vertexX) * spacing - halfExtent,
                                      static_cast<float>(vertexZ) * spacing - halfExtent);
        }
    }

    // Pass 1: synthesize the raw macro landforms and retain their masks. Each later pass has a
    // deliberately different job; plains are no longer smoothed like hills or mountain ridges.
    for (int z = 0; z < vertexCount; ++z) {
        if (progress && z % 16 == 0)
            progress->report(WorldGenerationPhase::terrainFields,
                             0.25F + 0.30F * static_cast<float>(z) / vertexCount);
        for (int x = 0; x < vertexCount; ++x) {
            const int regionalX = std::min(x / regionalStride, regionalSide - 2);
            const int regionalZ = std::min(z / regionalStride, regionalSide - 2);
            const float tx = static_cast<float>(x - regionalX * regionalStride) / regionalStride;
            const float tz = static_cast<float>(z - regionalZ * regionalStride) / regionalStride;
            const auto regionalAt = [&](int sampleX, int sampleZ) -> const TerrainRegionalFields& {
                return regionalFields[static_cast<std::size_t>(sampleZ * regionalSide + sampleX)];
            };
            const TerrainRegionalFields fields = interpolateFields(
                regionalAt(regionalX, regionalZ), regionalAt(regionalX + 1, regionalZ),
                regionalAt(regionalX, regionalZ + 1),
                regionalAt(regionalX + 1, regionalZ + 1), tx, tz);
            const std::size_t index = static_cast<std::size_t>(z * vertexCount + x);
            heights_[index] = fieldGenerator.height(fields);
            plainMasks[index] = fields.plains * (1.0F - fields.mountainBelt) *
                                (1.0F - fields.rockyOutcrops * 0.8F);
            hillMasks[index] = fields.hills * (1.0F - fields.plains * 0.55F) *
                               (1.0F - fields.mountainBelt);
            mountainMasks[index] = std::max(fields.mountainBelt,
                                             fields.rockyOutcrops * 0.45F);
            const float shoreDistance = std::abs(heights_[index] - generator.waterLevel);
            coastalMasks[index] = fields.coastalShelf *
                                  (1.0F - smoothstep(0.025F, 0.15F, shoreDistance));
        }
    }

    std::vector<float> filtered;
    const TerrainPostProcessDefinition& post = generator.postProcessing;
    const auto blendPass = [&](const std::vector<float>& mask, float strength) {
        for (std::size_t index = 0; index < heights_.size(); ++index)
            heights_[index] = std::lerp(heights_[index], filtered[index],
                                        glm::clamp(mask[index] * strength, 0.0F, 1.0F));
    };

    // Pass 2: broad plain leveling creates usable plateaus without forcing a single height.
    boxBlur(heights_, filtered, vertexCount, static_cast<int>(post.plainBlurRadius));
    blendPass(plainMasks, post.plainFlattenStrength);
    if (progress) progress->report(WorldGenerationPhase::terrainFields, 0.68F);

    // Pass 3: hills use a smaller kernel, preserving rolling silhouettes and valleys.
    boxBlur(heights_, filtered, vertexCount, static_cast<int>(post.hillBlurRadius));
    blendPass(hillMasks, post.hillSmoothingStrength);
    if (progress) progress->report(WorldGenerationPhase::terrainFields, 0.78F);

    // Pass 4: very light repeated erosion removes needle peaks while retaining mountain chains.
    for (std::uint32_t pass = 0; pass < post.mountainErosionPasses; ++pass) {
        boxBlur(heights_, filtered, vertexCount, 1);
        blendPass(mountainMasks, post.mountainErosionStrength);
    }
    if (progress) progress->report(WorldGenerationPhase::terrainFields, 0.88F);

    // Pass 5: coastal shelves are resolved last so later landform passes cannot reintroduce
    // noisy shore steps.
    boxBlur(heights_, filtered, vertexCount, static_cast<int>(post.coastalBlurRadius));
    blendPass(coastalMasks, post.coastalSmoothingStrength);
    for (float& height : heights_) height = glm::clamp(height, 0.0F, 1.0F);
    if (progress) progress->report(WorldGenerationPhase::terrainFields, 1.0F);
}

void Terrain::applyHydrology(std::uint32_t seed, const TerrainGeneratorDefinition& generator,
                             WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::water, 0.0F);
    const TerrainHydrologyDefinition& settings = generator.hydrology;
    const float extent = worldExtent();
    const float halfExtent = extent * 0.5F;
    const int side = std::max(3, static_cast<int>(std::ceil(extent / settings.gridCellSize)));
    const std::size_t count = static_cast<std::size_t>(side * side);
    const float cellSize = extent / static_cast<float>(side);
    const float cellArea = cellSize * cellSize;
    const float normalizedWater = generator.waterLevel;
    std::vector<float> elevation(count);
    std::vector<float> filled(count, std::numeric_limits<float>::infinity());
    std::vector<float> accumulation(count, 1.0F);
    std::vector<int> receiver(count, -1);
    std::vector<std::uint8_t> visited(count, 0);
    using QueueEntry = std::pair<float, int>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> frontier;
    const auto indexOf = [side](int x, int z) { return z * side + x; };
    const auto worldAt = [&](int coordinate) {
        return -halfExtent + (static_cast<float>(coordinate) + 0.5F) * cellSize;
    };

    for (int z = 0; z < side; ++z) {
        if (progress && z % 8 == 0)
            progress->report(WorldGenerationPhase::water,
                             0.15F * static_cast<float>(z) / side);
        for (int x = 0; x < side; ++x) {
            const int index = indexOf(x, z);
            elevation[static_cast<std::size_t>(index)] =
                heightAt(worldAt(x), worldAt(z)) / heightScale;
            const bool boundary = x == 0 || z == 0 || x == side - 1 || z == side - 1;
            const bool ocean = elevation[static_cast<std::size_t>(index)] <= normalizedWater;
            if (boundary || ocean) {
                filled[static_cast<std::size_t>(index)] =
                    elevation[static_cast<std::size_t>(index)];
                visited[static_cast<std::size_t>(index)] = 1;
                frontier.emplace(filled[static_cast<std::size_t>(index)], index);
            }
        }
    }

    // Priority-flood drainage gives every inland cell a deterministic route to ocean or map edge.
    // Depression fill depth is retained for lake and wetland classification.
    constexpr std::array<glm::ivec2, 8> neighbors{{
        {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
    while (!frontier.empty()) {
        const auto [level, index] = frontier.top();
        frontier.pop();
        const int x = index % side;
        const int z = index / side;
        for (const glm::ivec2 offset : neighbors) {
            const int nx = x + offset.x;
            const int nz = z + offset.y;
            if (nx < 0 || nz < 0 || nx >= side || nz >= side) continue;
            const int next = indexOf(nx, nz);
            if (visited[static_cast<std::size_t>(next)] != 0) continue;
            visited[static_cast<std::size_t>(next)] = 1;
            receiver[static_cast<std::size_t>(next)] = index;
            filled[static_cast<std::size_t>(next)] =
                std::max(elevation[static_cast<std::size_t>(next)], level + 0.00001F);
            frontier.emplace(filled[static_cast<std::size_t>(next)], next);
        }
    }

    std::vector<int> drainageOrder(count);
    std::iota(drainageOrder.begin(), drainageOrder.end(), 0);
    std::stable_sort(drainageOrder.begin(), drainageOrder.end(), [&](int left, int right) {
        if (filled[static_cast<std::size_t>(left)] != filled[static_cast<std::size_t>(right)])
            return filled[static_cast<std::size_t>(left)] >
                   filled[static_cast<std::size_t>(right)];
        return left < right;
    });
    for (const int index : drainageOrder) {
        const int downstream = receiver[static_cast<std::size_t>(index)];
        if (downstream >= 0)
            accumulation[static_cast<std::size_t>(downstream)] +=
                accumulation[static_cast<std::size_t>(index)];
    }
    std::vector<std::vector<int>> upstream(count);
    for (int index = 0; index < static_cast<int>(count); ++index) {
        const int downstream = receiver[static_cast<std::size_t>(index)];
        if (downstream >= 0)
            upstream[static_cast<std::size_t>(downstream)].push_back(index);
    }
    if (progress) progress->report(WorldGenerationPhase::water, 0.35F);

    waterSurfaceHeights_ = heights_;
    waterCoverage_.assign(heights_.size(), 0.0F);
    const std::vector<float> preHydrologyHeights = heights_;
    drainage_.assign(heights_.size(), 0.0F);
    waterKinds_.assign(heights_.size(), WaterKind::none);
    wetlandFlags_.assign(heights_.size(), 0);
    for (std::size_t index = 0; index < heights_.size(); ++index) {
        if (heights_[index] < normalizedWater) {
            waterSurfaceHeights_[index] = normalizedWater;
            waterCoverage_[index] = 1.0F;
            waterKinds_[index] = WaterKind::ocean;
        }
    }

    const float maximumAccumulation =
        *std::max_element(accumulation.begin(), accumulation.end());
    const auto applyWaterBrush = [&](glm::vec2 center, float radius, float bankFalloff,
                                     float surface, float carveDepth, WaterKind kind,
                                     float drainageValue) {
        const float outerRadius = radius + bankFalloff;
        const int minX = std::clamp(static_cast<int>(std::floor(
                                        (center.x - outerRadius + halfExtent) / spacing)),
                                    0, cellCount);
        const int maxX = std::clamp(static_cast<int>(std::ceil(
                                        (center.x + outerRadius + halfExtent) / spacing)),
                                    0, cellCount);
        const int minZ = std::clamp(static_cast<int>(std::floor(
                                        (center.y - outerRadius + halfExtent) / spacing)),
                                    0, cellCount);
        const int maxZ = std::clamp(static_cast<int>(std::ceil(
                                        (center.y + outerRadius + halfExtent) / spacing)),
                                    0, cellCount);
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const float worldX = static_cast<float>(x) * spacing - halfExtent;
                const float worldZ = static_cast<float>(z) * spacing - halfExtent;
                const float distance = glm::distance(glm::vec2{worldX, worldZ}, center);
                if (distance > outerRadius) continue;
                const float influence = 1.0F - smoothstep(radius, outerRadius, distance);
                const std::size_t vertex = static_cast<std::size_t>(z * vertexCount + x);
                if (waterKinds_[vertex] == WaterKind::ocean) continue;
                if (kind == WaterKind::none) {
                    wetlandFlags_[vertex] = influence > 0.35F ? 1 : wetlandFlags_[vertex];
                    drainage_[vertex] = std::max(drainage_[vertex], drainageValue);
                    continue;
                }
                const float targetBed =
                    std::max(0.0F, preHydrologyHeights[vertex] - carveDepth * influence);
                heights_[vertex] = std::min(heights_[vertex], targetBed);
                const float localSurface = std::max(heights_[vertex], surface);
                waterSurfaceHeights_[vertex] =
                    std::max(waterSurfaceHeights_[vertex],
                             std::lerp(heights_[vertex], localSurface, influence));
                drainage_[vertex] = std::max(drainage_[vertex], drainageValue);
                waterCoverage_[vertex] = std::max(waterCoverage_[vertex], influence);
                if (influence > 0.35F) waterKinds_[vertex] = kind;
            }
        }
    };

    // Stable sub-cell control points prevent the D8 drainage grid from appearing as a sequence of
    // aligned dots. Shared endpoints keep tributaries connected; per-segment Bezier bends provide
    // restrained meanders without changing authoritative downstream topology.
    const auto randomSigned = [seed](std::uint32_t value, std::uint32_t salt) {
        std::uint32_t hash = seed ^ (value * 0x9E3779B9U) ^ salt;
        hash ^= hash >> 16U;
        hash *= 0x7FEB352DU;
        hash ^= hash >> 15U;
        hash *= 0x846CA68BU;
        hash ^= hash >> 16U;
        return static_cast<float>(hash & 0x00FFFFFFU) /
                   static_cast<float>(0x007FFFFFU) -
               1.0F;
    };
    const auto channelPoint = [&](int index) {
        const int x = index % side;
        const int z = index / side;
        return glm::vec2{worldAt(x), worldAt(z)};
    };

    // Build an authoritative river graph before carving. Thresholds are evaluated in world-area
    // units so this topology is independent of hydrology resolution.
    std::vector<std::uint8_t> activeRiver(count, 0);
    for (int index = 0; index < static_cast<int>(count); ++index) {
        const float catchmentArea = accumulation[static_cast<std::size_t>(index)] * cellArea;
        if (receiver[static_cast<std::size_t>(index)] >= 0 &&
            elevation[static_cast<std::size_t>(index)] > normalizedWater &&
            catchmentArea >= settings.riverCatchmentArea)
            activeRiver[static_cast<std::size_t>(index)] = 1;
    }
    const auto activeUpstreamCounts = [&]() {
        std::vector<std::uint32_t> result(count, 0);
        for (int index = 0; index < static_cast<int>(count); ++index)
            if (activeRiver[static_cast<std::size_t>(index)] != 0)
                for (const int source : upstream[static_cast<std::size_t>(index)])
                    if (activeRiver[static_cast<std::size_t>(source)] != 0)
                        ++result[static_cast<std::size_t>(index)];
        return result;
    };

    // Remove only short first-order stubs. Junction and downstream cells are retained, and the
    // stable index scan makes pruning deterministic.
    std::vector<std::uint32_t> activeUpstream = activeUpstreamCounts();
    for (int source = 0; source < static_cast<int>(count); ++source) {
        if (activeRiver[static_cast<std::size_t>(source)] == 0 ||
            activeUpstream[static_cast<std::size_t>(source)] != 0)
            continue;
        std::vector<int> branch{source};
        float length = 0.0F;
        int cursor = source;
        while (true) {
            const int next = receiver[static_cast<std::size_t>(cursor)];
            if (next < 0 || activeRiver[static_cast<std::size_t>(next)] == 0) break;
            length += glm::distance(channelPoint(cursor), channelPoint(next));
            if (activeUpstream[static_cast<std::size_t>(next)] != 1) break;
            branch.push_back(next);
            cursor = next;
        }
        if (length < settings.minimumFirstOrderLength)
            for (const int index : branch)
                activeRiver[static_cast<std::size_t>(index)] = 0;
    }
    activeUpstream = activeUpstreamCounts();

    // Strahler ordering gives tributaries a stable hierarchy. Two equal-order tributaries raise
    // downstream order; otherwise the larger order continues.
    std::vector<std::uint32_t> streamOrders(count, 0);
    for (const int index : drainageOrder) {
        if (activeRiver[static_cast<std::size_t>(index)] == 0) continue;
        std::uint32_t maximumOrder = 0;
        std::uint32_t maximumCount = 0;
        for (const int source : upstream[static_cast<std::size_t>(index)]) {
            if (activeRiver[static_cast<std::size_t>(source)] == 0) continue;
            const std::uint32_t order = streamOrders[static_cast<std::size_t>(source)];
            if (order > maximumOrder) {
                maximumOrder = order;
                maximumCount = 1;
            } else if (order == maximumOrder) {
                ++maximumCount;
            }
        }
        streamOrders[static_cast<std::size_t>(index)] =
            maximumOrder == 0 ? 1 : maximumOrder + (maximumCount >= 2 ? 1U : 0U);
    }

    const auto interpolatePoint = [](const RiverPathPoint& a, const RiverPathPoint& b, float t) {
        RiverPathPoint point;
        point.position = glm::mix(a.position, b.position, t);
        point.surfaceHeight = std::lerp(a.surfaceHeight, b.surfaceHeight, t);
        point.normalizedDrainage = std::lerp(a.normalizedDrainage, b.normalizedDrainage, t);
        point.streamOrder = t < 0.5F ? a.streamOrder : b.streamOrder;
        return point;
    };
    const auto resamplePath = [&](const std::vector<RiverPathPoint>& input) {
        std::vector<RiverPathPoint> result;
        if (input.empty()) return result;
        result.push_back(input.front());
        float distanceUntilSample = settings.riverPathSampleSpacing;
        for (std::size_t segmentIndex = 1; segmentIndex < input.size(); ++segmentIndex) {
            RiverPathPoint start = input[segmentIndex - 1];
            const RiverPathPoint end = input[segmentIndex];
            float segmentLength = glm::distance(start.position, end.position);
            while (segmentLength >= distanceUntilSample && segmentLength > 0.0001F) {
                const float t = distanceUntilSample / segmentLength;
                start = interpolatePoint(start, end, t);
                result.push_back(start);
                segmentLength = glm::distance(start.position, end.position);
                distanceUntilSample = settings.riverPathSampleSpacing;
            }
            distanceUntilSample -= segmentLength;
        }
        if (glm::distance(result.back().position, input.back().position) > 0.01F)
            result.push_back(input.back());
        return result;
    };

    riverPaths_.clear();
    std::uint32_t nextPathId = 1;
    for (int start = 0; start < static_cast<int>(count); ++start) {
        if (activeRiver[static_cast<std::size_t>(start)] == 0 ||
            activeUpstream[static_cast<std::size_t>(start)] == 1)
            continue;
        std::vector<int> nodes{start};
        int cursor = start;
        while (true) {
            const int next = receiver[static_cast<std::size_t>(cursor)];
            if (next < 0 || activeRiver[static_cast<std::size_t>(next)] == 0) break;
            nodes.push_back(next);
            cursor = next;
            if (activeUpstream[static_cast<std::size_t>(cursor)] != 1) break;
        }
        if (nodes.size() < 2) continue;

        std::vector<RiverPathPoint> points;
        points.reserve(nodes.size());
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const int node = nodes[nodeIndex];
            glm::vec2 position = channelPoint(node);
            if (nodeIndex > 0 && nodeIndex + 1 < nodes.size()) {
                const glm::vec2 previous = channelPoint(nodes[nodeIndex - 1]);
                const glm::vec2 next = channelPoint(nodes[nodeIndex + 1]);
                const glm::vec2 tangent = glm::normalize(next - previous);
                const glm::vec2 normal{-tangent.y, tangent.x};
                const float run = std::max(glm::distance(previous, next), 0.001F);
                const float slopeDegrees = glm::degrees(std::atan(
                    std::abs(elevation[static_cast<std::size_t>(nodes[nodeIndex - 1])] -
                             elevation[static_cast<std::size_t>(nodes[nodeIndex + 1])]) *
                    heightScale / run));
                const float meanderWeight =
                    1.0F - smoothstep(0.0F, settings.meanderMaximumSlopeDegrees, slopeDegrees);
                position += normal * randomSigned(static_cast<std::uint32_t>(node), 0xAD90777DU) *
                            settings.riverMeanderStrength * meanderWeight;

                // Search across the local valley and keep the lowest nearby candidate. This
                // constrains smoothing/meanders to drainage terrain instead of crossing ridges.
                glm::vec2 best = position;
                float bestScore = heightAt(best.x, best.y);
                for (const float lateral : {-1.0F, -0.5F, 0.0F, 0.5F, 1.0F}) {
                    const glm::vec2 candidate = position + normal * lateral * cellSize;
                    const float score = heightAt(candidate.x, candidate.y) +
                                        std::abs(lateral) * 0.015F;
                    if (score < bestScore) {
                        best = candidate;
                        bestScore = score;
                    }
                }
                position = best;
            }
            RiverPathPoint point;
            point.position = position;
            point.normalizedDrainage = std::log1p(accumulation[static_cast<std::size_t>(node)]) /
                                       std::log1p(maximumAccumulation);
            point.streamOrder = streamOrders[static_cast<std::size_t>(node)];
            points.push_back(point);
        }

        for (std::uint32_t iteration = 0; iteration < settings.riverSmoothingIterations;
             ++iteration) {
            std::vector<RiverPathPoint> smoothed;
            smoothed.reserve(points.size() * 2);
            smoothed.push_back(points.front());
            for (std::size_t index = 1; index < points.size(); ++index) {
                smoothed.push_back(interpolatePoint(points[index - 1], points[index], 0.25F));
                smoothed.push_back(interpolatePoint(points[index - 1], points[index], 0.75F));
            }
            smoothed.push_back(points.back());
            points = std::move(smoothed);
        }
        points = resamplePath(points);

        float previousSurface = std::numeric_limits<float>::infinity();
        for (RiverPathPoint& point : points) {
            const float terrainSurface = heightAt(point.position.x, point.position.y) / heightScale;
            float surface = terrainSurface - settings.riverCarveDepth * 0.20F;
            if (std::isfinite(previousSurface))
                surface = std::min(surface, previousSurface - 0.000002F);
            surface = std::max(surface, normalizedWater);
            point.surfaceHeight = surface * heightScale;
            previousSurface = surface;
        }
        RiverPath path;
        path.id = nextPathId++;
        path.streamOrder = points.front().streamOrder;
        const int outlet = receiver[static_cast<std::size_t>(cursor)];
        path.endsAtOcean = outlet >= 0 &&
                           elevation[static_cast<std::size_t>(outlet)] <= normalizedWater;
        for (const RiverPathPoint& point : points)
            path.streamOrder = std::max(path.streamOrder, point.streamOrder);
        path.points = std::move(points);
        riverPaths_.push_back(std::move(path));
    }
    riverDistances_.assign(heights_.size(), std::numeric_limits<float>::infinity());
    riverHalfWidths_.assign(heights_.size(), 0.0F);
    riverFlowDirections_.assign(heights_.size(), glm::vec2{0.0F});
    riverStreamOrders_.assign(heights_.size(), 0);
    floodplainFlags_.assign(heights_.size(), 0);
    riverBankFlags_.assign(heights_.size(), 0);
    sedimentFlags_.assign(heights_.size(), 0);
    estuaryFlags_.assign(heights_.size(), 0);
    confluenceFlags_.assign(heights_.size(), 0);
    std::vector<float> riverSurfaceField(heights_.size(), 0.0F);
    std::vector<float> riverCarveField(heights_.size(), 0.0F);
    std::vector<float> riverDrainageField(heights_.size(), 0.0F);
    std::vector<float> estuaryInfluence(heights_.size(), 0.0F);

    const auto rasterizeChannelSegment = [&](const RiverPathPoint& start,
                                             const RiverPathPoint& end,
                                             float startWidth, float endWidth,
                                             float carveDepth,
                                             float startEstuary, float endEstuary) {
        const float maximumWidth = std::max(startWidth, endWidth);
        const float outerWidth = maximumWidth + settings.riverBankFalloff +
                                 maximumWidth * settings.riverFloodplainWidthMultiplier;
        const int minX = std::clamp(static_cast<int>(std::floor(
                                        (std::min(start.position.x, end.position.x) - outerWidth + halfExtent) /
                                        spacing)), 0, cellCount);
        const int maxX = std::clamp(static_cast<int>(std::ceil(
                                        (std::max(start.position.x, end.position.x) + outerWidth + halfExtent) /
                                        spacing)), 0, cellCount);
        const int minZ = std::clamp(static_cast<int>(std::floor(
                                        (std::min(start.position.y, end.position.y) - outerWidth + halfExtent) /
                                        spacing)), 0, cellCount);
        const int maxZ = std::clamp(static_cast<int>(std::ceil(
                                        (std::max(start.position.y, end.position.y) + outerWidth + halfExtent) /
                                        spacing)), 0, cellCount);
        const glm::vec2 segment = end.position - start.position;
        const float lengthSquared = glm::dot(segment, segment);
        if (lengthSquared <= 0.00001F) return;
        const glm::vec2 flowDirection = glm::normalize(segment);
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const glm::vec2 point{static_cast<float>(x) * spacing - halfExtent,
                                      static_cast<float>(z) * spacing - halfExtent};
                const float t = glm::clamp(glm::dot(point - start.position, segment) / lengthSquared,
                                           0.0F, 1.0F);
                const float width = std::lerp(startWidth, endWidth, t);
                const float distance = glm::distance(point, start.position + segment * t);
                if (distance > width + settings.riverBankFalloff +
                                   width * settings.riverFloodplainWidthMultiplier)
                    continue;
                const std::size_t vertex = static_cast<std::size_t>(z * vertexCount + x);
                const float signedDistance = distance - width;
                if (signedDistance >= riverDistances_[vertex]) continue;
                riverDistances_[vertex] = signedDistance;
                riverHalfWidths_[vertex] = width;
                riverSurfaceField[vertex] =
                    std::lerp(start.surfaceHeight, end.surfaceHeight, t) / heightScale;
                riverCarveField[vertex] = carveDepth;
                riverDrainageField[vertex] =
                    std::lerp(start.normalizedDrainage, end.normalizedDrainage, t);
                riverFlowDirections_[vertex] = flowDirection;
                riverStreamOrders_[vertex] =
                    t < 0.5F ? start.streamOrder : end.streamOrder;
                estuaryInfluence[vertex] = std::lerp(startEstuary, endEstuary, t);
            }
        }
    };

    for (const RiverPath& path : riverPaths_) {
        for (std::size_t index = 1; index < path.points.size(); ++index) {
            const RiverPathPoint& start = path.points[index - 1];
            const RiverPathPoint& end = path.points[index];
            const auto mouthFor = [&](std::size_t pointIndex) {
                if (!path.endsAtOcean || settings.estuaryLength <= 0.0F) return 0.0F;
                const float remaining = static_cast<float>(path.points.size() - 1 - pointIndex) *
                                        settings.riverPathSampleSpacing;
                return 1.0F - smoothstep(0.0F, settings.estuaryLength, remaining);
            };
            const auto widthFor = [&](const RiverPathPoint& point, std::size_t pointIndex) {
                float width = settings.riverHalfWidth *
                       (0.62F + 0.20F * static_cast<float>(point.streamOrder) +
                        0.72F * point.normalizedDrainage);
                width *= std::lerp(1.0F, settings.estuaryWidthMultiplier,
                                   mouthFor(pointIndex));
                return width;
            };
            const float carve = settings.riverCarveDepth *
                                std::sqrt(std::max(widthFor(start, index - 1),
                                                   widthFor(end, index)) /
                                          settings.riverHalfWidth);
            rasterizeChannelSegment(start, end, widthFor(start, index - 1),
                                    widthFor(end, index), carve,
                                    mouthFor(index - 1), mouthFor(index));
        }
    }

    // Blend tributaries into a widened junction zone. This operates on the same distance field,
    // so the result remains one continuous surface instead of overlapping channel meshes.
    for (int node = 0; node < static_cast<int>(count); ++node) {
        if (activeRiver[static_cast<std::size_t>(node)] == 0 ||
            activeUpstream[static_cast<std::size_t>(node)] < 2)
            continue;
        const glm::vec2 center = channelPoint(node);
        const int downstream = receiver[static_cast<std::size_t>(node)];
        glm::vec2 flow{0.0F, 1.0F};
        if (downstream >= 0) {
            const glm::vec2 delta = channelPoint(downstream) - center;
            if (glm::length(delta) > 0.001F) flow = glm::normalize(delta);
        }
        const float normalizedDrainage =
            std::log1p(accumulation[static_cast<std::size_t>(node)]) /
            std::log1p(maximumAccumulation);
        const float baseWidth = settings.riverHalfWidth *
            (0.62F + 0.20F * static_cast<float>(streamOrders[static_cast<std::size_t>(node)]) +
             0.72F * normalizedDrainage);
        const float junctionWidth = baseWidth * settings.confluenceWidthMultiplier;
        const float outerRadius = junctionWidth + settings.riverBankFalloff +
                                  junctionWidth * settings.riverFloodplainWidthMultiplier;
        const int minX = std::clamp(static_cast<int>(std::floor(
                                        (center.x - outerRadius + halfExtent) / spacing)),
                                    0, cellCount);
        const int maxX = std::clamp(static_cast<int>(std::ceil(
                                        (center.x + outerRadius + halfExtent) / spacing)),
                                    0, cellCount);
        const int minZ = std::clamp(static_cast<int>(std::floor(
                                        (center.y - outerRadius + halfExtent) / spacing)),
                                    0, cellCount);
        const int maxZ = std::clamp(static_cast<int>(std::ceil(
                                        (center.y + outerRadius + halfExtent) / spacing)),
                                    0, cellCount);
        const float surface = std::max(normalizedWater,
            elevation[static_cast<std::size_t>(node)] - settings.riverCarveDepth * 0.2F);
        const glm::vec2 perpendicular{-flow.y, flow.x};
        const float sedimentSide = randomSigned(static_cast<std::uint32_t>(node), 0x91E10DA5U) < 0
                                       ? -1.0F : 1.0F;
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const glm::vec2 point{static_cast<float>(x) * spacing - halfExtent,
                                      static_cast<float>(z) * spacing - halfExtent};
                const glm::vec2 delta = point - center;
                const float distance = glm::length(delta);
                if (distance > outerRadius) continue;
                const std::size_t vertex = static_cast<std::size_t>(z * vertexCount + x);
                confluenceFlags_[vertex] = 1;
                const float signedDistance = distance - junctionWidth;
                if (signedDistance < riverDistances_[vertex]) {
                    riverDistances_[vertex] = signedDistance;
                    riverHalfWidths_[vertex] = junctionWidth;
                    riverSurfaceField[vertex] = surface;
                    riverCarveField[vertex] = settings.riverCarveDepth * 1.10F;
                    riverDrainageField[vertex] = normalizedDrainage;
                    riverFlowDirections_[vertex] = flow;
                    riverStreamOrders_[vertex] = streamOrders[static_cast<std::size_t>(node)];
                }
                const float lateral = glm::dot(delta, perpendicular) * sedimentSide;
                const float longitudinal = glm::dot(delta, flow);
                if (lateral > junctionWidth * 0.25F &&
                    lateral < junctionWidth * 0.82F &&
                    std::abs(longitudinal) < junctionWidth * 0.75F)
                    sedimentFlags_[vertex] = 1;
            }
        }
    }

    // Apply one coherent cross-section from the unioned channel distance field. This avoids
    // repeated segment stamps at bends and confluences and gives every channel the same bed,
    // bank, and floodplain construction.
    for (std::size_t vertex = 0; vertex < heights_.size(); ++vertex) {
        if (!std::isfinite(riverDistances_[vertex]) ||
            waterKinds_[vertex] == WaterKind::ocean)
            continue;
        const float width = riverHalfWidths_[vertex];
        const float distance = std::max(0.0F, riverDistances_[vertex] + width);
        const float surface = riverSurfaceField[vertex];
        const float original = preHydrologyHeights[vertex];
        const float carveDepth = riverCarveField[vertex];
        if (estuaryInfluence[vertex] > 0.05F) estuaryFlags_[vertex] = 1;
        drainage_[vertex] = std::max(drainage_[vertex], riverDrainageField[vertex]);
        if (distance <= width) {
            const float radial = width > 0.0F ? distance / width : 0.0F;
            const float bedDepth = carveDepth * (1.0F - 0.32F * radial * radial);
            const float targetBed = std::max(
                {0.0F, surface - bedDepth, original - settings.riverMaximumIncision});
            heights_[vertex] = std::min(heights_[vertex], targetBed);
            const float coverage = 1.0F - smoothstep(width * 0.82F, width, distance);
            waterSurfaceHeights_[vertex] = std::max(heights_[vertex], surface);
            waterCoverage_[vertex] = std::max(waterCoverage_[vertex], coverage);
            if (coverage > 0.01F) waterKinds_[vertex] = WaterKind::river;
        } else if (distance <= width + settings.riverBankFalloff) {
            riverBankFlags_[vertex] = 1;
            const float bankT = (distance - width) / settings.riverBankFalloff;
            const float bankTarget = std::lerp(surface, original,
                                                smoothstep(0.0F, 1.0F, bankT));
            heights_[vertex] = std::min(
                heights_[vertex], std::max(bankTarget,
                                           original - settings.riverMaximumIncision));
        } else {
            const float floodplainWidth = width * settings.riverFloodplainWidthMultiplier;
            if (floodplainWidth <= 0.0F) continue;
            const float floodplainT = glm::clamp(
                (distance - width - settings.riverBankFalloff) / floodplainWidth,
                0.0F, 1.0F);
            const float transition = smoothstep(0.0F, 1.0F, floodplainT);
            const float floodplainTarget =
                std::lerp(surface + settings.riverBankHeight, original, transition);
            const float flatten = settings.riverFloodplainFlattenStrength * (1.0F - transition);
            heights_[vertex] = std::lerp(heights_[vertex],
                                          std::min(heights_[vertex],
                                                   std::max(floodplainTarget,
                                                       original - settings.riverMaximumIncision)),
                                          flatten);
            floodplainFlags_[vertex] = flatten > 0.02F ? 1 : 0;
        }
    }

    for (int z = 1; z < side - 1; ++z) {
        if (progress && z % 8 == 0)
            progress->report(WorldGenerationPhase::water,
                             0.35F + 0.50F * static_cast<float>(z) / side);
        for (int x = 1; x < side - 1; ++x) {
            const int index = indexOf(x, z);
            const float original = elevation[static_cast<std::size_t>(index)];
            if (original <= normalizedWater) continue;
            if (activeRiver[static_cast<std::size_t>(index)] != 0) continue;
            const float accumulated = accumulation[static_cast<std::size_t>(index)];
            const float catchmentArea = accumulated * cellArea;
            const float normalizedDrainage = std::log1p(accumulated) /
                                             std::log1p(maximumAccumulation);
            const float fillDepth = filled[static_cast<std::size_t>(index)] - original;
            const int downstream = receiver[static_cast<std::size_t>(index)];
            const float downstreamHeight = downstream >= 0
                                               ? elevation[static_cast<std::size_t>(downstream)]
                                               : original;
            const float localSlopeDegrees = glm::degrees(std::atan(
                std::abs(original - downstreamHeight) / cellSize));
            if (fillDepth >= settings.lakeMinimumDepth &&
                fillDepth <= settings.lakeMaximumDepth * 2.0F &&
                catchmentArea >= settings.wetlandCatchmentArea) {
                const float lakeDepth = std::min(fillDepth, settings.lakeMaximumDepth);
                applyWaterBrush({worldAt(x), worldAt(z)}, cellSize * 0.55F,
                                cellSize * 0.38F, original + lakeDepth, 0.0F,
                                WaterKind::lake,
                                normalizedDrainage);
            } else if (catchmentArea >= settings.wetlandCatchmentArea &&
                       localSlopeDegrees <= settings.wetlandMaximumSlopeDegrees) {
                applyWaterBrush({worldAt(x), worldAt(z)}, cellSize * 0.42F,
                                cellSize * 0.45F, original, 0.0F, WaterKind::none,
                                normalizedDrainage);
            }
        }
    }

    generatedWaterSurfaceHeights_ = waterSurfaceHeights_;
    generatedWaterCoverage_ = waterCoverage_;

    // Smooth the water-depth scalar field rather than the terrain mesh. A small positive cutoff
    // removes sub-triangle slivers at banks, while the blur keeps the surviving shoreline contour
    // continuous instead of exposing the render grid as saw teeth.
    std::vector<float> waterDepth(heights_.size(), 0.0F);
    for (std::size_t index = 0; index < heights_.size(); ++index)
        if (waterKinds_[index] != WaterKind::ocean)
            waterDepth[index] =
                std::max(0.0F, waterSurfaceHeights_[index] - heights_[index]);
    std::vector<float> smoothedWaterDepth;
    boxBlur(waterDepth, smoothedWaterDepth, vertexCount,
            static_cast<int>(settings.waterEdgeSmoothingRadius));
    for (std::size_t index = 0; index < heights_.size(); ++index) {
        if (waterKinds_[index] == WaterKind::ocean) continue;
        const float depth = smoothedWaterDepth[index];
        if (depth <= settings.minimumRenderedWaterDepth) {
            waterSurfaceHeights_[index] = heights_[index];
            waterCoverage_[index] = 0.0F;
            waterKinds_[index] = WaterKind::none;
            continue;
        }
        const float edge = smoothstep(settings.minimumRenderedWaterDepth,
                                      settings.minimumRenderedWaterDepth * 2.5F, depth);
        waterSurfaceHeights_[index] = heights_[index] + depth * edge;
        waterCoverage_[index] = std::max(waterCoverage_[index], edge);
        if (std::isfinite(riverDistances_[index]) &&
            riverDistances_[index] <= 0.0F)
            waterKinds_[index] = WaterKind::river;
    }
    if (progress) progress->report(WorldGenerationPhase::water, 0.90F);
}

void Terrain::generateSemantics(std::uint32_t seed,
                                const TerrainGeneratorDefinition& generator,
                                const TerrainGenerationDefinitions& definitions,
                                std::span<const TerrainBiomeId> enabledBiomes,
                                WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::water, 0.0F);
    waterLevel_ = generator.waterLevel * heightScale;
    minimumStartingLandFraction_ = generator.connectivity.minimumStartingLandFraction;
    maximumGenerationAttempts_ = generator.connectivity.maximumGenerationAttempts;
    const TerrainFieldGenerator fieldGenerator(seed, generator);
    const TerrainBiomeGenerator biomeGenerator(definitions, enabledBiomes);
    semanticSamples_.resize(
        static_cast<std::size_t>(semanticCellCount * semanticCellCount));
    const float halfExtent = worldExtent() * 0.5F;
    for (int z = 0; z < semanticCellCount; ++z) {
        if (progress && z % 8 == 0)
            progress->report(WorldGenerationPhase::water,
                             static_cast<float>(z) / semanticCellCount);
        for (int x = 0; x < semanticCellCount; ++x) {
            const float worldX = -halfExtent + (static_cast<float>(x) + 0.5F) * semanticCellSize;
            const float worldZ = -halfExtent + (static_cast<float>(z) + 0.5F) * semanticCellSize;
            TerrainSample& sample = semanticSamples_[static_cast<std::size_t>(
                z * semanticCellCount + x)];
            sample.baseHeight = heightAt(worldX, worldZ);
            sample.waterSurfaceHeight = waterSurfaceAt(worldX, worldZ);
            sample.waterCoverage = waterCoverageAt(worldX, worldZ);
            sample.waterKind = waterKindAt(worldX, worldZ);
            sample.waterDepth = sample.waterCoverage > 0.0F
                                    ? std::max(0.0F, sample.waterSurfaceHeight - sample.baseHeight)
                                    : 0.0F;
            sample.submerged = sample.waterCoverage > 0.01F && sample.waterDepth > 0.0F;
            const int vertexX = std::clamp(static_cast<int>(std::round(
                                               (worldX + halfExtent) / spacing)),
                                           0, cellCount);
            const int vertexZ = std::clamp(static_cast<int>(std::round(
                                               (worldZ + halfExtent) / spacing)),
                                           0, cellCount);
            const std::size_t vertexIndex =
                static_cast<std::size_t>(vertexZ * vertexCount + vertexX);
            sample.drainage = drainage_[vertexIndex];
            sample.riverDistance = std::isfinite(riverDistances_[vertexIndex])
                                       ? riverDistances_[vertexIndex]
                                       : 0.0F;
            sample.riverHalfWidth = riverHalfWidths_[vertexIndex];
            sample.riverFlowDirection = riverFlowDirections_[vertexIndex];
            sample.streamOrder = riverStreamOrders_[vertexIndex];
            sample.floodplain = floodplainFlags_[vertexIndex] != 0;
            sample.riverBank = riverBankFlags_[vertexIndex] != 0;
            sample.sediment = sedimentFlags_[vertexIndex] != 0;
            sample.estuary = estuaryFlags_[vertexIndex] != 0;
            sample.confluence = confluenceFlags_[vertexIndex] != 0;
            sample.river = sample.waterKind == WaterKind::river;
            sample.lake = sample.waterKind == WaterKind::lake;
            sample.wetland = wetlandFlags_[vertexIndex] != 0;
            const float dx = (heightAt(worldX + semanticCellSize, worldZ) -
                              heightAt(worldX - semanticCellSize, worldZ)) /
                             (2.0F * semanticCellSize);
            const float dz = (heightAt(worldX, worldZ + semanticCellSize) -
                              heightAt(worldX, worldZ - semanticCellSize)) /
                             (2.0F * semanticCellSize);
            sample.slopeDegrees = glm::degrees(std::atan(std::sqrt(dx * dx + dz * dz)));
            const TerrainRegionalFields fields = fieldGenerator.sample(worldX, worldZ);
            sample.continentalness = fields.continentalness;
            sample.erosion = fields.erosion;
            sample.peaks = fields.peaks;
            sample.moisture = fields.moisture;
            sample.temperature = fields.temperature;
            sample.plains = fields.plains;
            sample.hills = fields.hills;
            sample.mountainBelt = fields.mountainBelt;
            sample.rockyOutcrops = fields.rockyOutcrops;
            sample.coastalShelf = fields.coastalShelf;
            sample.basin = fields.basin;
            const TerrainBiomeDefinition* biome = &biomeGenerator.classify(
                fields, sample.baseHeight / heightScale, sample.slopeDegrees);
            if (sample.submerged) {
                const std::string waterBiome =
                    sample.waterKind == WaterKind::ocean && sample.waterDepth > 1.2F
                        ? "deep_water"
                        : "shallow_water";
                const auto water = std::find_if(
                    definitions.biomes().begin(), definitions.biomes().end(),
                    [&](const TerrainBiomeDefinition& item) { return item.id.value == waterBiome; });
                if (water != definitions.biomes().end()) biome = &*water;
            }
            sample.biome = biome->id;
            sample.surface = biome->surface;
            const auto surface = std::find_if(
                definitions.surfaces().begin(),
                definitions.surfaces().end(),
                [&](const TerrainSurfaceDefinition& item) { return item.id == biome->surface; });
            if (surface == definitions.surfaces().end())
                throw std::runtime_error("Terrain sample references an unknown surface");
            sample.surfaceColor = {surface->color[0], surface->color[1], surface->color[2]};
            sample.tags = 0;
            for (const std::string& tag : surface->tags)
                sample.tags |= terrainTagFromName(tag);
            sample.materialWeights = {surface->materialWeights[0],
                                      surface->materialWeights[1],
                                      surface->materialWeights[2],
                                      surface->materialWeights[3]};
            sample.materialWeights /= sample.materialWeights.x + sample.materialWeights.y +
                                      sample.materialWeights.z + sample.materialWeights.w;
            if (sample.river) {
                sample.materialWeights = sample.sediment
                                             ? glm::vec4{0.0F, 0.82F, 0.12F, 0.06F}
                                             : glm::vec4{0.0F, 0.58F, 0.42F, 0.0F};
                sample.surfaceColor = sample.sediment ? glm::vec3{0.43F, 0.36F, 0.22F}
                                                       : glm::vec3{0.18F, 0.25F, 0.22F};
            } else if (sample.riverBank) {
                sample.materialWeights = sample.sediment
                                             ? glm::vec4{0.04F, 0.82F, 0.04F, 0.10F}
                                             : glm::vec4{0.18F, 0.70F, 0.08F, 0.04F};
                sample.surfaceColor = {0.36F, 0.34F, 0.20F};
            } else if (sample.wetland) {
                sample.materialWeights = {0.42F, 0.54F, 0.02F, 0.02F};
                sample.surfaceColor = {0.25F, 0.38F, 0.20F};
            } else if (sample.floodplain) {
                sample.materialWeights = {0.58F, 0.38F, 0.02F, 0.02F};
                sample.surfaceColor = {0.29F, 0.44F, 0.21F};
            }
            sample.traversal = traversalClass(biome->traversal);
            sample.buildability = buildabilityClass(biome->buildability);
            sample.movementCosts = biome->movementCosts;
            if (sample.submerged) {
                sample.tags |= terrainTagBit(TerrainTag::water) |
                               terrainTagBit(TerrainTag::submerged);
                sample.tags |= biome->id.value == "deep_water"
                                   ? terrainTagBit(TerrainTag::deepWater)
                                   : terrainTagBit(TerrainTag::shallowWater);
                if (sample.river) sample.tags |= terrainTagBit(TerrainTag::river);
                if (sample.lake) sample.tags |= terrainTagBit(TerrainTag::lake);
                sample.tags &= ~terrainTagBit(TerrainTag::land);
            } else {
                sample.tags |= terrainTagBit(TerrainTag::land);
                sample.tags &= ~(terrainTagBit(TerrainTag::water) |
                                 terrainTagBit(TerrainTag::submerged));

                if (sample.wetland) {
                    sample.tags |= terrainTagBit(TerrainTag::wetland) |
                                   terrainTagBit(TerrainTag::shoreline);
                    sample.buildability = TerrainBuildabilityClass::restricted;
                    sample.movementCosts[static_cast<std::size_t>(MovementDomain::land)] = 1.35F;
                }

                const float normalizedHeight = sample.baseHeight / heightScale;
                const bool mountainSignal =
                    normalizedHeight >= generator.barriers.minimumHeight &&
                    sample.peaks >= generator.barriers.minimumPeak;
                const bool cliffSignal =
                    sample.slopeDegrees >= generator.barriers.cliffSlopeDegrees;
                if (mountainSignal || cliffSignal) {
                    const bool deliberatePass =
                        sample.erosion >= generator.barriers.passMinimumErosion &&
                        sample.slopeDegrees <= generator.barriers.passMaximumSlopeDegrees;
                    if (deliberatePass) {
                        sample.traversal = TerrainTraversalClass::difficult;
                        sample.buildability = TerrainBuildabilityClass::restricted;
                        sample.movementCosts[static_cast<std::size_t>(MovementDomain::land)] =
                            generator.barriers.passMovementCost;
                        sample.movementCosts[static_cast<std::size_t>(MovementDomain::water)] =
                            0.0F;
                        sample.movementCosts[static_cast<std::size_t>(MovementDomain::air)] = 1.0F;
                        sample.tags |= terrainTagBit(TerrainTag::mountainPass);
                        sample.tags &= ~terrainTagBit(TerrainTag::mountainBarrier);
                    } else {
                        sample.traversal = TerrainTraversalClass::impassable;
                        sample.buildability = TerrainBuildabilityClass::forbidden;
                        sample.movementCosts[static_cast<std::size_t>(MovementDomain::land)] = 0.0F;
                        sample.movementCosts[static_cast<std::size_t>(MovementDomain::water)] = 0.0F;
                        sample.movementCosts[static_cast<std::size_t>(MovementDomain::air)] = 1.0F;
                        sample.tags |= terrainTagBit(TerrainTag::mountainBarrier) |
                                       terrainTagBit(TerrainTag::noBuild);
                        sample.tags &= ~terrainTagBit(TerrainTag::buildable);
                    }
                }
                if ((sample.tags & terrainTagBit(TerrainTag::universalBarrier)) != 0) {
                    sample.traversal = TerrainTraversalClass::impassable;
                    sample.buildability = TerrainBuildabilityClass::forbidden;
                    sample.movementCosts.fill(0.0F);
                }
            }
            if (sample.riverBank)
                sample.tags |= terrainTagBit(TerrainTag::riverBank) |
                               terrainTagBit(TerrainTag::shoreline);
            if (sample.floodplain) sample.tags |= terrainTagBit(TerrainTag::floodplain);
            if (sample.sediment) sample.tags |= terrainTagBit(TerrainTag::sediment);
            if (sample.estuary) sample.tags |= terrainTagBit(TerrainTag::estuary);
            if (sample.confluence) sample.tags |= terrainTagBit(TerrainTag::confluence);
        }
    }
    for (int z = 0; z < semanticCellCount; ++z)
        for (int x = 0; x < semanticCellCount; ++x) {
            TerrainSample& sample = semanticSamples_[static_cast<std::size_t>(
                z * semanticCellCount + x)];
            bool oppositeMedium = false;
            for (const glm::ivec2 offset : {glm::ivec2{-1, 0}, glm::ivec2{1, 0},
                                            glm::ivec2{0, -1}, glm::ivec2{0, 1}}) {
                const int nx = x + offset.x, nz = z + offset.y;
                if (nx < 0 || nz < 0 || nx >= semanticCellCount || nz >= semanticCellCount)
                    continue;
                if (semanticSamples_[static_cast<std::size_t>(nz * semanticCellCount + nx)]
                        .submerged != sample.submerged) {
                    oppositeMedium = true;
                    break;
                }
            }
            if (oppositeMedium) {
                sample.tags |= terrainTagBit(TerrainTag::shoreline);
                const TerrainTagMask mountainTags =
                    terrainTagBit(TerrainTag::mountainBarrier) |
                    terrainTagBit(TerrainTag::mountainPass);
                if (!sample.submerged && sample.baseHeight <= waterLevel_ + 2.4F &&
                    (sample.tags & mountainTags) == 0) {
                    std::string coastId;
                    if (sample.slopeDegrees <= 11.0F && sample.peaks < 0.68F)
                        coastId = "coastal_beach";
                    else if (sample.slopeDegrees >= 16.0F || sample.peaks >= 0.55F)
                        coastId = "rocky_coast";
                    if (!coastId.empty()) {
                        const auto coast = std::find_if(
                            definitions.biomes().begin(), definitions.biomes().end(),
                            [&](const TerrainBiomeDefinition& item) {
                                return item.id.value == coastId;
                            });
                        if (coast != definitions.biomes().end()) {
                            const auto surface = std::find_if(
                                definitions.surfaces().begin(), definitions.surfaces().end(),
                                [&](const TerrainSurfaceDefinition& item) {
                                    return item.id == coast->surface;
                                });
                            if (surface != definitions.surfaces().end()) {
                                sample.biome = coast->id;
                                sample.surface = coast->surface;
                                sample.surfaceColor = {surface->color[0], surface->color[1],
                                                       surface->color[2]};
                                sample.materialWeights = {
                                    surface->materialWeights[0], surface->materialWeights[1],
                                    surface->materialWeights[2], surface->materialWeights[3]};
                                sample.materialWeights /= sample.materialWeights.x +
                                                          sample.materialWeights.y +
                                                          sample.materialWeights.z +
                                                          sample.materialWeights.w;
                                sample.traversal = traversalClass(coast->traversal);
                                sample.buildability = buildabilityClass(coast->buildability);
                                sample.movementCosts = coast->movementCosts;
                                for (const std::string& tag : surface->tags)
                                    sample.tags |= terrainTagFromName(tag);
                                sample.tags |= terrainTagBit(TerrainTag::shoreline);
                            }
                        }
                    }
                }
            }
        }
    if (progress) progress->report(WorldGenerationPhase::water, 1.0F);
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
    if (gridX < 0.0F || gridZ < 0.0F || gridX > static_cast<float>(cellCount) ||
        gridZ > static_cast<float>(cellCount)) {
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
    return h11 + (1.0F - u) * (h01 - h11) + (1.0F - v) * (h10 - h11);
}

float Terrain::waterSurfaceAt(float worldX, float worldZ) const {
    const float halfExtent = worldExtent() * 0.5F;
    const float gridX = (worldX + halfExtent) / spacing;
    const float gridZ = (worldZ + halfExtent) / spacing;
    if (gridX < 0.0F || gridZ < 0.0F || gridX > static_cast<float>(cellCount) ||
        gridZ > static_cast<float>(cellCount) || waterSurfaceHeights_.empty())
        return heightAt(worldX, worldZ);
    const int x0 = std::min(static_cast<int>(std::floor(gridX)), cellCount - 1);
    const int z0 = std::min(static_cast<int>(std::floor(gridZ)), cellCount - 1);
    const float u = gridX - static_cast<float>(x0);
    const float v = gridZ - static_cast<float>(z0);
    const auto surface = [&](int x, int z) {
        return waterSurfaceHeights_[static_cast<std::size_t>(z * vertexCount + x)] * heightScale;
    };
    const float h00 = surface(x0, z0);
    const float h10 = surface(x0 + 1, z0);
    const float h01 = surface(x0, z0 + 1);
    const float h11 = surface(x0 + 1, z0 + 1);
    if (u + v <= 1.0F)
        return h00 + u * (h10 - h00) + v * (h01 - h00);
    return h11 + (1.0F - u) * (h01 - h11) + (1.0F - v) * (h10 - h11);
}

float Terrain::waterCoverageAt(float worldX, float worldZ) const {
    const float halfExtent = worldExtent() * 0.5F;
    const float gridX = (worldX + halfExtent) / spacing;
    const float gridZ = (worldZ + halfExtent) / spacing;
    if (gridX < 0.0F || gridZ < 0.0F || gridX > static_cast<float>(cellCount) ||
        gridZ > static_cast<float>(cellCount) || waterCoverage_.empty())
        return 0.0F;
    const int x0 = std::min(static_cast<int>(std::floor(gridX)), cellCount - 1);
    const int z0 = std::min(static_cast<int>(std::floor(gridZ)), cellCount - 1);
    const float u = gridX - static_cast<float>(x0);
    const float v = gridZ - static_cast<float>(z0);
    const auto coverage = [&](int x, int z) {
        return waterCoverage_[static_cast<std::size_t>(z * vertexCount + x)];
    };
    const float c00 = coverage(x0, z0), c10 = coverage(x0 + 1, z0);
    const float c01 = coverage(x0, z0 + 1), c11 = coverage(x0 + 1, z0 + 1);
    const float interpolated =
        u + v <= 1.0F ? c00 + u * (c10 - c00) + v * (c01 - c00)
                      : c11 + (1.0F - u) * (c01 - c11) +
                            (1.0F - v) * (c10 - c11);
    return glm::clamp(interpolated, 0.0F, 1.0F);
}

float Terrain::generatedWaterSurfaceAt(float worldX, float worldZ) const {
    const float halfExtent = worldExtent() * 0.5F;
    const float gridX = (worldX + halfExtent) / spacing;
    const float gridZ = (worldZ + halfExtent) / spacing;
    if (gridX < 0.0F || gridZ < 0.0F || gridX > static_cast<float>(cellCount) ||
        gridZ > static_cast<float>(cellCount) || generatedWaterSurfaceHeights_.empty())
        return 0.0F;
    const int x0 = std::clamp(static_cast<int>(std::floor(gridX)), 0, cellCount);
    const int z0 = std::clamp(static_cast<int>(std::floor(gridZ)), 0, cellCount);
    const int x1 = std::min(x0 + 1, cellCount);
    const int z1 = std::min(z0 + 1, cellCount);
    const float tx = gridX - static_cast<float>(x0);
    const float tz = gridZ - static_cast<float>(z0);
    const auto at = [&](int x, int z) {
        return generatedWaterSurfaceHeights_[static_cast<std::size_t>(z * vertexCount + x)];
    };
    return std::lerp(std::lerp(at(x0, z0), at(x1, z0), tx),
                     std::lerp(at(x0, z1), at(x1, z1), tx), tz) * heightScale;
}

float Terrain::generatedWaterCoverageAt(float worldX, float worldZ) const {
    const float halfExtent = worldExtent() * 0.5F;
    const float gridX = (worldX + halfExtent) / spacing;
    const float gridZ = (worldZ + halfExtent) / spacing;
    if (gridX < 0.0F || gridZ < 0.0F || gridX > static_cast<float>(cellCount) ||
        gridZ > static_cast<float>(cellCount) || generatedWaterCoverage_.empty())
        return 0.0F;
    const int x0 = std::clamp(static_cast<int>(std::floor(gridX)), 0, cellCount);
    const int z0 = std::clamp(static_cast<int>(std::floor(gridZ)), 0, cellCount);
    const int x1 = std::min(x0 + 1, cellCount);
    const int z1 = std::min(z0 + 1, cellCount);
    const float tx = gridX - static_cast<float>(x0);
    const float tz = gridZ - static_cast<float>(z0);
    const auto at = [&](int x, int z) {
        return generatedWaterCoverage_[static_cast<std::size_t>(z * vertexCount + x)];
    };
    return std::lerp(std::lerp(at(x0, z0), at(x1, z0), tx),
                     std::lerp(at(x0, z1), at(x1, z1), tx), tz);
}

WaterKind Terrain::waterKindAt(float worldX, float worldZ) const {
    if (waterKinds_.empty()) return WaterKind::none;
    const float halfExtent = worldExtent() * 0.5F;
    const int x = std::clamp(static_cast<int>(std::round((worldX + halfExtent) / spacing)),
                             0, cellCount);
    const int z = std::clamp(static_cast<int>(std::round((worldZ + halfExtent) / spacing)),
                             0, cellCount);
    return waterKinds_[static_cast<std::size_t>(z * vertexCount + x)];
}

float Terrain::riverDistanceAt(float worldX, float worldZ) const {
    if (riverDistances_.empty()) return std::numeric_limits<float>::infinity();
    const float halfExtent = worldExtent() * 0.5F;
    const int x = std::clamp(static_cast<int>(std::round((worldX + halfExtent) / spacing)),
                             0, cellCount);
    const int z = std::clamp(static_cast<int>(std::round((worldZ + halfExtent) / spacing)),
                             0, cellCount);
    return riverDistances_[static_cast<std::size_t>(z * vertexCount + x)];
}

glm::vec2 Terrain::riverFlowAt(float worldX, float worldZ) const {
    if (riverFlowDirections_.empty()) return {0.0F, 0.0F};
    const float halfExtent = worldExtent() * 0.5F;
    const float gridX = std::clamp((worldX + halfExtent) / spacing, 0.0F,
                                   static_cast<float>(cellCount));
    const float gridZ = std::clamp((worldZ + halfExtent) / spacing, 0.0F,
                                   static_cast<float>(cellCount));
    const int x0 = static_cast<int>(std::floor(gridX));
    const int z0 = static_cast<int>(std::floor(gridZ));
    const int x1 = std::min(x0 + 1, cellCount);
    const int z1 = std::min(z0 + 1, cellCount);
    const float tx = gridX - static_cast<float>(x0);
    const float tz = gridZ - static_cast<float>(z0);
    const auto at = [&](int x, int z) {
        return riverFlowDirections_[static_cast<std::size_t>(z * vertexCount + x)];
    };
    const glm::vec2 flow = glm::mix(glm::mix(at(x0, z0), at(x1, z0), tx),
                                    glm::mix(at(x0, z1), at(x1, z1), tx), tz);
    return glm::length(flow) > 0.0001F ? glm::normalize(flow) : glm::vec2{0.0F};
}

int Terrain::semanticIndex(float worldX, float worldZ) const {
    const float halfExtent = worldExtent() * 0.5F;
    const int x = std::clamp(static_cast<int>(std::floor((worldX + halfExtent) /
                                                        semanticCellSize)),
                             0,
                             semanticCellCount - 1);
    const int z = std::clamp(static_cast<int>(std::floor((worldZ + halfExtent) /
                                                        semanticCellSize)),
                             0,
                             semanticCellCount - 1);
    return z * semanticCellCount + x;
}

const TerrainSample& Terrain::sampleAt(float worldX, float worldZ) const {
    return semanticSamples_[static_cast<std::size_t>(semanticIndex(worldX, worldZ))];
}

TerrainBiomeId Terrain::biomeAt(float worldX, float worldZ) const {
    return sampleAt(worldX, worldZ).biome;
}

TerrainTraversalClass Terrain::traversalAt(float worldX, float worldZ) const {
    return sampleAt(worldX, worldZ).traversal;
}

float Terrain::movementCostAt(float worldX, float worldZ, MovementDomainMask domains) const {
    const TerrainSample& sample = sampleAt(worldX, worldZ);
    float result = std::numeric_limits<float>::max();
    for (const MovementDomain domain : movementDomains) {
        if (!hasMovementDomain(domains, domain)) continue;
        const float cost = sample.movementCosts[static_cast<std::size_t>(domain)];
        if (cost > 0.0F) result = std::min(result, cost);
    }
    return result == std::numeric_limits<float>::max() ? 0.0F : result;
}

bool Terrain::isBuildableAt(float worldX, float worldZ) const {
    return sampleAt(worldX, worldZ).buildability == TerrainBuildabilityClass::buildable;
}

glm::vec3 Terrain::normalAt(int x, int z) const {
    const float left = vertexHeight(x - 1, z);
    const float right = vertexHeight(x + 1, z);
    const float down = vertexHeight(x, z - 1);
    const float up = vertexHeight(x, z + 1);
    return glm::normalize(glm::vec3{left - right, 2.0F * spacing, down - up});
}

glm::vec3 Terrain::colorAt(float worldX, float worldZ) const {
    const float halfExtent = worldExtent() * 0.5F;
    const float gridX = (worldX + halfExtent) / semanticCellSize - 0.5F;
    const float gridZ = (worldZ + halfExtent) / semanticCellSize - 0.5F;
    const int x0 = std::clamp(static_cast<int>(std::floor(gridX)), 0, semanticCellCount - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor(gridZ)), 0, semanticCellCount - 1);
    const int x1 = std::min(x0 + 1, semanticCellCount - 1);
    const int z1 = std::min(z0 + 1, semanticCellCount - 1);
    const float tx = glm::clamp(gridX - std::floor(gridX), 0.0F, 1.0F);
    const float tz = glm::clamp(gridZ - std::floor(gridZ), 0.0F, 1.0F);
    const auto color = [&](int x, int z) {
        return semanticSamples_[static_cast<std::size_t>(z * semanticCellCount + x)].surfaceColor;
    };
    return glm::mix(glm::mix(color(x0, z0), color(x1, z0), tx),
                    glm::mix(color(x0, z1), color(x1, z1), tx),
                    tz);
}

glm::vec4 Terrain::materialWeightsAt(float worldX, float worldZ) const {
    const float halfExtent = worldExtent() * 0.5F;
    const float gridX = (worldX + halfExtent) / semanticCellSize - 0.5F;
    const float gridZ = (worldZ + halfExtent) / semanticCellSize - 0.5F;
    const int x0 = std::clamp(static_cast<int>(std::floor(gridX)), 0, semanticCellCount - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor(gridZ)), 0, semanticCellCount - 1);
    const int x1 = std::min(x0 + 1, semanticCellCount - 1);
    const int z1 = std::min(z0 + 1, semanticCellCount - 1);
    const float tx = glm::clamp(gridX - std::floor(gridX), 0.0F, 1.0F);
    const float tz = glm::clamp(gridZ - std::floor(gridZ), 0.0F, 1.0F);
    const auto weights = [&](int x, int z) {
        return semanticSamples_[static_cast<std::size_t>(z * semanticCellCount + x)]
            .materialWeights;
    };
    glm::vec4 result = glm::mix(glm::mix(weights(x0, z0), weights(x1, z0), tx),
                                glm::mix(weights(x0, z1), weights(x1, z1), tx),
                                tz);
    return result / (result.x + result.y + result.z + result.w);
}

float Terrain::worldExtent() const {
    return static_cast<float>(cellCount) * spacing;
}

FootprintFit Terrain::fitFootprint(float worldX,
                                   float worldZ,
                                   float radius,
                                   float maximumSlopeDegrees) const {
    const std::array<glm::vec2, 9> offsets{{{0, 0}, {-1, -1}, {0, -1}, {1, -1},
                                            {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    float total = 0.0F;
    for (const glm::vec2 offset : offsets) {
        const float height = heightAt(worldX + offset.x * radius, worldZ + offset.y * radius);
        minimum = std::min(minimum, height);
        maximum = std::max(maximum, height);
        total += height;
    }
    const float run = std::max(radius * 2.0F, spacing);
    const float slope = glm::degrees(std::atan2(maximum - minimum, run));
    const float sampleStep = std::max(radius * 0.5F, spacing);
    const float dx = (heightAt(worldX + sampleStep, worldZ) - heightAt(worldX - sampleStep, worldZ)) /
                     (2.0F * sampleStep);
    const float dz = (heightAt(worldX, worldZ + sampleStep) - heightAt(worldX, worldZ - sampleStep)) /
                     (2.0F * sampleStep);
    return {total / static_cast<float>(offsets.size()), slope, {dx, dz}, slope <= maximumSlopeDegrees};
}

FootprintFit Terrain::fitFootprint(float worldX, float worldZ,
                                   const TerrainFootprint& footprint) const {
    constexpr int samples = 5;
    const float radians = glm::radians(footprint.rotationDegrees);
    const float cosine = std::cos(radians), sine = std::sin(radians);
    float total = 0.0F;
    float xx = 0.0F, xz = 0.0F, zz = 0.0F, xh = 0.0F, zh = 0.0F;
    std::size_t count = 0;
    for (int z = 0; z < samples; ++z)
        for (int x = 0; x < samples; ++x) {
            const glm::vec2 uv{-1.0F + 2.0F * x / (samples - 1.0F),
                               -1.0F + 2.0F * z / (samples - 1.0F)};
            if (footprint.shape == FootprintShape::circle && glm::dot(uv, uv) > 1.0F)
                continue;
            const glm::vec2 extent = footprint.shape == FootprintShape::circle
                                         ? glm::vec2{footprint.radius}
                                         : footprint.halfExtents;
            const glm::vec2 local = uv * extent;
            const glm::vec2 rotated{cosine * local.x - sine * local.y,
                                    sine * local.x + cosine * local.y};
            const float height = heightAt(worldX + rotated.x, worldZ + rotated.y);
            total += height;
            xx += rotated.x * rotated.x;
            xz += rotated.x * rotated.y;
            zz += rotated.y * rotated.y;
            xh += rotated.x * height;
            zh += rotated.y * height;
            ++count;
        }
    const float determinant = xx * zz - xz * xz;
    const glm::vec2 gradient = std::abs(determinant) > 0.000001F
                                   ? glm::vec2{(xh * zz - zh * xz) / determinant,
                                               (zh * xx - xh * xz) / determinant}
                                   : glm::vec2{0.0F};
    const float slope = glm::degrees(std::atan(glm::length(gradient)));
    return {total / static_cast<float>(std::max<std::size_t>(count, 1)), slope, gradient,
            slope <= footprint.maximumTiltDegrees};
}

TerrainPlacementResult Terrain::evaluatePlacement(
    float worldX, float worldZ, const TerrainFootprint& footprint,
    TerrainPlacementProfile profile) const {
    TerrainPlacementResult result;
    result.fit = fitFootprint(worldX, worldZ, footprint);
    if (!result.fit.valid) {
        result.failure = TerrainPlacementFailure::excessiveSlope;
        return result;
    }
    constexpr int samples = 7;
    const float radians = glm::radians(footprint.rotationDegrees);
    const float cosine = std::cos(radians), sine = std::sin(radians);
    bool sawLand = false;
    bool sawWater = false;
    for (int z = 0; z < samples; ++z) {
        for (int x = 0; x < samples; ++x) {
            const glm::vec2 uv{-1.0F + 2.0F * x / (samples - 1.0F),
                               -1.0F + 2.0F * z / (samples - 1.0F)};
            if (footprint.shape == FootprintShape::circle && glm::dot(uv, uv) > 1.0F)
                continue;
            const glm::vec2 extent = footprint.shape == FootprintShape::circle
                                         ? glm::vec2{footprint.radius}
                                         : footprint.halfExtents;
            const glm::vec2 local = uv * extent;
            const glm::vec2 offset{cosine * local.x - sine * local.y,
                                   sine * local.x + cosine * local.y};
            const TerrainSample& sample = sampleAt(worldX + offset.x, worldZ + offset.y);
            TerrainPlacementDomain domain = TerrainPlacementDomain::land;
            if (sample.submerged) {
                sawWater = true;
                domain = sample.biome.value == "deep_water"
                             ? TerrainPlacementDomain::deepWater
                             : TerrainPlacementDomain::shallowWater;
            } else {
                sawLand = true;
                if (sample.buildability == TerrainBuildabilityClass::forbidden) {
                    result.failure = TerrainPlacementFailure::forbiddenTerrain;
                    return result;
                }
            }
            if ((profile.domains & terrainPlacementBit(domain)) == 0) {
                result.failure = TerrainPlacementFailure::forbiddenTerrain;
                return result;
            }
        }
    }
    if (profile.requiresShore && !(sawLand && sawWater))
        result.failure = TerrainPlacementFailure::shoreRequired;
    return result;
}

TerrainFoundation Terrain::evaluateFoundation(std::uint64_t sourceEntity, float worldX,
                                               float worldZ,
                                               const TerrainFootprint& footprint) const {
    FootprintFit fit = fitFootprint(worldX, worldZ, footprint);
    glm::vec2 gradient = fit.gradient;
    // Buildings follow gentle terrain directly. On moderate slopes a recessed slab
    // absorbs the excess while the building itself never tilts beyond five degrees.
    constexpr float maximumBuildingTiltDegrees = 5.0F;
    const float maximumGradient = std::tan(glm::radians(maximumBuildingTiltDegrees));
    const float magnitude = glm::length(gradient);
    if (magnitude > maximumGradient && magnitude > 0.0F)
        gradient *= maximumGradient / magnitude;
    TerrainFoundation foundation{sourceEntity, {worldX, fit.height, worldZ}, footprint, gradient};
    foundation.sourceSlopeDegrees = fit.slopeDegrees;
    foundation.requiresSlab = fit.slopeDegrees > maximumBuildingTiltDegrees;
    return foundation;
}

float Terrain::signedDistanceToFootprint(const TerrainFoundation& foundation,
                                         glm::vec2 worldPosition) {
    const glm::vec2 delta = worldPosition - glm::vec2{foundation.center.x, foundation.center.z};
    if (foundation.shape == FootprintShape::circle)
        return glm::length(delta) - foundation.outerRadius;
    const float radians = glm::radians(foundation.rotationDegrees);
    const glm::vec2 local{std::cos(radians) * delta.x + std::sin(radians) * delta.y,
                          -std::sin(radians) * delta.x + std::cos(radians) * delta.y};
    const glm::vec2 outside = glm::max(glm::abs(local) - foundation.halfExtents, glm::vec2{0.0F});
    const float outsideDistance = glm::length(outside);
    const float insideDistance = std::min(
        std::max(std::abs(local.x) - foundation.halfExtents.x,
                 std::abs(local.y) - foundation.halfExtents.y),
        0.0F);
    return outsideDistance + insideDistance;
}

void Terrain::applyFoundation(const TerrainFoundation& foundation) {
    const float halfExtent = worldExtent() * 0.5F;
    // Restrict deformation to the foundation's bounded region. The previous full-map
    // pass touched ~263k vertices for every placement, causing a visible hitch.
    const float affectedRadius = foundation.outerRadius + foundation.edgeFalloff;
    const int minX = std::max(0, static_cast<int>((foundation.center.x - affectedRadius + halfExtent) / spacing) - 1);
    const int maxX = std::min(vertexCount - 1, static_cast<int>((foundation.center.x + affectedRadius + halfExtent) / spacing) + 1);
    const int minZ = std::max(0, static_cast<int>((foundation.center.z - affectedRadius + halfExtent) / spacing) - 1);
    const int maxZ = std::min(vertexCount - 1, static_cast<int>((foundation.center.z + affectedRadius + halfExtent) / spacing) + 1);
    const int minChunkX = std::max(0, (std::max(0, minX - 1)) / chunkCellCount),
              maxChunkX = std::min(chunksPerSide - 1, std::min(vertexCount - 1, maxX + 1) / chunkCellCount);
    const int minChunkZ = std::max(0, (std::max(0, minZ - 1)) / chunkCellCount),
              maxChunkZ = std::min(chunksPerSide - 1, std::min(vertexCount - 1, maxZ + 1) / chunkCellCount);
    for (int cz = minChunkZ; cz <= maxChunkZ; ++cz)
        for (int cx = minChunkX; cx <= maxChunkX; ++cx)
            if (std::find(dirtyChunks_.begin(), dirtyChunks_.end(), std::pair{cx, cz}) == dirtyChunks_.end())
                dirtyChunks_.emplace_back(cx, cz);
    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const float worldX = static_cast<float>(x) * spacing - halfExtent;
            const float worldZ = static_cast<float>(z) * spacing - halfExtent;
            const glm::vec2 delta{worldX - foundation.center.x, worldZ - foundation.center.z};
            const float signedDistance = signedDistanceToFootprint(foundation, {worldX, worldZ});
            const float falloff = std::max(foundation.edgeFalloff, spacing);
            if (signedDistance >= falloff)
                continue;
            const std::size_t vertexIndex = static_cast<std::size_t>(z * vertexCount + x);
            // Foundations alter land only. Explicit water coverage prevents flattening near a
            // shore from either deforming a river/lake bed or creating a false water halo.
            const float dryInfluence = 1.0F - glm::clamp(waterCoverage_[vertexIndex], 0.0F, 1.0F);
            const float blend = (signedDistance <= 0.0F
                                     ? 1.0F
                                     : 1.0F - smoothstep(0.0F, falloff, signedDistance)) *
                                glm::clamp(foundation.influence, 0.0F, 1.0F) * dryInfluence;
            if (blend <= 0.0F) continue;
            float& normalized = heights_[vertexIndex];
            const float planeHeight = (foundation.center.y +
                                       foundation.gradient.x * delta.x +
                                       foundation.gradient.y * delta.y) / heightScale;
            normalized = glm::mix(normalized,
                                  planeHeight,
                                  blend);
        }
    }
}

void Terrain::rebuildFoundations(const std::vector<TerrainFoundation>& foundations) {
    // Mark the previous regions too, so removing or shrinking a foundation restores their VBOs.
    for (const TerrainFoundation& foundation : appliedFoundations_)
        applyFoundation(foundation);
    if (baseHeights_.size() == heights_.size())
        heights_ = baseHeights_;
    for (const TerrainFoundation& foundation : foundations)
        applyFoundation(foundation);
    appliedFoundations_ = foundations;
}

} // namespace strategy
