#include "terrain/TerrainGeneration.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <stdexcept>

namespace strategy {
namespace {

rapidjson::Document loadDocument(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error("Could not open terrain generation data: " + path.string());
    rapidjson::IStreamWrapper input(stream);
    rapidjson::Document result;
    result.ParseStream(input);
    if (result.HasParseError() || !result.IsObject())
        throw std::runtime_error("Invalid terrain generation data: " + path.string());
    return result;
}

float requiredFloat(const rapidjson::Value& object,
                    const char* field,
                    const std::string& context) {
    if (!object.HasMember(field) || !object[field].IsNumber())
        throw std::runtime_error(context + " requires numeric field '" + field + "'");
    return object[field].GetFloat();
}

std::string requiredString(const rapidjson::Value& object,
                           const char* field,
                           const std::string& context) {
    if (!object.HasMember(field) || !object[field].IsString())
        throw std::runtime_error(context + " requires string field '" + field + "'");
    return object[field].GetString();
}

std::pair<float, float> requiredRange(const rapidjson::Value& object,
                                      const char* field,
                                      const std::string& context) {
    if (!object.HasMember(field) || !object[field].IsArray() || object[field].Size() != 2 ||
        !object[field][0].IsNumber() || !object[field][1].IsNumber())
        throw std::runtime_error(context + " requires two-number range '" + field + "'");
    const std::pair result{object[field][0].GetFloat(), object[field][1].GetFloat()};
    if (result.first < 0.0F || result.second > 1.0F || result.first > result.second)
        throw std::runtime_error(context + " has invalid range '" + field + "'");
    return result;
}

float fade(float value) {
    return value * value * value * (value * (value * 6.0F - 15.0F) + 10.0F);
}

float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

std::uint32_t coordinateHash(int x, int z, std::uint32_t seed) {
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

} // namespace

TerrainGenerationDefinitions TerrainGenerationDefinitions::load(
    const std::filesystem::path& directory) {
    TerrainGenerationDefinitions result;
    const rapidjson::Document generators = loadDocument(directory / "generators.json");
    if (!generators.HasMember("activeGenerator") || !generators["activeGenerator"].IsString() ||
        !generators.HasMember("generators") || !generators["generators"].IsObject())
        throw std::runtime_error("Terrain generators require activeGenerator and generators");
    result.activeGeneratorId_ = TerrainGeneratorId{generators["activeGenerator"].GetString()};

    for (auto item = generators["generators"].MemberBegin();
         item != generators["generators"].MemberEnd(); ++item) {
        const std::string id = item->name.GetString();
        const rapidjson::Value& value = item->value;
        const std::string context = "Terrain generator '" + id + "'";
        if (!value.IsObject() || !value.HasMember("version") || !value["version"].IsUint() ||
            !value.HasMember("height") || !value["height"].IsObject() ||
            !value.HasMember("fields") || !value["fields"].IsObject())
            throw std::runtime_error(context + " has an invalid structure");
        TerrainGeneratorDefinition generator;
        generator.id = TerrainGeneratorId{id};
        generator.version = value["version"].GetUint();
        const rapidjson::Value& height = value["height"];
        generator.height.baseHeight = requiredFloat(height, "baseHeight", context);
        generator.height.continentalAmplitude =
            requiredFloat(height, "continentalAmplitude", context);
        generator.height.detailAmplitude = requiredFloat(height, "detailAmplitude", context);
        generator.height.mountainAmplitude = requiredFloat(height, "mountainAmplitude", context);
        generator.height.mountainThreshold = requiredFloat(height, "mountainThreshold", context);
        generator.height.erosionStrength = requiredFloat(height, "erosionStrength", context);
        generator.height.ridgePower = requiredFloat(height, "ridgePower", context);
        generator.height.minimumHeight = requiredFloat(height, "minimumHeight", context);
        generator.height.maximumHeight = requiredFloat(height, "maximumHeight", context);
        generator.height.warpScale = requiredFloat(height, "warpScale", context);
        generator.height.warpStrength = requiredFloat(height, "warpStrength", context);
        if (!height.HasMember("smoothingPasses") || !height["smoothingPasses"].IsUint())
            throw std::runtime_error(context + " requires unsigned smoothingPasses");
        generator.height.smoothingPasses = height["smoothingPasses"].GetUint();
        if (generator.height.minimumHeight < 0.0F || generator.height.maximumHeight > 1.0F ||
            generator.height.minimumHeight >= generator.height.maximumHeight ||
            generator.height.warpScale <= 0.0F || generator.height.ridgePower <= 0.0F)
            throw std::runtime_error(context + " has invalid height bounds");

        for (auto field = value["fields"].MemberBegin(); field != value["fields"].MemberEnd();
             ++field) {
            TerrainNoiseFieldDefinition definition;
            const std::string fieldContext = context + " field '" + field->name.GetString() + "'";
            definition.scale = requiredFloat(field->value, "scale", fieldContext);
            definition.persistence = requiredFloat(field->value, "persistence", fieldContext);
            if (!field->value.HasMember("octaves") || !field->value["octaves"].IsUint())
                throw std::runtime_error(fieldContext + " requires unsigned octaves");
            definition.octaves = field->value["octaves"].GetUint();
            if (definition.scale <= 0.0F || definition.octaves == 0 ||
                definition.persistence <= 0.0F || definition.persistence > 1.0F)
                throw std::runtime_error(fieldContext + " has invalid noise parameters");
            generator.fields.emplace(field->name.GetString(), definition);
        }
        for (const char* required :
             {"continentalness", "erosion", "peaks", "moisture", "temperature", "detail"})
            if (!generator.fields.contains(required))
                throw std::runtime_error(context + " is missing field '" + required + "'");
        result.generators_.emplace(id, std::move(generator));
    }
    if (!result.generators_.contains(result.activeGeneratorId_.value))
        throw std::runtime_error("Active terrain generator does not exist: " +
                                 result.activeGeneratorId_.value);

    const rapidjson::Document biomes = loadDocument(directory / "biomes.json");
    if (!biomes.HasMember("fallbackBiome") || !biomes["fallbackBiome"].IsString() ||
        !biomes.HasMember("biomes") || !biomes["biomes"].IsArray())
        throw std::runtime_error("Terrain biomes require fallbackBiome and a biomes array");
    result.fallbackBiome_ = TerrainBiomeId{biomes["fallbackBiome"].GetString()};
    for (const rapidjson::Value& value : biomes["biomes"].GetArray()) {
        TerrainBiomeDefinition biome;
        const std::string id = requiredString(value, "id", "Terrain biome");
        biome.id = TerrainBiomeId{id};
        if (!value.HasMember("priority") || !value["priority"].IsInt() ||
            !value.HasMember("enabled") || !value["enabled"].IsBool())
            throw std::runtime_error("Terrain biome requires priority and enabled: " + id);
        biome.priority = value["priority"].GetInt();
        biome.enabled = value["enabled"].GetBool();
        const auto [minimumHeight, maximumHeight] = requiredRange(value, "height", id);
        const auto [minimumMoisture, maximumMoisture] = requiredRange(value, "moisture", id);
        const auto [minimumErosion, maximumErosion] = requiredRange(value, "erosion", id);
        const auto [minimumPeaks, maximumPeaks] = requiredRange(value, "peaks", id);
        const auto [minimumTemperature, maximumTemperature] =
            requiredRange(value, "temperature", id);
        biome.minimumHeight = minimumHeight;
        biome.maximumHeight = maximumHeight;
        biome.minimumMoisture = minimumMoisture;
        biome.maximumMoisture = maximumMoisture;
        biome.minimumErosion = minimumErosion;
        biome.maximumErosion = maximumErosion;
        biome.minimumPeaks = minimumPeaks;
        biome.maximumPeaks = maximumPeaks;
        biome.minimumTemperature = minimumTemperature;
        biome.maximumTemperature = maximumTemperature;
        biome.maximumSlopeDegrees = requiredFloat(value, "maximumSlopeDegrees", id);
        if (!value.HasMember("weights") || !value["weights"].IsObject())
            throw std::runtime_error("Terrain biome requires suitability weights: " + id);
        const rapidjson::Value& weights = value["weights"];
        biome.heightWeight = requiredFloat(weights, "height", id);
        biome.moistureWeight = requiredFloat(weights, "moisture", id);
        biome.erosionWeight = requiredFloat(weights, "erosion", id);
        biome.peaksWeight = requiredFloat(weights, "peaks", id);
        biome.temperatureWeight = requiredFloat(weights, "temperature", id);
        biome.surface = TerrainSurfaceId{requiredString(value, "surface", id)};
        biome.traversal = requiredString(value, "traversal", id);
        biome.buildability = requiredString(value, "buildability", id);
        result.biomes_.push_back(std::move(biome));
    }
    if (std::none_of(result.biomes_.begin(), result.biomes_.end(), [&](const auto& biome) {
            return biome.id == result.fallbackBiome_ && biome.enabled;
        }))
        throw std::runtime_error("Fallback terrain biome is missing or disabled: " +
                                 result.fallbackBiome_.value);

    const rapidjson::Document surfaces = loadDocument(directory / "surfaces.json");
    if (!surfaces.HasMember("surfaces") || !surfaces["surfaces"].IsArray())
        throw std::runtime_error("Terrain surfaces require a surfaces array");
    for (const rapidjson::Value& value : surfaces["surfaces"].GetArray()) {
        TerrainSurfaceDefinition surface;
        const std::string id = requiredString(value, "id", "Terrain surface");
        surface.id = TerrainSurfaceId{id};
        if (!value.HasMember("color") || !value["color"].IsArray() ||
            value["color"].Size() != 3)
            throw std::runtime_error("Terrain surface requires RGB color: " + id);
        for (const rapidjson::Value& channel : value["color"].GetArray())
            surface.color.push_back(channel.GetFloat());
        if (!value.HasMember("materialWeights") || !value["materialWeights"].IsArray() ||
            value["materialWeights"].Size() != 4)
            throw std::runtime_error("Terrain surface requires four material weights: " + id);
        float weightTotal = 0.0F;
        for (const rapidjson::Value& weight : value["materialWeights"].GetArray()) {
            if (!weight.IsNumber() || weight.GetFloat() < 0.0F)
                throw std::runtime_error("Terrain surface has invalid material weights: " + id);
            surface.materialWeights.push_back(weight.GetFloat());
            weightTotal += weight.GetFloat();
        }
        if (weightTotal <= 0.0F)
            throw std::runtime_error("Terrain surface material weights cannot be empty: " + id);
        if (value.HasMember("tags") && value["tags"].IsArray())
            for (const rapidjson::Value& tag : value["tags"].GetArray())
                surface.tags.emplace_back(tag.GetString());
        result.surfaces_.push_back(std::move(surface));
    }
    for (const TerrainBiomeDefinition& biome : result.biomes_)
        if (std::none_of(result.surfaces_.begin(), result.surfaces_.end(),
                         [&](const TerrainSurfaceDefinition& surface) {
                             return surface.id == biome.surface;
                         }))
            throw std::runtime_error("Terrain biome references missing surface: " +
                                     biome.surface.value);
    return result;
}

const TerrainGeneratorDefinition& TerrainGenerationDefinitions::activeGenerator() const {
    return generators_.at(activeGeneratorId_.value);
}

TerrainFieldGenerator::TerrainFieldGenerator(std::uint32_t worldSeed,
                                             const TerrainGeneratorDefinition& definition)
    : definition_(&definition)
    , continentalness_{&definition.fields.at("continentalness"),
                       namedSeed(worldSeed, "terrain.continentalness")}
    , erosion_{&definition.fields.at("erosion"), namedSeed(worldSeed, "terrain.erosion")}
    , peaks_{&definition.fields.at("peaks"), namedSeed(worldSeed, "terrain.peaks")}
    , moisture_{&definition.fields.at("moisture"), namedSeed(worldSeed, "terrain.moisture")}
    , temperature_{&definition.fields.at("temperature"),
                   namedSeed(worldSeed, "terrain.temperature")}
    , detail_{&definition.fields.at("detail"), namedSeed(worldSeed, "terrain.detail")}
    , warpXSeed_(namedSeed(worldSeed, "terrain.domain_warp.x"))
    , warpZSeed_(namedSeed(worldSeed, "terrain.domain_warp.z")) {}

std::uint32_t TerrainFieldGenerator::namedSeed(std::uint32_t worldSeed,
                                               const std::string& stream) {
    std::uint32_t hash = 2166136261U ^ worldSeed;
    for (const unsigned char value : stream) {
        hash ^= value;
        hash *= 16777619U;
    }
    hash ^= hash >> 16U;
    hash *= 0x7FEB352DU;
    hash ^= hash >> 15U;
    return hash;
}

float TerrainFieldGenerator::valueNoise(float x, float z, std::uint32_t seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const float tx = fade(x - static_cast<float>(x0));
    const float tz = fade(z - static_cast<float>(z0));
    const auto randomSigned = [seed](int sampleX, int sampleZ) {
        return static_cast<float>(coordinateHash(sampleX, sampleZ, seed) & 0x00FFFFFFU) /
                   static_cast<float>(0x007FFFFFU) -
               1.0F;
    };
    const float top = std::lerp(randomSigned(x0, z0), randomSigned(x0 + 1, z0), tx);
    const float bottom =
        std::lerp(randomSigned(x0, z0 + 1), randomSigned(x0 + 1, z0 + 1), tx);
    return std::lerp(top, bottom, tz);
}

float TerrainFieldGenerator::fractalNoise(float x,
                                          float z,
                                          std::uint32_t seed,
                                          std::uint32_t octaves,
                                          float persistence) {
    float total = 0.0F;
    float amplitude = 1.0F;
    float frequency = 1.0F;
    float amplitudeSum = 0.0F;
    for (std::uint32_t octave = 0; octave < octaves; ++octave) {
        total += valueNoise(x * frequency, z * frequency, seed + octave * 1013U) * amplitude;
        amplitudeSum += amplitude;
        amplitude *= persistence;
        frequency *= 2.0F;
    }
    return total / amplitudeSum;
}

float TerrainFieldGenerator::sampleField(const RuntimeField& field,
                                         float worldX,
                                         float worldZ) const {
    const float value = fractalNoise(worldX / field.definition->scale,
                                     worldZ / field.definition->scale,
                                     field.seed,
                                     field.definition->octaves,
                                     field.definition->persistence);
    return std::clamp(value * 0.5F + 0.5F, 0.0F, 1.0F);
}

std::pair<float, float> TerrainFieldGenerator::warpedPosition(float worldX, float worldZ) const {
    const TerrainHeightDefinition& height = definition_->height;
    const float warpX = fractalNoise(worldX / height.warpScale,
                                     worldZ / height.warpScale,
                                     warpXSeed_,
                                     3,
                                     0.5F) *
                        height.warpStrength;
    const float warpZ = fractalNoise(worldX / height.warpScale,
                                     worldZ / height.warpScale,
                                     warpZSeed_,
                                     3,
                                     0.5F) *
                        height.warpStrength;
    return {worldX + warpX, worldZ + warpZ};
}

TerrainRegionalFields TerrainFieldGenerator::sample(float worldX, float worldZ) const {
    const auto [x, z] = warpedPosition(worldX, worldZ);
    TerrainRegionalFields result;
    result.continentalness = sampleField(continentalness_, x, z);
    result.erosion = sampleField(erosion_, x, z);
    const float peakNoise = sampleField(peaks_, x, z) * 2.0F - 1.0F;
    result.peaks =
        std::pow(1.0F - std::abs(peakNoise), definition_->height.ridgePower);
    result.moisture = sampleField(moisture_, x, z);
    result.temperature = sampleField(temperature_, x, z);
    result.detail = sampleField(detail_, x, z);
    return result;
}

float TerrainFieldGenerator::heightAt(float worldX, float worldZ) const {
    const auto [x, z] = warpedPosition(worldX, worldZ);
    TerrainRegionalFields fields;
    fields.continentalness = sampleField(continentalness_, x, z);
    fields.erosion = sampleField(erosion_, x, z);
    const float peakNoise = sampleField(peaks_, x, z) * 2.0F - 1.0F;
    fields.peaks =
        std::pow(1.0F - std::abs(peakNoise), definition_->height.ridgePower);
    fields.detail = sampleField(detail_, x, z);
    return height(fields);
}

float TerrainFieldGenerator::height(const TerrainRegionalFields& fields) const {
    const TerrainHeightDefinition& definition = definition_->height;
    const float continent = (fields.continentalness - 0.5F) * 2.0F;
    const float base = definition.baseHeight + continent * definition.continentalAmplitude;
    const float detail = (fields.detail - 0.5F) * 2.0F * definition.detailAmplitude;
    const float mountainSignal = fields.continentalness * 0.58F + fields.peaks * 0.42F;
    const float mountainWeight = smoothstep(definition.mountainThreshold, 1.0F, mountainSignal);
    const float erosion = 1.0F - fields.erosion * definition.erosionStrength;
    const float mountains =
        fields.peaks * mountainWeight * erosion * definition.mountainAmplitude;
    return std::clamp(base + detail + mountains,
                      definition.minimumHeight,
                      definition.maximumHeight);
}

} // namespace strategy
