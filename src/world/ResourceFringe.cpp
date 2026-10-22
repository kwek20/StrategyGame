#include "world/ResourceFringe.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "simulation/DeterministicRandom.hpp"
#include "terrain/Terrain.hpp"
#include "world/Collision.hpp"
#include "world/GenerationProgress.hpp"
#include "world/MapArea.hpp"
#include "world/Vegetation.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <map>
#include <utility>

namespace strategy {
namespace {
glm::vec2 position(const Entity& node) {
    return {node.transform.position.x, node.transform.position.z};
}

// A minimum spanning tree connects each field without dense all-to-all webs. Sorted entity
// identities break equal-distance ties, making visual topology independent of world iteration.
std::vector<std::pair<const Entity*, const Entity*>> connections(std::vector<const Entity*> nodes) {
    std::sort(
        nodes.begin(), nodes.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
    std::vector<std::pair<const Entity*, const Entity*>> result;
    std::vector<float> distance(nodes.size(), std::numeric_limits<float>::max());
    std::vector<std::size_t> parent(nodes.size());
    std::vector<bool> visited(nodes.size());
    if (nodes.empty())
        return result;
    distance[0] = 0;
    for (std::size_t iteration = 0; iteration < nodes.size(); ++iteration) {
        std::size_t next = nodes.size();
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (!visited[i] && (next == nodes.size() || distance[i] < distance[next]))
                next = i;
        visited[next] = true;
        if (iteration)
            result.emplace_back(nodes[parent[next]], nodes[next]);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (visited[i])
                continue;
            const auto delta = position(*nodes[i]) - position(*nodes[next]);
            const float candidate = glm::dot(delta, delta);
            if (candidate < distance[i] ||
                (candidate == distance[i] && nodes[next]->id < nodes[parent[i]]->id)) {
                distance[i] = candidate;
                parent[i] = next;
            }
        }
    }
    return result;
}
} // namespace

void appendResourceFringe(const World& world,
                          const Terrain& terrain,
                          const DefinitionRegistry& definitions,
                          std::uint32_t seed,
                          std::uint32_t mapChunks,
                          std::vector<VegetationChunk>& chunks,
                          WorldGenerationProgress* progress) {
    if (!terrain.v2Landscape())
        return;
    const MapArea map{mapChunks};
    std::vector<SpatialShape> obstacles;
    std::map<std::pair<std::uint64_t, std::string>, std::vector<const Entity*>> fields;
    for (const auto& entity : world.entities()) {
        // Keep exhausted source footprints: depletion must not move or regrow the decoration.
        // Transient unit positions do not affect reconstruction.
        if (entity.resource || entity.kind == EntityKind::building)
            obstacles.push_back(spatialShape(definitions, entity));
        if (entity.resource && entity.resource.fieldId)
            fields[{entity.resource.fieldId, entity.resource.type}].push_back(&entity);
    }
    const auto place = [&](glm::vec2 point,
                           const ResourceFringeDefinition& settings,
                           DeterministicRandom& random) {
        if (!map.contains(point, definitions.matchRules().terrainEdgeMargin) ||
            terrain.v2Landscape()->plan.reservedRoute(point) ||
            terrain.v2Landscape()->plan.openingInfluence(point) > 0 ||
            terrain.waterCoverageAt(point.x, point.y) > .01F ||
            terrain.sampleAt(point.x, point.y).slopeDegrees > 20)
            return false;
        for (const auto& obstacle : obstacles)
            if (overlaps(obstacle, {FootprintShape::circle, point, .3F}))
                return false;
        const float height = terrain.heightAt(point.x, point.y);
        for (const auto offset :
             {glm::vec2{.3F, 0}, glm::vec2{-.3F, 0}, glm::vec2{0, .3F}, glm::vec2{0, -.3F}}) {
            const auto edge = point + offset;
            if (terrain.waterCoverageAt(edge.x, edge.y) > .01F ||
                std::abs(terrain.heightAt(edge.x, edge.y) - height) > .3F)
                return false;
        }
        const auto& variant = settings.variants[random.next() % settings.variants.size()];
        const auto* decoration = definitions.archetype(variant);
        VegetationInstance instance;
        instance.archetype = variant;
        instance.presentation = PresentationId{decoration->presentation};
        instance.transform.position = {point.x, 0, point.y};
        instance.transform.rotationDegrees.y = random.range(0, 360);
        const float scale = random.range(settings.minimumScale, settings.maximumScale);
        instance.transform.scale = {scale, scale, scale};
        const auto cell = map.gridCell(point, static_cast<int>(mapChunks));
        chunks[cell.y * mapChunks + cell.x].instances.push_back(std::move(instance));
        return true;
    };
    for (const auto& [key, nodes] : fields) {
        if (progress)
            progress->throwIfCancellationRequested();
        const auto* field =
            definitions.resourceFieldForNode(ResourceArchetypeId{nodes.front()->archetype.value});
        if (!field || field->decorativeFringe.variants.empty())
            continue;
        const auto& settings = field->decorativeFringe;
        // Local scatter has its own stream, separate from the trails and resource inventories.
        for (const auto* node : nodes) {
            DeterministicRandom random(seed, "resource.fringe." + std::to_string(node->id));
            if (random.range(0, 1) >= settings.chance)
                continue;
            const auto count = 1 + random.next() % settings.maximumPieces;
            const auto* type = definitions.archetype(node->archetype);
            for (std::uint32_t piece = 0; piece < count; ++piece)
                for (int attempt = 0; attempt < 8; ++attempt) {
                    const float angle = random.range(0, glm::two_pi<float>());
                    const float radius =
                        type->collisionRadius +
                        random.range(settings.minimumDistance, settings.maximumDistance);
                    if (place(position(*node) +
                                  glm::vec2{std::cos(angle), std::sin(angle)} * radius,
                              settings,
                              random))
                        break;
                }
        }
        if (settings.connectionSpacing <= 0)
            continue;
        for (const auto& [from, to] : connections(nodes)) {
            if (progress)
                progress->throwIfCancellationRequested();
            const auto a = position(*from), b = position(*to);
            const float length = glm::distance(a, b);
            if (length < .01F || length > settings.maximumConnectionLength)
                continue;
            const auto low = std::min(from->id, to->id), high = std::max(from->id, to->id);
            DeterministicRandom random(
                seed, "resource.fringe.link." + std::to_string(low) + "." + std::to_string(high));
            const glm::vec2 normal{-(b - a).y / length, (b - a).x / length};
            const float bend = random.range(-settings.connectionWidth, settings.connectionWidth);
            const int count = static_cast<int>(std::ceil(length / settings.connectionSpacing));
            for (int piece = 0; piece < count; ++piece) {
                const float t = (piece + random.range(.2F, .8F)) / count;
                const float offset =
                    std::sin(glm::pi<float>() * t) * bend +
                    random.range(-settings.connectionWidth, settings.connectionWidth) * .35F;
                place(glm::mix(a, b, t) + normal * offset, settings, random);
            }
        }
    }
}
} // namespace strategy
