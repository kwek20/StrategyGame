#include "game/GameplayParticlePresenter.hpp"
#include "app/GameEvents.hpp"
#include "core/EventBus.hpp"
#include "simulation/CombatShotEvent.hpp"

#include "particles/ParticleSystem.hpp"
#include "render/Renderer.hpp"
#include "players/Player.hpp"
#include "world/MapArea.hpp"
#include "world/World.hpp"

#include <set>
#include <string>

#include <glm/geometric.hpp>

namespace strategy {
namespace {

glm::vec3 shownPosition(const Renderer& renderer, const Entity& entity) {
    if (entity.flight)
        return {entity.transform.position.x,
                renderer.terrainHeightAt(entity.transform.position.x, entity.transform.position.z) +
                    entity.transform.position.y,
                entity.transform.position.z};
    const float lift = entity.kind == EntityKind::building ? 1.1F : 0.45F;
    return {entity.transform.position.x,
            renderer.terrainHeightAt(entity.transform.position.x, entity.transform.position.z) + lift,
            entity.transform.position.z};
}

std::uint32_t effectSeed(EntityId entity, std::uint32_t salt) {
    std::uint32_t value = static_cast<std::uint32_t>(entity) ^ (salt * 0x9E3779B9U);
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    return value == 0 ? 1 : value;
}

bool visibleTo(const Entity& entity, const Player* viewer) {
    if (!viewer) return true;
    return viewer->visibility.visibleAt({entity.transform.position.x, entity.transform.position.z});
}

} // namespace

void GameplayParticlePresenter::sync(Renderer& renderer, const World& world, const Player* viewer,
                                     std::uint32_t /*mapChunksPerSide*/) {
    for (auto iterator = impactCooldownFrames_.begin(); iterator != impactCooldownFrames_.end();) {
        if (iterator->second > 0) --iterator->second;
        if (iterator->second == 0) iterator = impactCooldownFrames_.erase(iterator);
        else ++iterator;
    }
    std::set<ActivityKey> active;
    std::map<EntityId, Snapshot> current;
    const auto maintain = [&](ActivityKey key, ParticleEffectId effect, glm::vec3 position,
                              glm::vec3 direction, std::uint32_t salt) {
        active.insert(key);
        auto found = continuous_.find(key);
        if (found == continuous_.end()) {
            const ParticleEmitterHandle emitter = renderer.emitParticle(
                {renderer.particleEffect(std::move(effect)), position, direction,
                 effectSeed(key.entity, salt)});
            if (emitter) continuous_.emplace(key, emitter);
        } else {
            renderer.setParticleEmitterTransform(found->second, position, direction);
        }
    };

    for (const Entity& entity : world.entities()) {
        const glm::vec3 position = shownPosition(renderer, entity);
        const bool visible = visibleTo(entity, viewer);
        current.emplace(entity.id, Snapshot{position,
            entity.health ? entity.health.current : 0.0F, entity.kind, isOperational(entity), visible});
        if (!visible) continue;

        if (entity.processor && entity.processor.state == ProcessorOperationalState::processing) {
            const bool fuel = entity.archetype.value == "fuel_processor" ||
                (entity.archetype.value == "command_hub" &&
                 (entity.processor.bufferedInputs.contains("oil") ||
                  entity.processor.bufferedInputs.contains("uranium")));
            maintain({entity.id, Activity::processing},
                     ParticleEffectId{fuel ? "processing.fuel" : "processing.alloy"},
                     position + glm::vec3{0, 1.0F, 0}, {0, 1, 0}, 31U);
        }
        if (entity.unitControl && entity.unitControl.order == UnitOrderKind::gather && entity.gatherer &&
            !entity.unitControl.hasStrategicDestination) {
            const Entity* target = world.findEntity(entity.gatherer.sourceTarget);
            if (target && target->resource && visibleTo(*target, viewer)) {
                const glm::vec3 resourcePosition = shownPosition(renderer, *target);
                maintain({entity.id, Activity::mining},
                         ParticleEffectId{target->resource.type == "oil" ? "mining.oil" : "mining.scrap"},
                         resourcePosition, {0, 1, 0}, 47U);
                if (target->resource.type != "oil")
                    maintain({entity.id, Activity::miningDust}, ParticleEffectId{"mining.dust"},
                             resourcePosition, {0, 1, 0}, 53U);
            }
        }
        if (entity.unitControl && entity.unitControl.order == UnitOrderKind::construct &&
            !entity.unitControl.hasStrategicDestination) {
            const Entity* target = world.findEntity(entity.unitControl.orderTarget);
            if (target && target->construction && !isOperational(*target) &&
                visibleTo(*target, viewer)) {
                const glm::vec3 destination = shownPosition(renderer, *target);
                maintain({entity.id, Activity::construction},
                         ParticleEffectId{"construction.drone_beam"}, position,
                         glm::length(destination - position) > 0.001F
                             ? glm::normalize(destination - position) : glm::vec3{0, -1, 0}, 59U);
                maintain({entity.id, Activity::constructionContact},
                         ParticleEffectId{"construction.contact"}, destination,
                         {0, 1, 0}, 61U);
            }
        }
        if (initialized_) {
            const auto old = previous_.find(entity.id);
            if (old != previous_.end() && entity.health &&
                entity.health.current + 0.001F < old->second.health &&
                !impactCooldownFrames_.contains(entity.id)) {
                (void)renderer.emitParticle({renderer.particleEffect(ParticleEffectId{"impact.kinetic"}),
                                             position, {0, 1, 0}, effectSeed(entity.id, 71U)});
                impactCooldownFrames_[entity.id] = 6;
            }
        }
    }

    for (auto iterator = continuous_.begin(); iterator != continuous_.end();) {
        if (!active.contains(iterator->first)) {
            renderer.stopParticle(iterator->second);
            iterator = continuous_.erase(iterator);
        } else ++iterator;
    }
    if (initialized_) {
        for (const auto& [id, snapshot] : previous_) {
            if (current.contains(id) || !snapshot.operational || !snapshot.visible) continue;
            const char* effect = snapshot.kind == EntityKind::building
                                     ? "explosion.large" : "explosion.small";
            (void)renderer.emitParticle({renderer.particleEffect(ParticleEffectId{effect}),
                                         snapshot.position, {0, 1, 0}, effectSeed(id, 83U)});
        }
    }
    previous_ = std::move(current);
    initialized_ = true;
}

void GameplayParticlePresenter::presentShot(Renderer& renderer,EventBus& events,const CombatShotEvent& shot,const Player* viewer,std::uint32_t /*chunks*/) {
    const auto visible=[&](glm::vec3 p) {
        if (!viewer) return true;
        return viewer->visibility.visibleAt({p.x, p.z});
    };
    if (visible(shot.origin)) {
        auto end=shot.origin;
        const int samples = std::max(
            1, static_cast<int>(std::ceil(glm::length(shot.destination - shot.origin) / .5F)));
        for (int i = 1; i <= samples; ++i) {
            const auto p = glm::mix(shot.origin, shot.destination, static_cast<float>(i) / samples);
            if (!visible(p))
                break;
            end = p;
        }
        renderer.showWeaponFire(shot.attacker, shot.origin, end);
        if (shot.cannon)
            events.enqueue(AudioEvent{AudioCue::cannonFire});
        const auto direction = shot.destination - shot.origin;
        (void)renderer.emitParticle(
            {renderer.particleEffect(
                 ParticleEffectId{shot.cannon ? "weapon.muzzle_cannon" : "weapon.muzzle_kinetic"}),
             shot.origin,
             glm::length(direction) > .001F ? glm::normalize(direction) : glm::vec3{0, 1, 0},
             effectSeed(shot.attacker, static_cast<std::uint32_t>(shot.shot))});
    }
    if (shot.impact && visible(shot.destination)) {
        (void)renderer.emitParticle(
            {renderer.particleEffect(ParticleEffectId{shot.destroyed ? "explosion.vehicle"
                                                      : shot.cannon  ? "impact.cannon"
                                                                     : "impact.kinetic"}),
             shot.destination,
             {0, 1, 0},
             effectSeed(shot.target, static_cast<std::uint32_t>(shot.shot))});
        if (shot.cannon)
            events.enqueue(
                AudioEvent{shot.destroyed ? AudioCue::vehicleDestroyed : AudioCue::cannonImpact});
        if (shot.destroyed)
            (void)renderer.emitParticle(
                {renderer.particleEffect(ParticleEffectId{"destruction.vehicle_smoke"}),
                 shot.destination,
                 {0, 1, 0},
                 effectSeed(shot.target, 97)});
        impactCooldownFrames_[shot.target] = 6;
        if (shot.destroyed)
            previous_.erase(shot.target);
    }
}

void GameplayParticlePresenter::reset(Renderer& renderer) {
    for (const auto& [key, emitter] : continuous_) {
        (void)key;
        renderer.stopParticle(emitter, true);
    }
    continuous_.clear();
    previous_.clear();
    impactCooldownFrames_.clear();
    initialized_ = false;
}

} // namespace strategy
