#include "world/World.hpp"

#include <algorithm>
#include <utility>

namespace strategy {

Entity& World::createEntity(std::string name, std::string archetypeId, PlayerId owner) {
    Entity entity;
    entity.id = nextId_++;
    entity.name = std::move(name);
    entity.archetype = EntityArchetypeId{archetypeId};
    entity.presentation = PresentationId{archetypeId};
    entity.authority.owner = owner;
    entities_.push_back(std::move(entity));
    entityIndices_[entities_.back().id] = entities_.size() - 1;
    return entities_.back();
}

bool World::destroyEntity(EntityId id) {
    const auto iterator = std::find_if(
        entities_.begin(), entities_.end(), [id](const Entity& entity) { return entity.id == id; });
    if (iterator == entities_.end()) {
        return false;
    }
    entities_.erase(iterator);
    entityIndices_.clear();
    for (std::size_t index = 0; index < entities_.size(); ++index)
        entityIndices_[entities_[index].id] = index;
    return true;
}

Entity* World::findEntity(EntityId id) {
    const auto found = entityIndices_.find(id);
    return found == entityIndices_.end() ? nullptr : &entities_[found->second];
}

const Entity* World::findEntity(EntityId id) const {
    const auto found = entityIndices_.find(id);
    return found == entityIndices_.end() ? nullptr : &entities_[found->second];
}

void World::replaceEntities(std::vector<Entity> entities) {
    entities_ = std::move(entities);
    nextId_ = 1;
    entityIndices_.clear();
    for (std::size_t index = 0; index < entities_.size(); ++index) {
        const Entity& entity = entities_[index];
        entityIndices_[entity.id] = index;
        nextId_ = std::max(nextId_, entity.id + 1);
    }
}

} // namespace strategy
