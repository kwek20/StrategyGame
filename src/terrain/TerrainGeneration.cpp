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
        generator.waterLevel = requiredFloat(value, "waterLevel", context);
        if (generator.waterLevel < 0.0F || generator.waterLevel > 1.0F)
            throw std::runtime_error(context + " has an invalid waterLevel");
        const rapidjson::Value& height = value["height"];
        generator.height.baseHeight = requiredFloat(height, "baseHeight", context);
        generator.height.continentalAmplitude =
            requiredFloat(height, "continentalAmplitude", context);
        generator.height.detailAmplitude = requiredFloat(height, "detailAmplitude", context);
        generator.height.mountainAmplitude = requiredFloat(height, "mountainAmplitude", context);
        generator.height.mountainThreshold = requiredFloat(height, "mountainThreshold", context);
        generator.height.erosionStrength = requiredFloat(height, "erosionStrength", context);
        generator.height.ridgePower = requiredFloat(height, "ridgePower", context);
        generator.height.warpScale = requiredFloat(height, "warpScale", context);
        generator.height.warpStrength = requiredFloat(height, "warpStrength", context);
        if (generator.height.warpScale <= 0.0F || generator.height.ridgePower <= 0.0F)
            throw std::runtime_error(context + " has invalid height settings");

        if (!value.HasMember("landforms") || !value["landforms"].IsObject())
            throw std::runtime_error(context + " requires landform settings");
        const rapidjson::Value& landforms = value["landforms"];
        generator.landforms.plainCompression =
            requiredFloat(landforms, "plainCompression", context);
        generator.landforms.hillAmplitude =
            requiredFloat(landforms, "hillAmplitude", context);
        generator.landforms.mountainBeltStrength =
            requiredFloat(landforms, "mountainBeltStrength", context);
        generator.landforms.outcropAmplitude =
            requiredFloat(landforms, "outcropAmplitude", context);
        generator.landforms.basinDepth = requiredFloat(landforms, "basinDepth", context);
        generator.landforms.coastalShelfStrength =
            requiredFloat(landforms, "coastalShelfStrength", context);
        generator.landforms.offshoreIslandAmplitude =
            requiredFloat(landforms, "offshoreIslandAmplitude", context);
        generator.landforms.coastalBayDepth =
            requiredFloat(landforms, "coastalBayDepth", context);
        generator.landforms.chainAngleDegrees =
            requiredFloat(landforms, "chainAngleDegrees", context);
        generator.landforms.chainAnisotropy =
            requiredFloat(landforms, "chainAnisotropy", context);
        generator.landforms.chainSharpness =
            requiredFloat(landforms, "chainSharpness", context);
        generator.landforms.chainThresholdLow =
            requiredFloat(landforms, "chainThresholdLow", context);
        generator.landforms.chainThresholdHigh =
            requiredFloat(landforms, "chainThresholdHigh", context);
        generator.landforms.secondaryChainWeight =
            requiredFloat(landforms, "secondaryChainWeight", context);
        generator.landforms.foothillAmplitude =
            requiredFloat(landforms, "foothillAmplitude", context);
        generator.landforms.plainElevationCompression =
            requiredFloat(landforms, "plainElevationCompression", context);
        generator.landforms.hillCurvePower =
            requiredFloat(landforms, "hillCurvePower", context);
        generator.landforms.mountainCurvePower =
            requiredFloat(landforms, "mountainCurvePower", context);
        generator.landforms.cliffCurveThreshold =
            requiredFloat(landforms, "cliffCurveThreshold", context);
        generator.landforms.cliffCurveStrength =
            requiredFloat(landforms, "cliffCurveStrength", context);
        if (generator.landforms.plainCompression < 0.0F ||
            generator.landforms.plainCompression > 1.0F ||
            generator.landforms.hillAmplitude < 0.0F ||
            generator.landforms.mountainBeltStrength < 0.0F ||
            generator.landforms.outcropAmplitude < 0.0F ||
            generator.landforms.basinDepth < 0.0F ||
            generator.landforms.coastalShelfStrength < 0.0F ||
            generator.landforms.coastalShelfStrength > 1.0F ||
            generator.landforms.offshoreIslandAmplitude < 0.0F ||
            generator.landforms.coastalBayDepth < 0.0F ||
            generator.landforms.chainAngleDegrees < -180.0F ||
            generator.landforms.chainAngleDegrees > 180.0F ||
            generator.landforms.chainAnisotropy < 1.0F ||
            generator.landforms.chainSharpness <= 0.0F ||
            generator.landforms.chainThresholdLow < 0.0F ||
            generator.landforms.chainThresholdHigh > 1.0F ||
            generator.landforms.chainThresholdLow >= generator.landforms.chainThresholdHigh ||
            generator.landforms.secondaryChainWeight < 0.0F ||
            generator.landforms.secondaryChainWeight > 1.0F ||
            generator.landforms.foothillAmplitude < 0.0F ||
            generator.landforms.plainElevationCompression < 0.0F ||
            generator.landforms.plainElevationCompression > 1.0F ||
            generator.landforms.hillCurvePower <= 0.0F ||
            generator.landforms.mountainCurvePower <= 0.0F ||
            generator.landforms.cliffCurveThreshold < 0.0F ||
            generator.landforms.cliffCurveThreshold >= 1.0F ||
            generator.landforms.cliffCurveStrength < 0.0F)
            throw std::runtime_error(context + " has invalid landform settings");

        if (!value.HasMember("postProcessing") || !value["postProcessing"].IsObject())
            throw std::runtime_error(context + " requires postProcessing settings");
        const rapidjson::Value& post = value["postProcessing"];
        const auto requiredUint = [&](const char* field) {
            if (!post.HasMember(field) || !post[field].IsUint())
                throw std::runtime_error(context + " requires unsigned postProcessing '" +
                                         field + "'");
            return post[field].GetUint();
        };
        generator.postProcessing.regionalSampleStride =
            requiredUint("regionalSampleStride");
        generator.postProcessing.plainBlurRadius = requiredUint("plainBlurRadius");
        generator.postProcessing.plainFlattenStrength =
            requiredFloat(post, "plainFlattenStrength", context);
        generator.postProcessing.hillBlurRadius = requiredUint("hillBlurRadius");
        generator.postProcessing.hillSmoothingStrength =
            requiredFloat(post, "hillSmoothingStrength", context);
        generator.postProcessing.mountainErosionPasses =
            requiredUint("mountainErosionPasses");
        generator.postProcessing.mountainErosionStrength =
            requiredFloat(post, "mountainErosionStrength", context);
        generator.postProcessing.coastalBlurRadius = requiredUint("coastalBlurRadius");
        generator.postProcessing.coastalSmoothingStrength =
            requiredFloat(post, "coastalSmoothingStrength", context);
        if (generator.postProcessing.regionalSampleStride == 0 ||
            generator.postProcessing.plainBlurRadius == 0 ||
            generator.postProcessing.hillBlurRadius == 0 ||
            generator.postProcessing.coastalBlurRadius == 0 ||
            generator.postProcessing.plainFlattenStrength < 0.0F ||
            generator.postProcessing.plainFlattenStrength > 1.0F ||
            generator.postProcessing.hillSmoothingStrength < 0.0F ||
            generator.postProcessing.hillSmoothingStrength > 1.0F ||
            generator.postProcessing.mountainErosionStrength < 0.0F ||
            generator.postProcessing.mountainErosionStrength > 1.0F ||
            generator.postProcessing.coastalSmoothingStrength < 0.0F ||
            generator.postProcessing.coastalSmoothingStrength > 1.0F)
            throw std::runtime_error(context + " has invalid postProcessing settings");

        if (!value.HasMember("hydrology") || !value["hydrology"].IsObject())
            throw std::runtime_error(context + " requires hydrology settings");
        const rapidjson::Value& hydrology = value["hydrology"];
        generator.hydrology.gridCellSize = requiredFloat(hydrology, "gridCellSize", context);
        generator.hydrology.riverCatchmentArea =
            requiredFloat(hydrology, "riverCatchmentArea", context);
        generator.hydrology.wetlandCatchmentArea =
            requiredFloat(hydrology, "wetlandCatchmentArea", context);
        generator.hydrology.riverCarveDepth =
            requiredFloat(hydrology, "riverCarveDepth", context);
        generator.hydrology.riverHalfWidth =
            requiredFloat(hydrology, "riverHalfWidth", context);
        generator.hydrology.riverMeanderStrength =
            requiredFloat(hydrology, "riverMeanderStrength", context);
        generator.hydrology.riverPathSampleSpacing =
            requiredFloat(hydrology, "riverPathSampleSpacing", context);
        generator.hydrology.minimumFirstOrderLength =
            requiredFloat(hydrology, "minimumFirstOrderLength", context);
        if (!hydrology.HasMember("riverSmoothingIterations") ||
            !hydrology["riverSmoothingIterations"].IsUint())
            throw std::runtime_error(context + " requires unsigned riverSmoothingIterations");
        generator.hydrology.riverSmoothingIterations =
            hydrology["riverSmoothingIterations"].GetUint();
        generator.hydrology.meanderMaximumSlopeDegrees =
            requiredFloat(hydrology, "meanderMaximumSlopeDegrees", context);
        generator.hydrology.riverBankFalloff =
            requiredFloat(hydrology, "riverBankFalloff", context);
        generator.hydrology.riverFloodplainWidthMultiplier =
            requiredFloat(hydrology, "riverFloodplainWidthMultiplier", context);
        generator.hydrology.riverFloodplainFlattenStrength =
            requiredFloat(hydrology, "riverFloodplainFlattenStrength", context);
        generator.hydrology.riverBankHeight =
            requiredFloat(hydrology, "riverBankHeight", context);
        generator.hydrology.riverMaximumIncision =
            requiredFloat(hydrology, "riverMaximumIncision", context);
        generator.hydrology.confluenceWidthMultiplier =
            requiredFloat(hydrology, "confluenceWidthMultiplier", context);
        generator.hydrology.estuaryLength =
            requiredFloat(hydrology, "estuaryLength", context);
        generator.hydrology.estuaryWidthMultiplier =
            requiredFloat(hydrology, "estuaryWidthMultiplier", context);
        if (!hydrology.HasMember("waterEdgeSmoothingRadius") ||
            !hydrology["waterEdgeSmoothingRadius"].IsUint())
            throw std::runtime_error(context + " requires unsigned waterEdgeSmoothingRadius");
        generator.hydrology.waterEdgeSmoothingRadius =
            hydrology["waterEdgeSmoothingRadius"].GetUint();
        generator.hydrology.minimumRenderedWaterDepth =
            requiredFloat(hydrology, "minimumRenderedWaterDepth", context);
        generator.hydrology.lakeMinimumDepth =
            requiredFloat(hydrology, "lakeMinimumDepth", context);
        generator.hydrology.lakeMaximumDepth =
            requiredFloat(hydrology, "lakeMaximumDepth", context);
        generator.hydrology.wetlandMaximumSlopeDegrees =
            requiredFloat(hydrology, "wetlandMaximumSlopeDegrees", context);
        if (generator.hydrology.gridCellSize <= 0.0F ||
            generator.hydrology.riverCatchmentArea <= 0.0F ||
            generator.hydrology.wetlandCatchmentArea <= 0.0F ||
            generator.hydrology.riverCarveDepth <= 0.0F ||
            generator.hydrology.riverHalfWidth <= 0.0F ||
            generator.hydrology.riverMeanderStrength < 0.0F ||
            generator.hydrology.riverPathSampleSpacing <= 0.0F ||
            generator.hydrology.minimumFirstOrderLength < 0.0F ||
            generator.hydrology.riverSmoothingIterations > 4 ||
            generator.hydrology.meanderMaximumSlopeDegrees <= 0.0F ||
            generator.hydrology.riverBankFalloff <= 0.0F ||
            generator.hydrology.riverFloodplainWidthMultiplier < 0.0F ||
            generator.hydrology.riverFloodplainFlattenStrength < 0.0F ||
            generator.hydrology.riverFloodplainFlattenStrength > 1.0F ||
            generator.hydrology.riverBankHeight < 0.0F ||
            generator.hydrology.riverMaximumIncision <= 0.0F ||
            generator.hydrology.confluenceWidthMultiplier < 1.0F ||
            generator.hydrology.estuaryLength < 0.0F ||
            generator.hydrology.estuaryWidthMultiplier < 1.0F ||
            generator.hydrology.waterEdgeSmoothingRadius == 0 ||
            generator.hydrology.minimumRenderedWaterDepth <= 0.0F ||
            generator.hydrology.lakeMinimumDepth <= 0.0F ||
            generator.hydrology.lakeMaximumDepth < generator.hydrology.lakeMinimumDepth ||
            generator.hydrology.wetlandMaximumSlopeDegrees <= 0.0F)
            throw std::runtime_error(context + " has invalid hydrology settings");

        if (!value.HasMember("barriers") || !value["barriers"].IsObject() ||
            !value.HasMember("connectivity") || !value["connectivity"].IsObject())
            throw std::runtime_error(context + " requires barriers and connectivity settings");
        const rapidjson::Value& barriers = value["barriers"];
        generator.barriers.minimumHeight = requiredFloat(barriers, "minimumHeight", context);
        generator.barriers.minimumPeak = requiredFloat(barriers, "minimumPeak", context);
        generator.barriers.cliffSlopeDegrees =
            requiredFloat(barriers, "cliffSlopeDegrees", context);
        generator.barriers.passMinimumErosion =
            requiredFloat(barriers, "passMinimumErosion", context);
        generator.barriers.passMaximumSlopeDegrees =
            requiredFloat(barriers, "passMaximumSlopeDegrees", context);
        generator.barriers.passMovementCost =
            requiredFloat(barriers, "passMovementCost", context);
        const rapidjson::Value& connectivity = value["connectivity"];
        generator.connectivity.minimumStartingLandFraction =
            requiredFloat(connectivity, "minimumStartingLandFraction", context);
        if (!connectivity.HasMember("maximumGenerationAttempts") ||
            !connectivity["maximumGenerationAttempts"].IsUint())
            throw std::runtime_error(context + " requires unsigned maximumGenerationAttempts");
        generator.connectivity.maximumGenerationAttempts =
            connectivity["maximumGenerationAttempts"].GetUint();
        if (generator.barriers.minimumHeight < 0.0F ||
            generator.barriers.minimumHeight > 1.0F ||
            generator.barriers.minimumPeak < 0.0F || generator.barriers.minimumPeak > 1.0F ||
            generator.barriers.cliffSlopeDegrees <= 0.0F ||
            generator.barriers.cliffSlopeDegrees > 90.0F ||
            generator.barriers.passMinimumErosion < 0.0F ||
            generator.barriers.passMinimumErosion > 1.0F ||
            generator.barriers.passMaximumSlopeDegrees <= 0.0F ||
            generator.barriers.passMaximumSlopeDegrees >=
                generator.barriers.cliffSlopeDegrees ||
            generator.barriers.passMovementCost < 1.0F ||
            generator.connectivity.minimumStartingLandFraction <= 0.0F ||
            generator.connectivity.minimumStartingLandFraction > 1.0F ||
            generator.connectivity.maximumGenerationAttempts == 0)
            throw std::runtime_error(context + " has invalid barrier/connectivity settings");

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
        for (const char* required : {"continentalness", "erosion", "peaks", "moisture",
                                     "temperature", "detail", "plains", "hills",
                                     "mountain_belt", "rocky_outcrops", "coastal_shelf",
                                     "basin"})
            if (!generator.fields.contains(required))
                throw std::runtime_error(context + " is missing field '" + required + "'");
        result.generators_.emplace(id, std::move(generator));
    }
    if (!result.generators_.contains(result.activeGeneratorId_.value))
        throw std::runtime_error("Active terrain generator does not exist: " +
                                 result.activeGeneratorId_.value);

    const rapidjson::Document layouts = loadDocument(directory / "layouts.json");
    if (!layouts.HasMember("activeLayout") || !layouts["activeLayout"].IsString() ||
        !layouts.HasMember("layouts") || !layouts["layouts"].IsArray())
        throw std::runtime_error("Terrain layouts require activeLayout and a layouts array");
    result.activeLayoutId_ = TerrainLayoutId{layouts["activeLayout"].GetString()};
    bool waterGenerationEnabled = true;
    if (layouts.HasMember("waterGenerationEnabled")) {
        if (!layouts["waterGenerationEnabled"].IsBool())
            throw std::runtime_error("Terrain waterGenerationEnabled must be boolean");
        waterGenerationEnabled = layouts["waterGenerationEnabled"].GetBool();
    }
    for (const rapidjson::Value& value : layouts["layouts"].GetArray()) {
        if (!value.IsObject()) throw std::runtime_error("Terrain layout must be an object");
        TerrainLayoutDefinition layout;
        const std::string id = requiredString(value, "id", "Terrain layout");
        const std::string context = "Terrain layout '" + id + "'";
        layout.id = TerrainLayoutId{id};
        layout.nameKey = requiredString(value, "nameKey", context);
        if (!value.HasMember("version") || !value["version"].IsUint())
            throw std::runtime_error(context + " requires unsigned version");
        layout.version = value["version"].GetUint();
        const std::string source = requiredString(value, "source", context);
        if (source == "procedural") {
            layout.source = TerrainLayoutSource::procedural;
            layout.generator = TerrainGeneratorId{requiredString(value, "generator", context)};
            if (!result.generators_.contains(layout.generator.value))
                throw std::runtime_error(context + " references missing generator: " +
                                         layout.generator.value);
            const std::string shape = value.HasMember("shape") && value["shape"].IsString()
                                          ? value["shape"].GetString()
                                          : "natural";
            if (shape == "natural") layout.shape = TerrainLayoutShape::natural;
            else if (shape == "plains") layout.shape = TerrainLayoutShape::plains;
            else if (shape == "hills") layout.shape = TerrainLayoutShape::hills;
            else if (shape == "central_hill") layout.shape = TerrainLayoutShape::centralHill;
            else if (shape == "central_water") layout.shape = TerrainLayoutShape::centralWater;
            else if (shape == "islands") layout.shape = TerrainLayoutShape::islands;
            else throw std::runtime_error(context + " has unknown shape: " + shape);
        } else if (source == "custom_map") {
            layout.source = TerrainLayoutSource::customMap;
            layout.customMap = requiredString(value, "map", context);
            if (value.HasMember("generator") && value["generator"].IsString())
                layout.generator = TerrainGeneratorId{value["generator"].GetString()};
            else
                layout.generator = result.activeGeneratorId_;
            if (!result.generators_.contains(layout.generator.value))
                throw std::runtime_error(context + " references missing fallback generator: " +
                                         layout.generator.value);
        } else {
            throw std::runtime_error(context + " has unknown source: " + source);
        }
        if (value.HasMember("hydrologyEnabled")) {
            if (!value["hydrologyEnabled"].IsBool())
                throw std::runtime_error(context + " hydrologyEnabled must be boolean");
            layout.hydrologyEnabled = value["hydrologyEnabled"].GetBool();
        }
        layout.hydrologyEnabled = layout.hydrologyEnabled && waterGenerationEnabled;
        if (value.HasMember("parameters")) {
            if (!value["parameters"].IsObject())
                throw std::runtime_error(context + " parameters must be an object");
            const auto& parameters = value["parameters"];
            if (parameters.HasMember("featureStrength"))
                layout.featureStrength =
                    requiredFloat(parameters, "featureStrength", context);
            if (parameters.HasMember("featureRadius"))
                layout.featureRadius = requiredFloat(parameters, "featureRadius", context);
            if (parameters.HasMember("islandCount")) {
                if (!parameters["islandCount"].IsUint())
                    throw std::runtime_error(context + " islandCount must be unsigned");
                layout.islandCount = parameters["islandCount"].GetUint();
            }
            if (layout.featureStrength < 0.0F || layout.featureRadius <= 0.0F ||
                layout.featureRadius > 1.0F || layout.islandCount == 0)
                throw std::runtime_error(context + " has invalid layout parameters");
        }
        if (value.HasMember("enabledBiomes")) {
            if (!value["enabledBiomes"].IsArray())
                throw std::runtime_error(context + " enabledBiomes must be an array");
            for (const rapidjson::Value& biome : value["enabledBiomes"].GetArray()) {
                if (!biome.IsString())
                    throw std::runtime_error(context + " enabledBiomes entries must be strings");
                layout.enabledBiomes.emplace_back(biome.GetString());
            }
        }
        result.layoutList_.push_back(layout);
        if (!result.layouts_.emplace(id, std::move(layout)).second)
            throw std::runtime_error("Duplicate terrain layout: " + id);
    }
    if (!result.layouts_.contains(result.activeLayoutId_.value))
        throw std::runtime_error("Active terrain layout does not exist: " +
                                 result.activeLayoutId_.value);

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
        if (value.HasMember("generator")) {
            if (!value["generator"].IsString())
                throw std::runtime_error("Terrain biome generator must be a string: " + id);
            biome.generator = value["generator"].GetString();
        }
        if (biome.generator != "range" && biome.generator != "disabled")
            throw std::runtime_error("Unknown terrain biome generator '" + biome.generator +
                                     "' for " + id);
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
        biome.movementCosts = biome.traversal == "impassable"
                                  ? std::array<float, 3>{0.0F, 0.0F, 0.0F}
                                  : std::array<float, 3>{biome.traversal == "difficult" ? 2.25F
                                                                                       : 1.0F,
                                                         0.0F, 1.0F};
        if (value.HasMember("movementCosts")) {
            if (!value["movementCosts"].IsObject())
                throw std::runtime_error("Terrain biome movementCosts must be an object: " + id);
            biome.movementCosts.fill(0.0F);
            const auto& costs = value["movementCosts"];
            for (const MovementDomain domain : movementDomains) {
                const std::string name{movementDomainName(domain)};
                if (!costs.HasMember(name.c_str())) continue;
                if (!costs[name.c_str()].IsNumber() || costs[name.c_str()].GetFloat() <= 0.0F)
                    throw std::runtime_error("Terrain biome has invalid movement cost for " +
                                             name + ": " + id);
                biome.movementCosts[static_cast<std::size_t>(domain)] =
                    costs[name.c_str()].GetFloat();
            }
        }
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
    for (const auto& [id, layout] : result.layouts_) {
        if (layout.enabledBiomes.empty()) continue;
        const auto contains = [&](const TerrainBiomeId& biomeId) {
            return std::find(layout.enabledBiomes.begin(), layout.enabledBiomes.end(), biomeId) !=
                   layout.enabledBiomes.end();
        };
        for (const TerrainBiomeId& biomeId : layout.enabledBiomes) {
            if (std::none_of(result.biomes_.begin(), result.biomes_.end(),
                             [&](const auto& biome) {
                                 return biome.id == biomeId && biome.enabled &&
                                        biome.generator != "disabled";
                             }))
                throw std::runtime_error("Terrain layout '" + id +
                                         "' enables missing or disabled biome: " +
                                         biomeId.value);
        }
        if (!contains(result.fallbackBiome_))
            throw std::runtime_error("Terrain layout '" + id +
                                     "' must enable the fallback biome: " +
                                     result.fallbackBiome_.value);
        if (layout.hydrologyEnabled &&
            (!contains(TerrainBiomeId{"deep_water"}) ||
             !contains(TerrainBiomeId{"shallow_water"})))
            throw std::runtime_error("Hydrology layout '" + id +
                                     "' must enable deep_water and shallow_water");
    }
    return result;
}

