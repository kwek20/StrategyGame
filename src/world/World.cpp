#include "world/World.hpp"

#include <algorithm>
#include <utility>

namespace strategy {

Entity& World::createEntity(std::string name, std::string modelKey, PlayerId owner) {
    Entity entity;
    entity.id = nextId_++;
    entity.name = std::move(name);
    entity.modelKey = std::move(modelKey);
    entity.authority.owner = owner;
    entities_.push_back(std::move(entity));
    return entities_.back();
}

bool World::destroyEntity(EntityId id) {
    const auto iterator = std::find_if(entities_.begin(), entities_.end(),
        [id](const Entity& entity) { return entity.id == id; });
    if (iterator == entities_.end()) {
        return false;
    }
    entities_.erase(iterator);
    return true;
}

Entity* World::findEntity(EntityId id) {
    const auto iterator = std::find_if(entities_.begin(), entities_.end(),
        [id](const Entity& entity) { return entity.id == id; });
    return iterator == entities_.end() ? nullptr : &*iterator;
}

const Entity* World::findEntity(EntityId id) const {
    const auto iterator = std::find_if(entities_.begin(), entities_.end(),
        [id](const Entity& entity) { return entity.id == id; });
    return iterator == entities_.end() ? nullptr : &*iterator;
}

void World::replaceEntities(std::vector<Entity> entities) {
    entities_ = std::move(entities);
    nextId_ = 1;
    for (const Entity& entity : entities_) {
        nextId_ = std::max(nextId_, entity.id + 1);
    }
}

} // namespace strategy
