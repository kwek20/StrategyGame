#include "game/GameplayParticlePresenter.hpp"

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

bool visibleTo(const Entity& entity, const Player* viewer, std::uint32_t mapChunksPerSide) {
    if (!viewer) return true;
    const glm::ivec2 cell = MapArea{mapChunksPerSide}.gridCell(
        {entity.transform.position.x, entity.transform.position.z}, Player::explorationCells);
    const std::size_t index = static_cast<std::size_t>(cell.y * Player::explorationCells + cell.x);
    return index < viewer->visible.size() && viewer->visible[index] != 0;
}

} // namespace

void GameplayParticlePresenter::sync(Renderer& renderer, const World& world, const Player* viewer,
                                     std::uint32_t mapChunksPerSide) {
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
        const bool visible = visibleTo(entity, viewer, mapChunksPerSide);
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
            if (target && target->resource && visibleTo(*target, viewer, mapChunksPerSide)) {
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
                visibleTo(*target, viewer, mapChunksPerSide)) {
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