const TerrainGeneratorDefinition& TerrainGenerationDefinitions::activeGenerator() const {
    return generator(activeLayout().generator);
}

const TerrainLayoutDefinition& TerrainGenerationDefinitions::activeLayout() const {
    return layout(activeLayoutId_);
}

const TerrainLayoutDefinition& TerrainGenerationDefinitions::layout(TerrainLayoutId id) const {
    const auto found = layouts_.find(id.value);
    if (found == layouts_.end()) throw std::runtime_error("Unknown terrain layout: " + id.value);
    return found->second;
}

const TerrainGeneratorDefinition& TerrainGenerationDefinitions::generator(
    TerrainGeneratorId id) const {
    const auto found = generators_.find(id.value);
    if (found == generators_.end())
        throw std::runtime_error("Unknown terrain generator: " + id.value);
    return found->second;
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
    , plains_{&definition.fields.at("plains"), namedSeed(worldSeed, "terrain.landform.plains")}
    , hills_{&definition.fields.at("hills"), namedSeed(worldSeed, "terrain.landform.hills")}
    , mountainBelt_{&definition.fields.at("mountain_belt"),
                    namedSeed(worldSeed, "terrain.landform.mountain_belt")}
    , rockyOutcrops_{&definition.fields.at("rocky_outcrops"),
                     namedSeed(worldSeed, "terrain.landform.rocky_outcrops")}
    , coastalShelf_{&definition.fields.at("coastal_shelf"),
                    namedSeed(worldSeed, "terrain.landform.coastal_shelf")}
    , basin_{&definition.fields.at("basin"), namedSeed(worldSeed, "terrain.landform.basin")}
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
    result.plains = smoothstep(0.40F, 0.72F, sampleField(plains_, x, z));
    result.hills = smoothstep(0.38F, 0.74F, sampleField(hills_, x, z));
    const TerrainLandformDefinition& landforms = definition_->landforms;
    const float angle = landforms.chainAngleDegrees * 0.017453292519943295F;
    const auto chainRidge = [&](float direction, std::uint32_t seedOffset) {
        const float cosine = std::cos(direction);
        const float sine = std::sin(direction);
        const float along = x * cosine + z * sine;
        const float across = -x * sine + z * cosine;
        const float noise = fractalNoise(
            along / (mountainBelt_.definition->scale * landforms.chainAnisotropy),
            across / mountainBelt_.definition->scale,
            mountainBelt_.seed + seedOffset,
            mountainBelt_.definition->octaves,
            mountainBelt_.definition->persistence);
        const float ridge = std::pow(1.0F - std::abs(noise), landforms.chainSharpness);
        return smoothstep(landforms.chainThresholdLow, landforms.chainThresholdHigh, ridge);
    };
    const float primaryChain = chainRidge(angle, 0U);
    const float secondaryChain =
        chainRidge(-angle * 0.73F, 0x6D2B79F5U) * landforms.secondaryChainWeight;
    result.mountainBelt = std::clamp(std::max(primaryChain, secondaryChain), 0.0F, 1.0F);
    result.rockyOutcrops = smoothstep(0.58F, 0.78F, sampleField(rockyOutcrops_, x, z));
    result.coastalShelf = smoothstep(0.42F, 0.75F, sampleField(coastalShelf_, x, z));
    result.basin = smoothstep(0.55F, 0.78F, sampleField(basin_, x, z));
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
    fields.plains = smoothstep(0.40F, 0.72F, sampleField(plains_, x, z));
    fields.hills = smoothstep(0.38F, 0.74F, sampleField(hills_, x, z));
    const TerrainLandformDefinition& landforms = definition_->landforms;
    const float angle = landforms.chainAngleDegrees * 0.017453292519943295F;
    const auto chainRidge = [&](float direction, std::uint32_t seedOffset) {
        const float cosine = std::cos(direction);
        const float sine = std::sin(direction);
        const float along = x * cosine + z * sine;
        const float across = -x * sine + z * cosine;
        const float noise = fractalNoise(
            along / (mountainBelt_.definition->scale * landforms.chainAnisotropy),
            across / mountainBelt_.definition->scale,
            mountainBelt_.seed + seedOffset,
            mountainBelt_.definition->octaves,
            mountainBelt_.definition->persistence);
        const float ridge = std::pow(1.0F - std::abs(noise), landforms.chainSharpness);
        return smoothstep(landforms.chainThresholdLow, landforms.chainThresholdHigh, ridge);
    };
    fields.mountainBelt = std::clamp(
        std::max(chainRidge(angle, 0U),
                 chainRidge(-angle * 0.73F, 0x6D2B79F5U) *
                     landforms.secondaryChainWeight),
        0.0F, 1.0F);
    fields.rockyOutcrops = smoothstep(0.58F, 0.78F, sampleField(rockyOutcrops_, x, z));
    fields.coastalShelf = smoothstep(0.42F, 0.75F, sampleField(coastalShelf_, x, z));
    fields.basin = smoothstep(0.55F, 0.78F, sampleField(basin_, x, z));
    return height(fields);
}

