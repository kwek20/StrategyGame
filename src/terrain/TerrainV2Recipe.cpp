#include "terrain/TerrainV2Recipe.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <stdexcept>

namespace strategy {
namespace {
float number(const rapidjson::Value& value, const char* key) {
    if (!value.HasMember(key) || !value[key].IsNumber() || !std::isfinite(value[key].GetFloat()))
        throw std::runtime_error(std::string{"V2 requires finite number: "} + key);
    return value[key].GetFloat();
}

std::string string(const rapidjson::Value& value, const char* key) {
    if (!value.HasMember(key) || !value[key].IsString())
        throw std::runtime_error(std::string{"V2 requires string: "} + key);
    return value[key].GetString();
}

TerrainRange range(const rapidjson::Value& value, const char* key) {
    if (!value.HasMember(key) || !value[key].IsArray() || value[key].Size() != 2 ||
        !value[key][0].IsNumber() || !value[key][1].IsNumber())
        throw std::runtime_error(std::string{"V2 requires range: "} + key);
    TerrainRange result{value[key][0].GetFloat(), value[key][1].GetFloat()};
    return result.intersect(result);
}

const rapidjson::Value& object(const rapidjson::Value& value, const char* key) {
    if (!value.HasMember(key) || !value[key].IsObject())
        throw std::runtime_error(std::string{"V2 requires object: "} + key);
    return value[key];
}
} // namespace

TerrainRange TerrainRange::intersect(TerrainRange other) const {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum ||
        !std::isfinite(other.minimum) || !std::isfinite(other.maximum) ||
        other.minimum > other.maximum)
        throw std::runtime_error("Invalid V2 constraint range");
    TerrainRange result{std::max(minimum, other.minimum), std::min(maximum, other.maximum)};
    if (result.minimum > result.maximum)
        throw std::runtime_error("Incompatible V2 constraint ranges");
    return result;
}

void TerrainCellSettings::validate() const {
    for (float value : {side, elevationStep, cliffRun, rampRun, sourceWeight})
        if (!std::isfinite(value) || value <= 0.0F)
            throw std::runtime_error("V2 cell dimensions/weights must be finite and positive");
    if (cliffRun >= side || rampRun > side || rampRun < elevationStep * 2.0F ||
        !std::isfinite(boundaryWeight) || boundaryWeight < 0.0F || !std::isfinite(isolatedWeight) ||
        isolatedWeight < 0.0F || sweeps > 64)
        throw std::runtime_error("Invalid V2 transition or resolution settings");
}

float TerrainV2Recipe::lakeBedHeight(float shoreDistance) const {
    const float t = std::clamp((shoreDistance + bankWidth) / (bankWidth * 2.0F), 0.0F, 1.0F);
    return waterLevel - lakeDepth +
           t * t * (3.0F - 2.0F * t) * (lakeDepth + cells.elevationStep * 2.0F);
}

void TerrainV2Recipe::validate(float mapExtent, std::size_t players) const {
    cells.validate();
    (void)relief.intersect(relief);
    if (!std::isfinite(mapExtent) || mapExtent <= 0.0F || players < 1 || players > 4 ||
        !std::isfinite(baseHeight) || relief.minimum < 0.0F || !std::isfinite(openingRadius) ||
        openingRadius <= cells.side || !std::isfinite(openingBlendWidth) ||
        openingBlendWidth < cells.side || openingRadius + cells.side * 2 >= mapExtent * 0.5F)
        throw std::runtime_error("V2 recipe has insufficient opening capacity");
    if (!std::isfinite(openingSeparationPadding) || openingSeparationPadding < 0)
        throw std::runtime_error("Invalid V2 opening separation padding");
    if (!std::isfinite(peakHeight) || peakHeight < 0)
        throw std::runtime_error("Invalid V2 peak dimensions");
    for (const auto bounds : {ridgeLength, ridgeWidth, ridgeHeightScale, ridgeBend}) {
        (void)bounds.intersect(bounds);
        if (bounds.minimum <= 0)
            throw std::runtime_error("Invalid V2 landform range");
    }
    for (float value : {forestDensity,
                        forestScale,
                        forestSpacing,
                        forestClusterSize,
                        forestCoverage,
                        forestDensityScale})
        if (!std::isfinite(value) || value <= 0)
            throw std::runtime_error("Invalid V2 forest settings");
    if (forestCoverage > 1 || forestClusterSize > 16 || forestDensity > 16)
        throw std::runtime_error("Excessive V2 forest settings");
    if (waterPolicy == TerrainWaterPolicy::forbidden) {
        if (waterFeature != TerrainWaterFeature::none)
            throw std::runtime_error("Dry V2 recipe cannot activate water features");
        return; // Inactive water settings cannot constrain dry generation.
    }
    (void)waterCoverage.intersect({0.0F, 0.5F});
    if (waterPolicy == TerrainWaterPolicy::optional && waterCoverage.maximum == 0)
        return;
    (void)lakeAspect.intersect(lakeAspect);
    if (lakeAspect.minimum <= 0)
        throw std::runtime_error("Invalid V2 lake aspect range");
    if (waterFeature == TerrainWaterFeature::none ||
        (waterPolicy == TerrainWaterPolicy::required && waterCoverage.minimum <= 0.0F) ||
        !std::isfinite(waterLevel) || !std::isfinite(lakeDepth) || lakeDepth <= 0.0F ||
        !std::isfinite(bankWidth) || bankWidth < cells.side || isletCount > 4 ||
        !std::isfinite(isletRadius) || isletRadius < cells.side || !std::isfinite(isletHeight) ||
        isletHeight < 0)
        throw std::runtime_error("Invalid active V2 water contract");
}

