#include "assets/ParticleEffectDefinitions.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <glm/common.hpp>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <stdexcept>
#include <string_view>

namespace strategy {
namespace {

[[noreturn]] void invalid(const std::string& id, const std::string& reason) {
    throw std::runtime_error("Invalid particle effect '" + id + "': " + reason);
}

const rapidjson::Value& object(const rapidjson::Value& parent,
                              const char* name,
                              const std::string& id) {
    if (!parent.HasMember(name) || !parent[name].IsObject()) invalid(id, std::string{name} + " must be an object");
    return parent[name];
}

std::string requiredString(const rapidjson::Value& parent,
                           const char* name,
                           const std::string& id) {
    if (!parent.HasMember(name) || !parent[name].IsString() || parent[name].GetStringLength() == 0)
        invalid(id, std::string{name} + " must be a non-empty string");
    return parent[name].GetString();
}

float number(const rapidjson::Value& parent,
             const char* name,
             const std::string& id,
             float fallback = 0.0F) {
    if (!parent.HasMember(name)) return fallback;
    if (!parent[name].IsNumber()) invalid(id, std::string{name} + " must be numeric");
    return parent[name].GetFloat();
}

std::uint32_t unsignedNumber(const rapidjson::Value& parent,
                             const char* name,
                             const std::string& id,
                             std::uint32_t fallback = 0) {
    if (!parent.HasMember(name)) return fallback;
    if (!parent[name].IsUint()) invalid(id, std::string{name} + " must be an unsigned integer");
    return parent[name].GetUint();
}

template <std::size_t Size>
std::array<float, Size> vector(const rapidjson::Value& parent,
                               const char* name,
                               const std::string& id) {
    if (!parent.HasMember(name) || !parent[name].IsArray() || parent[name].Size() != Size)
        invalid(id, std::string{name} + " must contain " + std::to_string(Size) + " numbers");
    std::array<float, Size> result{};
    for (rapidjson::SizeType index = 0; index < Size; ++index) {
        if (!parent[name][index].IsNumber()) invalid(id, std::string{name} + " must contain only numbers");
        result[index] = parent[name][index].GetFloat();
    }
    return result;
}

ParticleRange range(const rapidjson::Value& parent, const char* name, const std::string& id) {
    const auto values = vector<2>(parent, name, id);
    if (values[0] < 0.0F || values[1] < values[0]) invalid(id, std::string{name} + " must be an ordered non-negative range");
    return {values[0], values[1]};
}

template <class Enum>
Enum choice(std::string_view value,
            const std::initializer_list<std::pair<std::string_view, Enum>>& choices,
            const std::string& id,
            const char* field) {
    const auto found = std::find_if(choices.begin(), choices.end(),
                                    [&](const auto& item) { return item.first == value; });
    if (found == choices.end()) invalid(id, std::string{"unknown "} + field + " '" + std::string{value} + "'");
    return found->second;
}

} // namespace

ParticleEffectCatalogue ParticleEffectCatalogue::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Missing particle-effect definitions: " + path.string());
    rapidjson::IStreamWrapper input(stream);
    rapidjson::Document document;
    document.ParseStream(input);
    if (document.HasParseError() || !document.IsObject() || !document.HasMember("version") ||
        !document["version"].IsUint() || document["version"].GetUint() != 1 ||
        !document.HasMember("effects") || !document["effects"].IsObject())
        throw std::runtime_error("Invalid particle-effect definition document: " + path.string());

