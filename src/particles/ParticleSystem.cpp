#include "particles/ParticleSystem.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace strategy {
namespace {

float mixRange(const ParticleRange& range, float value) {
    return range.minimum + (range.maximum - range.minimum) * value;
}

glm::vec3 perpendicular(glm::vec3 direction) {
    const glm::vec3 reference = std::abs(direction.y) < 0.9F ? glm::vec3{0, 1, 0}
                                                             : glm::vec3{1, 0, 0};
    return glm::normalize(glm::cross(direction, reference));
}

} // namespace

ParticleSystem::ParticleSystem(const ParticleEffectCatalogue& catalogue,
                               std::size_t particleCapacity,
                               std::size_t emitterCapacity)
    : catalogue_(&catalogue), particleCapacity_(particleCapacity), emitters_(emitterCapacity) {
    particles_.reserve(particleCapacity);
}

ParticleEmitterHandle ParticleSystem::emit(const ParticleEmitterDesc& description) {
    if (!catalogue_->effect(description.effect)) return {};
    for (std::uint32_t index = 0; index < emitters_.size(); ++index) {
        Emitter& slot = emitters_[index];
        if (slot.active) continue;
        slot.active = true;
        slot.emitting = true;
        slot.burstIssued = false;
        slot.effect = description.effect;
        slot.position = description.position;
        slot.direction = glm::length(description.direction) > 0.0001F
                             ? glm::normalize(description.direction)
                             : glm::vec3{0, 1, 0};
        slot.ageSeconds = 0.0F;
        slot.emissionRemainder = 0.0F;
        slot.randomState = description.seed == 0 ? 1 : description.seed;
        return {index, slot.generation};
    }
    return {};
}

ParticleSystem::Emitter* ParticleSystem::emitter(ParticleEmitterHandle handle) {
    if (!handle || handle.index >= emitters_.size()) return nullptr;
    Emitter& slot = emitters_[handle.index];
    return slot.active && slot.generation == handle.generation ? &slot : nullptr;
}

void ParticleSystem::stop(ParticleEmitterHandle handle, bool removeParticles) {
    Emitter* slot = emitter(handle);
    if (!slot) return;
    slot->emitting = false;
    if (removeParticles) {
        particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                        [&](const Particle& particle) {
                                            return particle.emitterIndex == handle.index;
                                        }), particles_.end());
    }
}

void ParticleSystem::setTransform(ParticleEmitterHandle handle,
                                  glm::vec3 position,
                                  glm::vec3 direction) {
    Emitter* slot = emitter(handle);
    if (!slot) return;
    slot->position = position;
    if (glm::length(direction) > 0.0001F) slot->direction = glm::normalize(direction);
}

float ParticleSystem::random01(Emitter& emitter) {
    std::uint32_t value = emitter.randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    emitter.randomState = value;
    return static_cast<float>(value >> 8) * (1.0F / 16777216.0F);
}

void ParticleSystem::spawn(Emitter& emitter, std::uint32_t emitterIndex,
                           const ParticleEffectDefinition& definition, std::uint32_t count) {
    const std::size_t emitterParticles = static_cast<std::size_t>(std::count_if(
        particles_.begin(), particles_.end(), [&](const Particle& particle) {
            return particle.emitterIndex == emitterIndex;
        }));
    const std::size_t emitterAvailable = definition.emission.maximumParticles > emitterParticles
        ? definition.emission.maximumParticles - emitterParticles : 0;
    const std::size_t available = std::min(particleCapacity_ - particles_.size(), emitterAvailable);
    if (count > available) droppedParticles_ += count - available;
    count = static_cast<std::uint32_t>(std::min<std::size_t>(count, available));
    const glm::vec3 tangent = perpendicular(emitter.direction);
    const glm::vec3 bitangent = glm::cross(emitter.direction, tangent);
    for (std::uint32_t index = 0; index < count; ++index) {
        const float azimuth = random01(emitter) * 2.0F * std::numbers::pi_v<float>;
        const float radial = std::sqrt(random01(emitter));
        const glm::vec3 disc = tangent * std::cos(azimuth) * radial +
                               bitangent * std::sin(azimuth) * radial;
        glm::vec3 offset{0.0F};
        glm::vec3 direction = emitter.direction;
        switch (definition.spawn.shape) {
        case ParticleSpawnShape::sphere:
            direction = glm::normalize(disc + emitter.direction * (random01(emitter) * 2.0F - 1.0F));
            offset = direction * definition.spawn.radius * std::cbrt(random01(emitter));
            break;
        case ParticleSpawnShape::cone:
            direction = glm::normalize(emitter.direction + disc * definition.spawn.radius);
            break;
        case ParticleSpawnShape::box:
            offset = (glm::vec3{random01(emitter), random01(emitter), random01(emitter)} * 2.0F - 1.0F) *
                     definition.spawn.extents;
            break;
        case ParticleSpawnShape::line:
            offset = emitter.direction * (random01(emitter) * 2.0F - 1.0F) *
                     std::max(definition.spawn.extents.z, definition.spawn.extents.x);
            break;
        case ParticleSpawnShape::point:
            break;
        }
        Particle particle;
        particle.position = emitter.position + offset;
        particle.velocity = direction * mixRange(definition.particle.speed, random01(emitter));
        particle.lifetimeSeconds = mixRange(definition.particle.lifetimeSeconds, random01(emitter));
        particle.startSize = mixRange(definition.particle.startSize, random01(emitter));
        particle.endSize = mixRange(definition.particle.endSize, random01(emitter));
        particle.size = particle.startSize;
        particle.startColor = definition.particle.startColor;
        particle.endColor = definition.particle.endColor;
        particle.color = particle.startColor;
        particle.rotationRadians = random01(emitter) * 2.0F * std::numbers::pi_v<float>;
        particle.effect = emitter.effect;
        particle.emitterIndex = emitterIndex;
        particles_.push_back(particle);
    }
}

