#include "world/WorldGeneration.hpp"

#include "localization/Text.hpp"
#include "simulation/DeterministicRandom.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/World.hpp"

#include <cmath>
#include <glm/geometric.hpp>
#include <string>
#include <string_view>
namespace strategy {
namespace {
bool suitable(const Terrain& terrain, float x, float z) {
    const float half = terrain.worldExtent() * 0.5F - 5.0F;
    if (std::abs(x) > half || std::abs(z) > half)
        return false;
    const float normalized = terrain.heightAt(x, z) / Terrain::heightScale;
    if (normalized < 0.16F || normalized > 0.70F)
        return false;
    const float step = Terrain::spacing * 2.0F;
    const float dx = terrain.heightAt(x + step, z) - terrain.heightAt(x - step, z);
    const float dz = terrain.heightAt(x, z + step) - terrain.heightAt(x, z - step);
    return std::sqrt(dx * dx + dz * dz) < 1.2F;
}
bool clearOfBases(float x, float z) {
    const glm::vec2 p{x, z};
    return glm::distance(p, glm::vec2{-28.0F, -28.0F}) > 15.0F &&
           glm::distance(p, glm::vec2{28.0F, 28.0F}) > 15.0F;
}
bool add(World& world, const std::string& type, float x, float z, float rotation) {
    if (overlapsObject(world, {x, z}, collisionRadius(type)))
        return false;
    std::string key = "entity." + type;
    Entity& entity = world.createEntity(Text::get(key), type, 0);
    entity.kind = EntityKind::resource;
    entity.resource.emplace();
    entity.transform.position = {x, 0.0F, z};
    entity.transform.rotationDegrees.y = rotation;
    entity.resource.kind = type == "tree"    ? ResourceKind::wood
                           : type == "stone" ? ResourceKind::stone
                                             : ResourceKind::gold;
    entity.resource.remaining = type == "tree" ? 120.0F : type == "stone" ? 180.0F : 140.0F;
    return true;
}
void clusters(World& world,
              const Terrain& terrain,
              std::uint32_t seed,
              std::string_view streamName,
              const std::string& type,
              int clusterPairs,
              int perCluster,
              float spread) {
    DeterministicRandom random(seed, streamName);
    int made = 0;
    for (int attempt = 0; attempt < clusterPairs * 80 && made < clusterPairs; ++attempt) {
        const float centerX = random.range(-78.0F, 78.0F), centerZ = random.range(-78.0F, 78.0F);
        if (centerX < 0.0F)
            continue;
        if (!suitable(terrain, centerX, centerZ) || !suitable(terrain, -centerX, -centerZ) ||
            !clearOfBases(centerX, centerZ) || !clearOfBases(-centerX, -centerZ))
            continue;
        for (int member = 0; member < perCluster; ++member) {
            const float x = centerX + random.range(-spread, spread),
                        z = centerZ + random.range(-spread, spread);
            if (suitable(terrain, x, z) && clearOfBases(x, z)) {
                const float angle = random.range(0.0F, 360.0F);
                if (!overlapsObject(world, {x, z}, collisionRadius(type)) &&
                    !overlapsObject(world, {-x, -z}, collisionRadius(type))) {
                    add(world, type, x, z, angle);
                    add(world, type, -x, -z, angle + 180.0F);
                }
            }
        }
        ++made;
    }
}
} // namespace
void populateResources(World& world, const Terrain& terrain, std::uint32_t terrainSeed) {
    // Separate salted streams keep resource layouts deterministic and independent
    // from both terrain noise and changes to another resource type.
    clusters(world, terrain, terrainSeed, "resources.wood", "tree", 18, 3, 4.5F);
    clusters(world, terrain, terrainSeed, "resources.stone", "stone", 6, 2, 3.0F);
    clusters(world, terrain, terrainSeed, "resources.gold", "gold", 5, 2, 2.5F);
}
} // namespace strategy