    ParticleEffectCatalogue catalogue;
    for (const auto& member : document["effects"].GetObject()) {
        const std::string id = member.name.GetString();
        if (id.empty() || !member.value.IsObject()) invalid(id, "definition must be an object");
        const auto& value = member.value;
        ParticleEffectDefinition effect;
        effect.id = ParticleEffectId{id};
        effect.quality = requiredString(value, "quality", id);
        if (effect.quality != "low" && effect.quality != "medium" && effect.quality != "high")
            invalid(id, "quality must be low, medium, or high");
        effect.maximumDistance = number(value, "maximumDistance", id);
        if (effect.maximumDistance <= 0.0F) invalid(id, "maximumDistance must be positive");

        const auto& emission = object(value, "emission", id);
        const std::string emissionMode = requiredString(emission, "mode", id);
        effect.emission.mode = choice<ParticleEmissionMode>(
            emissionMode, {{"burst", ParticleEmissionMode::burst},
                           {"continuous", ParticleEmissionMode::continuous},
                           {"timed_loop", ParticleEmissionMode::timedLoop}}, id, "emission mode");
        effect.emission.ratePerSecond = number(emission, "ratePerSecond", id);
        effect.emission.burstCount = unsignedNumber(emission, "burstCount", id);
        effect.emission.durationSeconds = number(emission, "durationSeconds", id);
        effect.emission.maximumParticles = unsignedNumber(emission, "maximumParticles", id);
        if (effect.emission.maximumParticles == 0) invalid(id, "maximumParticles must be positive");
        if (effect.emission.mode == ParticleEmissionMode::burst && effect.emission.burstCount == 0)
            invalid(id, "burst effects require burstCount");
        if (effect.emission.mode != ParticleEmissionMode::burst && effect.emission.ratePerSecond <= 0.0F)
            invalid(id, "looping effects require positive ratePerSecond");
        if (effect.emission.mode == ParticleEmissionMode::timedLoop && effect.emission.durationSeconds <= 0.0F)
            invalid(id, "timed_loop effects require positive durationSeconds");

        const auto& spawn = object(value, "spawn", id);
        const std::string spawnShape = requiredString(spawn, "shape", id);
        effect.spawn.shape = choice<ParticleSpawnShape>(
            spawnShape, {{"point", ParticleSpawnShape::point}, {"sphere", ParticleSpawnShape::sphere},
                         {"cone", ParticleSpawnShape::cone}, {"box", ParticleSpawnShape::box},
                         {"line", ParticleSpawnShape::line}}, id, "spawn shape");
        effect.spawn.radius = number(spawn, "radius", id);
        const auto extents = vector<3>(spawn, "extents", id);
        effect.spawn.extents = {extents[0], extents[1], extents[2]};
        if (effect.spawn.radius < 0.0F || glm::any(glm::lessThan(effect.spawn.extents, glm::vec3{0.0F})))
            invalid(id, "spawn dimensions cannot be negative");

        const auto& particle = object(value, "particle", id);
        effect.particle.lifetimeSeconds = range(particle, "lifetimeSeconds", id);
        effect.particle.speed = range(particle, "speed", id);
        effect.particle.startSize = range(particle, "startSize", id);
        effect.particle.endSize = range(particle, "endSize", id);
        const auto gravity = vector<3>(particle, "gravity", id);
        effect.particle.gravity = {gravity[0], gravity[1], gravity[2]};
        effect.particle.drag = number(particle, "drag", id);
        effect.particle.turbulence = number(particle, "turbulence", id);
        if (effect.particle.lifetimeSeconds.minimum <= 0.0F || effect.particle.startSize.minimum <= 0.0F ||
            effect.particle.endSize.minimum <= 0.0F || effect.particle.drag < 0.0F ||
            effect.particle.turbulence < 0.0F)
            invalid(id, "lifetime, sizes, drag, and turbulence must be valid non-negative values");
        const auto startColor = vector<4>(particle, "startColor", id);
        const auto endColor = vector<4>(particle, "endColor", id);
        effect.particle.startColor = {startColor[0], startColor[1], startColor[2], startColor[3]};
        effect.particle.endColor = {endColor[0], endColor[1], endColor[2], endColor[3]};
        for (float component : startColor) if (component < 0.0F || component > 1.0F) invalid(id, "color components must be between zero and one");
        for (float component : endColor) if (component < 0.0F || component > 1.0F) invalid(id, "color components must be between zero and one");

        const auto& render = object(value, "render", id);
        effect.render.texture = ParticleTextureId{requiredString(render, "texture", id)};
        effect.render.billboard = choice<ParticleBillboardMode>(
            requiredString(render, "billboard", id),
            {{"camera", ParticleBillboardMode::camera}, {"vertical", ParticleBillboardMode::vertical},
             {"velocity", ParticleBillboardMode::velocity}}, id, "billboard mode");
        effect.render.blend = choice<ParticleBlendMode>(
            requiredString(render, "blend", id),
            {{"alpha", ParticleBlendMode::alpha}, {"additive", ParticleBlendMode::additive},
             {"premultiplied", ParticleBlendMode::premultiplied}}, id, "blend mode");
        if (render.HasMember("softParticles")) {
            if (!render["softParticles"].IsBool()) invalid(id, "softParticles must be boolean");
            effect.render.softParticles = render["softParticles"].GetBool();
        }
        if (catalogue.handles_.contains(id))
            invalid(id, "duplicate ID");
        const std::uint32_t index = static_cast<std::uint32_t>(catalogue.slots_.size());
        catalogue.slots_.push_back({1, std::move(effect)});
        catalogue.handles_.emplace(id, ParticleEffectHandle{index, 1});
    }
    if (catalogue.slots_.empty()) throw std::runtime_error("Particle-effect catalogue is empty");
    return catalogue;
}

ParticleEffectHandle ParticleEffectCatalogue::handle(ParticleEffectId id) const {
    const auto found = handles_.find(id.value);
    return found == handles_.end() ? ParticleEffectHandle{} : found->second;
}

const ParticleEffectDefinition* ParticleEffectCatalogue::effect(ParticleEffectHandle handle) const {
    if (!handle || handle.index >= slots_.size() ||
        slots_[handle.index].generation != handle.generation)
        return nullptr;
    return &slots_[handle.index].definition;
}

ParticleEffectId ParticleEffectCatalogue::id(ParticleEffectHandle handle) const {
    const ParticleEffectDefinition* definition = effect(handle);
    return definition ? definition->id : ParticleEffectId{};
}

} // namespace strategy
