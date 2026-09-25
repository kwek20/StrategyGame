#include "terrain/TerrainGenerationPipeline.hpp"

#include "terrain/Terrain.hpp"
#include "terrain/TerrainLayoutGenerator.hpp"
#include "terrain/TerrainWaterGenerator.hpp"
#include "world/GenerationProgress.hpp"

#include <algorithm>
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
    terrain.generateSemantics(seed, generator, definitions, layout.enabledBiomes, progress);
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
    terrain.generateSemantics(seed, generator, definitions,
                              definitions.activeLayout().enabledBiomes, progress);
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
