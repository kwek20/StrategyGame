#pragma once

#include "world/Entity.hpp"
#include "terrain/Terrain.hpp"

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <utility>

namespace strategy {

class World final {
  public:
    Entity& createEntity(std::string name, std::string archetypeId = {}, PlayerId owner = 0);
    Entity& createEntity(std::string name, EntityArchetypeId archetype, PlayerId owner = 0) {
        return createEntity(std::move(name), archetype.value, owner);
    }
    bool destroyEntity(EntityId id);
    [[nodiscard]] Entity* findEntity(EntityId id);
    [[nodiscard]] const Entity* findEntity(EntityId id) const;
    void replaceEntities(std::vector<Entity> entities);
    [[nodiscard]] std::vector<Entity>& entities() {
        return entities_;
    }
    [[nodiscard]] const std::vector<Entity>& entities() const {
        return entities_;
    }
    [[nodiscard]] std::size_t size() const {
        return entities_.size();
    }
    [[nodiscard]] std::uint64_t entityRevision(PlayerId player) const {
        const auto found = entityRevisions_.find(player);
        return found == entityRevisions_.end() ? 0 : found->second;
    }
    [[nodiscard]] std::uint64_t replacementRevision() const { return replacementRevision_; }
    void replaceFoundations(std::vector<TerrainFoundation> foundations) {
        foundations_ = std::move(foundations);
    }
    [[nodiscard]] std::vector<TerrainFoundation>& foundations() { return foundations_; }
    [[nodiscard]] const std::vector<TerrainFoundation>& foundations() const { return foundations_; }
  private:
    std::vector<Entity> entities_;
    EntityId nextId_{1};
    std::vector<TerrainFoundation> foundations_;
    std::unordered_map<EntityId, std::size_t> entityIndices_;
    std::map<PlayerId, std::uint64_t> entityRevisions_;
    std::uint64_t replacementRevision_{0};
};

} // namespace strategy