void ParticleSystem::update(float deltaSeconds) {
    const float dt = glm::clamp(deltaSeconds, 0.0F, 0.1F);
    for (std::uint32_t index = 0; index < emitters_.size(); ++index) {
        Emitter& slot = emitters_[index];
        if (!slot.active) continue;
        const ParticleEffectDefinition* definition = catalogue_->effect(slot.effect);
        if (!definition) { slot.emitting = false; continue; }
        if (slot.emitting) {
            if (definition->emission.mode == ParticleEmissionMode::burst) {
                if (!slot.burstIssued) {
                    spawn(slot, index, *definition, definition->emission.burstCount);
                    slot.burstIssued = true;
                    slot.emitting = false;
                }
            } else {
                const bool inWindow = definition->emission.mode == ParticleEmissionMode::continuous ||
                                      slot.ageSeconds < definition->emission.durationSeconds;
                if (inWindow) {
                    slot.emissionRemainder += definition->emission.ratePerSecond * dt;
                    const auto count = static_cast<std::uint32_t>(slot.emissionRemainder);
                    slot.emissionRemainder -= static_cast<float>(count);
                    spawn(slot, index, *definition, count);
                } else slot.emitting = false;
            }
        }
        slot.ageSeconds += dt;
    }

    for (Particle& particle : particles_) {
        const ParticleEffectDefinition* definition = catalogue_->effect(particle.effect);
        if (!definition) { particle.ageSeconds = particle.lifetimeSeconds; continue; }
        particle.ageSeconds += dt;
        const float damping = std::exp(-definition->particle.drag * dt);
        particle.velocity = (particle.velocity + definition->particle.gravity * dt) * damping;
        particle.position += particle.velocity * dt;
        const float progress = glm::clamp(particle.ageSeconds / particle.lifetimeSeconds, 0.0F, 1.0F);
        particle.color = glm::mix(particle.startColor, particle.endColor, progress);
        particle.size = glm::mix(particle.startSize, particle.endSize, progress);
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& particle) {
                         return particle.ageSeconds >= particle.lifetimeSeconds;
                     }), particles_.end());
    for (std::uint32_t index = 0; index < emitters_.size(); ++index) {
        Emitter& slot = emitters_[index];
        if (!slot.active || slot.emitting) continue;
        const bool hasParticles = std::any_of(particles_.begin(), particles_.end(), [&](const Particle& particle) {
            return particle.emitterIndex == index;
        });
        if (!hasParticles) {
            slot.active = false;
            slot.effect = {};
            ++slot.generation;
            if (slot.generation == 0) slot.generation = 1;
        }
    }
}

void ParticleSystem::clear() {
    particles_.clear();
    for (Emitter& slot : emitters_) {
        if (slot.active) {
            slot.active = false;
            ++slot.generation;
            if (slot.generation == 0) slot.generation = 1;
        }
    }
}

std::size_t ParticleSystem::activeEmitterCount() const {
    return static_cast<std::size_t>(std::count_if(emitters_.begin(), emitters_.end(),
                                                   [](const Emitter& slot) { return slot.active; }));
}

} // namespace strategy
