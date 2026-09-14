#pragma once

#include "assets/ParticleEffectDefinitions.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace strategy {

struct Particle {
    glm::vec3 position{0.0F};
    glm::vec3 velocity{0.0F};
    glm::vec4 color{1.0F};
    glm::vec4 startColor{1.0F};
    glm::vec4 endColor{1.0F};
    float ageSeconds{0.0F};
    float lifetimeSeconds{1.0F};
    float startSize{1.0F};
    float endSize{1.0F};
    float size{1.0F};
    float rotationRadians{0.0F};
    ParticleEffectHandle effect;
    std::uint32_t emitterIndex{0};
};

struct ParticleEmitterDesc {
    ParticleEffectHandle effect;
    glm::vec3 position{0.0F};
    glm::vec3 direction{0.0F, 1.0F, 0.0F};
    std::uint32_t seed{1};
};

// Presentation-only particle state. It consumes effect definitions but owns no world state,
// so simulation and checksums remain independent from frame timing and graphics quality.
class ParticleSystem final {
  public:
    explicit ParticleSystem(const ParticleEffectCatalogue& catalogue,
                            std::size_t particleCapacity = 8192,
                            std::size_t emitterCapacity = 512);

    [[nodiscard]] ParticleEmitterHandle emit(const ParticleEmitterDesc& description);
    void stop(ParticleEmitterHandle emitter, bool removeParticles = false);
    void setTransform(ParticleEmitterHandle emitter, glm::vec3 position, glm::vec3 direction);
    void update(float deltaSeconds);
    void clear();

    [[nodiscard]] const std::vector<Particle>& particles() const { return particles_; }
    [[nodiscard]] std::size_t activeEmitterCount() const;
    [[nodiscard]] std::size_t droppedParticleCount() const { return droppedParticles_; }

  private:
    struct Emitter {
        std::uint32_t generation{1};
        bool active{false};
        bool emitting{false};
        bool burstIssued{false};
        ParticleEffectHandle effect;
        glm::vec3 position{0.0F};
        glm::vec3 direction{0.0F, 1.0F, 0.0F};
        float ageSeconds{0.0F};
        float emissionRemainder{0.0F};
        std::uint32_t randomState{1};
    };

    [[nodiscard]] Emitter* emitter(ParticleEmitterHandle handle);
    void spawn(Emitter& emitter, std::uint32_t emitterIndex,
               const ParticleEffectDefinition& definition, std::uint32_t count);
    [[nodiscard]] float random01(Emitter& emitter);

    const ParticleEffectCatalogue* catalogue_{nullptr};
    std::size_t particleCapacity_{0};
    std::vector<Emitter> emitters_;
    std::vector<Particle> particles_;
    std::size_t droppedParticles_{0};
};

} // namespace strategy
