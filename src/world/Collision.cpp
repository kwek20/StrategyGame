#include "world/Collision.hpp"

#include "world/World.hpp"

#include <algorithm>
#include <cctype>
#include <glm/geometric.hpp>
namespace strategy {
float collisionRadius(const std::string& key) {
    std::string normalized = key;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (normalized == "worker" || normalized.find("worker_") != std::string::npos)
        return 0.65F;
    if (normalized.rfind("town_center", 0) == 0 ||
        normalized.find("towncenter_") != std::string::npos)
        return 10.5F;
    if (normalized == "tree" || normalized.find("tree") != std::string::npos)
        return 1.2F;
    if (normalized == "stone" || normalized == "gold" ||
        normalized.find("resource_rock") != std::string::npos ||
        normalized.find("resource_gold") != std::string::npos)
        return 1.5F;
    static constexpr const char* buildingNames[] = {"house",
                                                    "barracks",
                                                    "archery",
                                                    "market",
                                                    "storage",
                                                    "temple",
                                                    "tower",
                                                    "windmill",
                                                    "wonder",
                                                    "dock",
                                                    "port",
                                                    "farm",
                                                    "wall"};
    for (const char* building : buildingNames)
        if (normalized.find(building) != std::string::npos)
            return 5.0F;
    return 1.0F;
}
bool overlapsObject(const World& world, glm::vec2 position, float radius, EntityId ignored) {
    for (const Entity& entity : world.entities()) {
        if (entity.id == ignored)
            continue;
        const float combined = radius + collisionRadius(entity.modelKey);
        const glm::vec2 delta =
            position - glm::vec2{entity.transform.position.x, entity.transform.position.z};
        if (glm::dot(delta, delta) < combined * combined)
            return true;
    }
    return false;
}
} // namespace strategy