float TerrainFieldGenerator::height(const TerrainRegionalFields& fields) const {
    const TerrainHeightDefinition& definition = definition_->height;
    const TerrainLandformDefinition& landforms = definition_->landforms;
    const float continent = (fields.continentalness - 0.5F) * 2.0F;
    float base = definition.baseHeight + continent * definition.continentalAmplitude;
    const float plainMask = fields.plains * (1.0F - fields.mountainBelt * 0.85F) *
                            (1.0F - fields.rockyOutcrops * 0.65F);
    const float detailScale = std::lerp(1.0F, landforms.plainCompression, plainMask);
    const float detail =
        (fields.detail - 0.5F) * 2.0F * definition.detailAmplitude * detailScale;
    const float signedHill = (fields.hills - 0.5F) * 2.0F;
    const float curvedHill = std::copysign(
        std::pow(std::abs(signedHill), landforms.hillCurvePower), signedHill);
    const float hills = curvedHill * landforms.hillAmplitude *
                        (1.0F - plainMask * 0.75F);
    const float compressedPlain = definition.baseHeight +
        continent * definition.continentalAmplitude * landforms.plainElevationCompression;
    base = std::lerp(base, compressedPlain, plainMask);
    const float mountainSignal = fields.mountainBelt * 0.72F + fields.peaks * 0.28F;
    const float mountainWeight = smoothstep(definition.mountainThreshold, 1.0F, mountainSignal);
    const float erosion = 1.0F - fields.erosion * definition.erosionStrength;
    const float beltStrength = std::lerp(0.10F, landforms.mountainBeltStrength,
                                         fields.mountainBelt);
    const float mountainRidge = std::pow(fields.peaks, landforms.mountainCurvePower);
    const float mountains = mountainRidge * mountainWeight * erosion *
                            definition.mountainAmplitude * beltStrength;
    const float foothillMask = smoothstep(0.12F, 0.62F, fields.mountainBelt) *
                               (1.0F - mountainWeight);
    const float foothills = foothillMask * (0.35F + fields.hills * 0.65F) *
                            landforms.foothillAmplitude * erosion;
    const float outcrops = fields.peaks * fields.rockyOutcrops *
                           landforms.outcropAmplitude * (1.0F - plainMask * 0.6F);
    const float basin = fields.basin * landforms.basinDepth *
                        (1.0F - fields.mountainBelt * 0.8F);
    const float cliffSignal = smoothstep(landforms.cliffCurveThreshold, 1.0F,
                                         mountainRidge * fields.mountainBelt);
    const float cliffs = cliffSignal * cliffSignal * landforms.cliffCurveStrength;
    base += detail + hills + foothills + mountains + cliffs + outcrops - basin;
    const float coastProximity =
        1.0F - smoothstep(0.025F, 0.16F, std::abs(base - definition_->waterLevel));
    // Broad basins bite into the continental edge to form bays. Sparse rocky/peak overlap lifts
    // a few shelf locations into islands without distributing dots uniformly along the coast.
    base -= fields.basin * fields.coastalShelf * coastProximity *
            landforms.coastalBayDepth;
    const float islandMask = fields.rockyOutcrops * fields.peaks * fields.coastalShelf *
                             coastProximity;
    base += islandMask * landforms.offshoreIslandAmplitude;
    const float shelfDistance = std::abs(base - definition_->waterLevel);
    const float shelfBand = 1.0F - smoothstep(0.02F, 0.14F, shelfDistance);
    base = std::lerp(base, definition_->waterLevel,
                     fields.coastalShelf * shelfBand * landforms.coastalShelfStrength);
    // The normalized representation remains bounded, but no authored min/max clips the seed's
    // natural elevation range.
    return std::clamp(base, 0.0F, 1.0F);
}

} // namespace strategy
