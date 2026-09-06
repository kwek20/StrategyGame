#pragma once

#include "world/Entity.hpp"

#include <string>
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

  private:
    std::vector<Entity> entities_;
    EntityId nextId_{1};
};

} // namespace strategy
