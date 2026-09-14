#pragma once

#include "assets/ResourceHandle.hpp"
#include "world/Entity.hpp"

#include <cstdint>
#include <map>

#include <glm/vec3.hpp>

namespace strategy {

class Renderer;
class World;
struct Player;

// Translates authoritative state into disposable presentation effects. This state is never
// saved, checksummed, or consulted by gameplay.
class GameplayParticlePresenter final {
  public:
    void sync(Renderer& renderer, const World& world, const Player* viewer,
              std::uint32_t mapChunksPerSide);
    void reset(Renderer& renderer);

  private:
    enum class Activity : std::uint8_t {
        mining,
        miningDust,
        construction,
        constructionContact,
        processing
    };
    struct ActivityKey {
        EntityId entity{0};
        Activity activity{Activity::mining};
        friend bool operator<(const ActivityKey& lhs, const ActivityKey& rhs) {
            return lhs.entity < rhs.entity ||
                   (lhs.entity == rhs.entity && lhs.activity < rhs.activity);
        }
    };
    struct Snapshot {
        glm::vec3 position{0.0F};
        float health{0.0F};
        EntityKind kind{EntityKind::decoration};
        bool operational{true};
        bool visible{false};
    };

    std::map<ActivityKey, ParticleEmitterHandle> continuous_;
    std::map<EntityId, Snapshot> previous_;
    std::map<EntityId, std::uint8_t> impactCooldownFrames_;
    bool initialized_{false};
};

} // namespace strategy
