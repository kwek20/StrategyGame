#include "terrain/TerrainGenerationPipeline.hpp"

#include "terrain/Terrain.hpp"
#include "terrain/TerrainLayoutGenerator.hpp"
#include "terrain/TerrainWaterGenerator.hpp"
#include "world/GenerationProgress.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <stdexcept>

namespace strategy {
namespace {

rapidjson::Document loadManifest(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Could not open custom terrain map: " + path.string());
    rapidjson::IStreamWrapper input(stream);
    rapidjson::Document document;
    document.ParseStream(input);
    if (document.HasParseError() || !document.IsObject())
        throw std::runtime_error("Invalid custom terrain map: " + path.string());
    return document;
}

std::vector<TerrainBiomeId> dryBiomes(
    const TerrainGenerationDefinitions& definitions,
    std::span<const TerrainBiomeId> configured) {
    std::vector<TerrainBiomeId> result;
    if (configured.empty()) {
        for (const TerrainBiomeDefinition& biome : definitions.biomes())
            if (biome.enabled && biome.id.value != "deep_water" &&
                biome.id.value != "shallow_water")
                result.push_back(biome.id);
    } else {
        for (const TerrainBiomeId& biome : configured)
            if (biome.value != "deep_water" && biome.value != "shallow_water")
                result.push_back(biome);
    }
    return result;
}

} // namespace

void TerrainGenerationPipeline::generate(
    Terrain& terrain, std::uint32_t seed, const TerrainLayoutDefinition& layout,
    const TerrainGenerationDefinitions& definitions, WorldGenerationProgress* progress) {
    const TerrainGeneratorDefinition& generator = definitions.generator(layout.generator);
    if (layout.source == TerrainLayoutSource::procedural) {
        terrain.generateHeightfield(seed, generator, progress);
        TerrainLayoutGenerator::apply(terrain.heights_, Terrain::vertexCount, seed, layout,
                                      generator.waterLevel);
    } else
        loadCustomHeightfield(terrain, layout.customMap, progress);

    if (layout.hydrologyEnabled)
        TerrainWaterGenerator::generate(terrain, seed, generator, progress);
    else
        TerrainWaterGenerator::clear(terrain, progress);
    terrain.baseHeights_ = terrain.heights_;
    const std::vector<TerrainBiomeId> enabledBiomes =
        layout.hydrologyEnabled ? layout.enabledBiomes
                                : dryBiomes(definitions, layout.enabledBiomes);
    terrain.generateSemantics(seed, generator, definitions, enabledBiomes, progress);
}

void TerrainGenerationPipeline::generateV2(Terrain& terrain,
                                           std::uint32_t seed,
                                           const TerrainLayoutDefinition& layout,
                                           const TerrainGenerationDefinitions& definitions,
                                           std::uint32_t mapChunksPerSide,
                                           WorldGenerationProgress* progress) {
    static const auto recipes = TerrainV2Recipes::load();
    auto recipe = recipes.resolve(layout);
    const float extent =
        static_cast<float>(mapChunksPerSide * Terrain::chunkCellCount) * Terrain::spacing;
    auto landscape = TerrainV2Generator::generate(
        seed, std::move(recipe), definitions.generator(layout.generator), extent, 2, progress);
    assembleV2(terrain, seed, layout, definitions, std::move(landscape), progress);
}

void TerrainGenerationPipeline::assembleV2(Terrain& terrain,
                                           std::uint32_t seed,
                                           const TerrainLayoutDefinition& layout,
                                           const TerrainGenerationDefinitions& definitions,
                                           TerrainV2Landscape landscape,
                                           WorldGenerationProgress* progress) {
    terrain.v2_ = std::move(landscape);
    terrain.mode_ = TerrainMode::v2;
    terrain.heights_.resize(static_cast<std::size_t>(Terrain::vertexCount) * Terrain::vertexCount);
    const float halfExtent = terrain.worldExtent() * 0.5F;
    for (int z = 0; z < Terrain::vertexCount; ++z) {
        if (progress && z % 16 == 0)
            progress->throwIfCancellationRequested();
        for (int x = 0; x < Terrain::vertexCount; ++x) {
            const glm::vec2 position{x * Terrain::spacing - halfExtent,
                                     z * Terrain::spacing - halfExtent};
            terrain.heights_[static_cast<std::size_t>(z * Terrain::vertexCount + x)] =
                terrain.v2_->terraces.surfaceAt(position) / Terrain::heightScale;
        }
    }
    TerrainWaterGenerator::clear(terrain, progress);
    const auto& plan = terrain.v2_->plan;
    for (float dimension :
         {plan.recipe().cells.side, plan.recipe().cells.cliffRun, plan.recipe().cells.rampRun})
        if (std::abs(dimension / Terrain::spacing - std::round(dimension / Terrain::spacing)) >
            0.001F)
            throw std::runtime_error(
                "V2 transitions must align with the authoritative surface raster");
    if (plan.hasWater()) {
        const float level = plan.recipe().waterLevel;
        terrain.waterLevel_ = level / Terrain::heightScale;
        for (int z = 0; z < Terrain::vertexCount; ++z) {
            if (progress && z % 16 == 0)
                progress->throwIfCancellationRequested();
            for (int x = 0; x < Terrain::vertexCount; ++x) {
                const auto index = static_cast<std::size_t>(z * Terrain::vertexCount + x);
                const glm::vec2 position{x * Terrain::spacing - halfExtent,
                                         z * Terrain::spacing - halfExtent};
                const bool wet = plan.waterDistance(position) < plan.recipe().bankWidth * 2.0F &&
                                 terrain.heights_[index] * Terrain::heightScale < level;
                terrain.waterSurfaceHeights_[index] = level / Terrain::heightScale;
                terrain.waterCoverage_[index] = wet ? 1.0F : 0.0F;
                terrain.waterKinds_[index] = wet ? WaterKind::lake : WaterKind::none;
            }
        }
        terrain.generatedWaterSurfaceHeights_ = terrain.waterSurfaceHeights_;
        terrain.generatedWaterCoverage_ = terrain.waterCoverage_;
    } else
        terrain.waterLevel_ = 0.0F;
    terrain.baseHeights_ = terrain.heights_;
    const auto biomes =
        plan.hasWater() ? layout.enabledBiomes : dryBiomes(definitions, layout.enabledBiomes);
    auto semanticGenerator = definitions.generator(layout.generator);
    semanticGenerator.waterLevel =
        plan.hasWater() ? plan.recipe().waterLevel / Terrain::heightScale : 0.0F;
    terrain.generateSemantics(seed, semanticGenerator, definitions, biomes, progress);
    terrain.fingerprint_ = 14695981039346656037ULL;
    const auto mix = [&](std::uint32_t value) {
        terrain.fingerprint_ ^= value;
        terrain.fingerprint_ *= 1099511628211ULL;
    };
    for (std::size_t i = 0; i < terrain.heights_.size(); ++i) {
        mix(std::bit_cast<std::uint32_t>(terrain.heights_[i]));
        mix(std::bit_cast<std::uint32_t>(terrain.waterSurfaceHeights_[i]));
        mix(std::bit_cast<std::uint32_t>(terrain.waterCoverage_[i]));
    }
    for (const auto& cell : terrain.v2_->terraces.cells()) {
        mix(static_cast<std::uint32_t>(cell.east));
        mix(static_cast<std::uint32_t>(cell.south));
    }
    for (const auto& sample : terrain.semanticSamples_) {
        mix(sample.tags);
        for (float cost : sample.movementCosts)
            mix(std::bit_cast<std::uint32_t>(cost));
    }
    if (progress) {
        const auto& diagnostics = terrain.v2_->terraces.diagnostics();
        progress->diagnostic("v2 layout=" + layout.id.value +
                             " sourceError=" + std::to_string(diagnostics.meanSourceError) +
                             " isolated=" + std::to_string(diagnostics.isolatedBefore) + "->" +
                             std::to_string(diagnostics.isolatedAfter) +
                             " ramps=" + std::to_string(diagnostics.rampEdges) +
                             " cliffs=" + std::to_string(diagnostics.cliffEdges));
    }
}

void TerrainGenerationPipeline::generateProcedural(
    Terrain& terrain, std::uint32_t seed, const TerrainGeneratorDefinition& generator,
    const TerrainGenerationDefinitions& definitions, WorldGenerationProgress* progress,
    bool waterEnabled) {
    terrain.generateHeightfield(seed, generator, progress);
    if (waterEnabled)
        TerrainWaterGenerator::generate(terrain, seed, generator, progress);
    else
        TerrainWaterGenerator::clear(terrain, progress);
    terrain.baseHeights_ = terrain.heights_;
    const auto& configuredBiomes = definitions.activeLayout().enabledBiomes;
    const std::vector<TerrainBiomeId> enabledBiomes =
        waterEnabled ? configuredBiomes : dryBiomes(definitions, configuredBiomes);
    terrain.generateSemantics(seed, generator, definitions, enabledBiomes, progress);
}

void TerrainGenerationPipeline::loadCustomHeightfield(
    Terrain& terrain, const std::filesystem::path& manifest,
    WorldGenerationProgress* progress) {
    if (progress) progress->report(WorldGenerationPhase::terrainFields, 0.0F);
    const rapidjson::Document document = loadManifest(manifest);
    if (!document.HasMember("width") || !document["width"].IsUint() ||
        !document.HasMember("height") || !document["height"].IsUint() ||
        !document.HasMember("heights") || !document["heights"].IsArray())
        throw std::runtime_error("Custom terrain map requires width, height, and heights");
    const std::uint32_t width = document["width"].GetUint();
    const std::uint32_t height = document["height"].GetUint();
    const rapidjson::Value& values = document["heights"];
    if (width < 2 || height < 2 || values.Size() != width * height)
        throw std::runtime_error("Custom terrain height grid has invalid dimensions");
    std::vector<float> source(values.Size());
    for (rapidjson::SizeType index = 0; index < values.Size(); ++index) {
        if (!values[index].IsNumber())
            throw std::runtime_error("Custom terrain heights must be numeric");
        source[index] = std::clamp(values[index].GetFloat(), 0.0F, 1.0F);
    }
    terrain.heights_.resize(
        static_cast<std::size_t>(Terrain::vertexCount * Terrain::vertexCount));
    for (int z = 0; z < Terrain::vertexCount; ++z) {
        if (progress && z % 16 == 0)
            progress->report(WorldGenerationPhase::terrainFields,
                             static_cast<float>(z) / Terrain::vertexCount);
        const float sourceZ = static_cast<float>(z) * (height - 1) / Terrain::cellCount;
        const std::uint32_t z0 = static_cast<std::uint32_t>(std::floor(sourceZ));
        const std::uint32_t z1 = std::min(z0 + 1, height - 1);
        const float tz = sourceZ - static_cast<float>(z0);
        for (int x = 0; x < Terrain::vertexCount; ++x) {
            const float sourceX = static_cast<float>(x) * (width - 1) / Terrain::cellCount;
            const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(sourceX));
            const std::uint32_t x1 = std::min(x0 + 1, width - 1);
            const float tx = sourceX - static_cast<float>(x0);
            const auto at = [&](std::uint32_t sx, std::uint32_t sz) {
                return source[static_cast<std::size_t>(sz * width + sx)];
            };
            terrain.heights_[static_cast<std::size_t>(z * Terrain::vertexCount + x)] =
                std::lerp(std::lerp(at(x0, z0), at(x1, z0), tx),
                          std::lerp(at(x0, z1), at(x1, z1), tx), tz);
        }
    }
    if (progress) progress->report(WorldGenerationPhase::terrainFields, 1.0F);
}

} // namespace strategy
