#pragma once

#include "assets/ResourceHandle.hpp"
#include "core/DefinitionId.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace strategy {

struct ParticleEffectTag;
struct ParticleTextureTag;
using ParticleEffectId = DefinitionId<ParticleEffectTag>;
using ParticleTextureId = DefinitionId<ParticleTextureTag>;

enum class ParticleEmissionMode { burst, continuous, timedLoop };
enum class ParticleSpawnShape { point, sphere, cone, box, line };
enum class ParticleBillboardMode { camera, vertical, velocity };
enum class ParticleBlendMode { alpha, additive, premultiplied };

struct ParticleRange {
    float minimum{0.0F};
    float maximum{0.0F};
};

struct ParticleEmissionDefinition {
    ParticleEmissionMode mode{ParticleEmissionMode::burst};
    float ratePerSecond{0.0F};
    std::uint32_t burstCount{0};
    float durationSeconds{0.0F};
    std::uint32_t maximumParticles{1};
};

struct ParticleSpawnDefinition {
    ParticleSpawnShape shape{ParticleSpawnShape::point};
    float radius{0.0F};
    glm::vec3 extents{0.0F};
};

struct ParticleMotionDefinition {
    ParticleRange lifetimeSeconds;
    ParticleRange speed;
    ParticleRange startSize;
    ParticleRange endSize;
    glm::vec3 gravity{0.0F};
    float drag{0.0F};
    float turbulence{0.0F};
    glm::vec4 startColor{1.0F};
    glm::vec4 endColor{1.0F};
};

struct ParticleRenderDefinition {
    ParticleTextureId texture;
    ParticleBillboardMode billboard{ParticleBillboardMode::camera};
    ParticleBlendMode blend{ParticleBlendMode::alpha};
    bool softParticles{false};
};

struct ParticleEffectDefinition {
    ParticleEffectId id;
    std::string quality{"medium"};
    float maximumDistance{120.0F};
    ParticleEmissionDefinition emission;
    ParticleSpawnDefinition spawn;
    ParticleMotionDefinition particle;
    ParticleRenderDefinition render;
};

class ParticleEffectCatalogue final {
  public:
    static ParticleEffectCatalogue load(
        const std::filesystem::path& path = "assets/presentation/particle_effects.json");
    [[nodiscard]] ParticleEffectHandle handle(ParticleEffectId id) const;
    [[nodiscard]] const ParticleEffectDefinition* effect(ParticleEffectHandle handle) const;
    [[nodiscard]] const ParticleEffectDefinition* effect(ParticleEffectId id) const {
        return effect(handle(id));
    }
    [[nodiscard]] ParticleEffectId id(ParticleEffectHandle handle) const;
    [[nodiscard]] std::size_t size() const { return slots_.size(); }

  private:
    struct Slot {
        std::uint32_t generation{1};
        ParticleEffectDefinition definition;
    };
    std::vector<Slot> slots_;
    std::unordered_map<std::string, ParticleEffectHandle> handles_;
};

} // namespace strategy