TerrainV2Recipes TerrainV2Recipes::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error("Cannot open V2 recipes: " + path.string());
    rapidjson::IStreamWrapper input(stream);
    rapidjson::Document document;
    document.ParseStream(input);
    if (document.HasParseError() || !document.IsObject() || !document.HasMember("version") ||
        !document["version"].IsUint() || document["version"].GetUint() != 1)
        throw std::runtime_error("Invalid V2 recipe document");
    const auto& cell = object(document, "cells");
    TerrainCellSettings settings;
    settings.side = number(cell, "side");
    settings.elevationStep = number(cell, "elevationStep");
    settings.cliffRun = number(cell, "cliffRun");
    settings.rampRun = number(cell, "rampRun");
    settings.sourceWeight = number(cell, "sourceWeight");
    settings.boundaryWeight = number(cell, "boundaryWeight");
    settings.isolatedWeight = number(cell, "isolatedWeight");
    if (!cell.HasMember("sweeps") || !cell["sweeps"].IsUint())
        throw std::runtime_error("V2 sweeps must be an unsigned integer");
    settings.sweeps = cell["sweeps"].GetUint();
    settings.validate();
    const auto& profiles = object(document, "profiles");
    const auto& landforms = object(document, "landforms");
    const auto& layouts = object(document, "layouts");
    TerrainV2Recipes result;
    for (auto entry = layouts.MemberBegin(); entry != layouts.MemberEnd(); ++entry) {
        const auto& value = entry->value;
        if (!value.IsObject())
            throw std::runtime_error("V2 layout must be an object");
        TerrainV2Recipe recipe;
        recipe.layout = TerrainLayoutId{entry->name.GetString()};
        recipe.cells = settings;
        recipe.openingSeparationPadding = number(landforms, "openingSeparationPadding");
        recipe.ridgeLength = range(landforms, "ridgeLength");
        recipe.ridgeWidth = range(landforms, "ridgeWidth");
        recipe.ridgeHeightScale = range(landforms, "ridgeHeightScale");
        recipe.ridgeBend = range(landforms, "ridgeBend");
        recipe.lakeAspect = range(landforms, "lakeAspect");
        recipe.biomeProfile = string(value, "profile");
        const auto& profile = object(profiles, recipe.biomeProfile.c_str());
        recipe.relief = range(value, "relief").intersect(range(profile, "relief"));
        recipe.lowlandBiome = TerrainBiomeId{string(profile, "lowlandBiome")};
        recipe.hillBiome = TerrainBiomeId{string(profile, "hillBiome")};
        recipe.cliffBiome = TerrainBiomeId{string(profile, "cliffBiome")};
        if (!value.HasMember("peakCount") || !value["peakCount"].IsUint() ||
            value["peakCount"].GetUint() > 16)
            throw std::runtime_error("Invalid V2 peak count");
        recipe.peakCount = value["peakCount"].GetUint();
        recipe.peakHeight = number(value, "peakHeight");
        recipe.baseHeight = number(value, "baseHeight");
        recipe.forestDensity = number(value, "forestDensity");
        recipe.forestScale = number(value, "forestScale");
        recipe.forestSpacing = number(value, "forestSpacing");
        recipe.forestClusterSize = number(value, "forestClusterSize");
        recipe.forestCoverage = number(value, "forestCoverage");
        recipe.forestDensityScale = number(value, "forestDensityScale");
        recipe.openingRadius = number(value, "openingRadius");
        recipe.openingBlendWidth = number(value, "openingBlendWidth");
        const auto policy = string(value, "waterPolicy");
        if (policy == "forbidden")
            recipe.waterPolicy = TerrainWaterPolicy::forbidden;
        else if (policy == "optional")
            recipe.waterPolicy = TerrainWaterPolicy::optional;
        else if (policy == "required")
            recipe.waterPolicy = TerrainWaterPolicy::required;
        else
            throw std::runtime_error("Unknown V2 water policy: " + policy);
        if (recipe.waterPolicy != TerrainWaterPolicy::forbidden) {
            const auto& water = object(value, "water");
            const auto feature = string(water, "feature");
            if (feature == "central_lake")
                recipe.waterFeature = TerrainWaterFeature::centralLake;
            else if (feature == "ocean")
                recipe.waterFeature = TerrainWaterFeature::ocean;
            else
                throw std::runtime_error("Unsupported V2 water feature: " + feature);
            recipe.waterCoverage = range(water, "coverage");
            recipe.waterLevel = number(water, "level");
            recipe.lakeDepth = number(water, "depth");
            recipe.bankWidth = number(water, "bankWidth");
            if (!water.HasMember("isletCount") || !water["isletCount"].IsUint())
                throw std::runtime_error("V2 requires an unsigned islet count");
            recipe.isletCount = water["isletCount"].GetUint();
            recipe.isletRadius = number(water, "isletRadius");
            recipe.isletHeight = number(water, "isletHeight");
        }
        if (!value.HasMember("competitive") || !value["competitive"].IsBool())
            throw std::runtime_error("V2 recipe requires competitive flag");
        recipe.competitive = value["competitive"].GetBool();
        recipe.validate(240.0F, 2);
        result.recipes_.emplace(recipe.layout.value, std::move(recipe));
    }
    return result;
}

TerrainV2Recipe TerrainV2Recipes::resolve(const TerrainLayoutDefinition& layout) const {
    const auto found = recipes_.find(layout.id.value);
    if (found == recipes_.end() || layout.source != TerrainLayoutSource::procedural)
        throw std::runtime_error("No procedural V2 recipe for layout: " + layout.id.value);
    auto result = found->second;
    result.shape = layout.shape;
    return result;
}
} // namespace strategy
