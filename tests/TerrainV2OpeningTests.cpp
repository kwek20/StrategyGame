#include "simulation/GameSession.hpp"
#include "world/Collision.hpp"
#include "world/Navigation.hpp"
#include "world/ResourceFringe.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void checkRubbleConnections(const strategy::DefinitionRegistry& definitions,
                            const strategy::Terrain& terrain) {
    using namespace strategy;
    glm::vec2 origin{};
    bool found = false;
    for (float z = -96; z <= 96 && !found; z += 8)
        for (float x = -96; x <= 88 && !found; x += 8) {
            bool fits = true;
            const float height = terrain.heightAt(x, z);
            for (int sample = 0; sample <= 12 && fits; ++sample)
                for (float side : {-.8F, 0.0F, .8F}) {
                    const glm::vec2 p{x + sample * .5F, z + side};
                    fits = fits && !terrain.v2Landscape()->plan.reservedRoute(p) &&
                           terrain.v2Landscape()->plan.openingInfluence(p) == 0 &&
                           terrain.waterCoverageAt(p.x, p.y) <= .01F &&
                           terrain.sampleAt(p.x, p.y).slopeDegrees < 18 &&
                           std::abs(terrain.heightAt(p.x, p.y) - height) < .25F;
                }
            if (fits) {
                origin = {x, z};
                found = true;
            }
        }
    require(found, "Connection fixture has a dry flat corridor outside protected regions");
    World world;
    auto& first = world.createEntity("connection A", "v2_scrap_beams", 0);
    definitions.initializeEntity(first);
    first.transform.position = {origin.x, 0, origin.y};
    first.resource.fieldId = 101;
    auto& second = world.createEntity("connection B", "v2_scrap_beams", 0);
    definitions.initializeEntity(second);
    second.transform.position = {origin.x + 6, 0, origin.y};
    second.resource.fieldId = 101;
    std::vector<VegetationChunk> joined(100), separate(100);
    appendResourceFringe(world, terrain, definitions, 7, 10, joined, nullptr);
    second.resource.fieldId = 102;
    appendResourceFringe(world, terrain, definitions, 7, 10, separate, nullptr);
    require(VegetationField{std::move(joined)}.instanceCount() >
                VegetationField{std::move(separate)}.instanceCount(),
            "Rubble joins nodes within a field but never connects adjacent different fields");
}
} // namespace
int strategyTestMain() {
    using namespace strategy;
    const DefinitionRegistry definitions;
    for (const char* layoutId :
         {"open_plains", "rolling_hills", "central_hill", "central_water", "continental"}) {
        GameSession session{definitions,
                            7,
                            "spain",
                            "japan",
                            "unassigned",
                            "unassigned",
                            10,
                            1,
                            1,
                            TerrainLayoutId{layoutId},
                            nullptr,
                            TerrainMode::v2};
        Navigation navigation{session.terrain(), definitions, 10};
        const auto a = session.startingAnchors()[0], b = session.startingAnchors()[1];
        const glm::vec3 approachA{a.x + 18, 0, a.y + 18}, approachB{b.x - 18, 0, b.y - 18};
        std::size_t rubbleCount = 0;
        for (const auto& chunk : session.vegetation().chunks())
            for (const auto& instance : chunk.instances) {
                const auto* type = definitions.archetype(instance.archetype);
                if (!type->tags.contains("resource-fringe"))
                    continue;
                ++rubbleCount;
                const auto& position = instance.transform.position;
                require(type->collisionRadius == 0 && !type->components.contains("resource") &&
                            type->tags.contains("clear-on-build") &&
                            instance.transform.scale.x >= .2F &&
                            instance.transform.scale.x <= .35F &&
                            session.terrain().waterCoverageAt(position.x, position.z) <= .01F &&
                            !overlapsObject(
                                session.world(), definitions, {position.x, position.z}, .3F),
                        "Sparse resource rubble is dry, tiny and nonblocking decoration");
            }
        require(rubbleCount > 0 && rubbleCount < session.resourceLayout().nodes.size() * 12,
                "Connecting rubble remains a sparse layer of tiny fragments");
        if (std::string_view{layoutId} == "central_water") {
            checkRubbleConnections(definitions, session.terrain());
            std::vector<VegetationChunk> original(100), repeated(100), depleted(100);
            appendResourceFringe(
                session.world(), session.terrain(), definitions, 7, 10, original, nullptr);
            appendResourceFringe(
                session.world(), session.terrain(), definitions, 7, 10, repeated, nullptr);
            World depletedWorld = session.world();
            for (auto& entity : depletedWorld.entities())
                if (entity.resource)
                    entity.resource.remaining = 0;
            appendResourceFringe(
                depletedWorld, session.terrain(), definitions, 7, 10, depleted, nullptr);
            for (std::size_t chunk = 0; chunk < original.size(); ++chunk) {
                require(original[chunk].instances.size() == repeated[chunk].instances.size() &&
                            original[chunk].instances.size() == depleted[chunk].instances.size(),
                        "Fringe counts are seeded and stable after resource depletion");
                for (std::size_t instance = 0; instance < original[chunk].instances.size();
                     ++instance) {
                    const auto& originalInstance = original[chunk].instances[instance];
                    const auto& repeatedInstance = repeated[chunk].instances[instance];
                    const auto& depletedInstance = depleted[chunk].instances[instance];
                    require(
                        originalInstance.archetype == repeatedInstance.archetype &&
                            originalInstance.archetype == depletedInstance.archetype &&
                            originalInstance.transform.position ==
                                repeatedInstance.transform.position &&
                            originalInstance.transform.position ==
                                depletedInstance.transform.position,
                        "Fringe reconstruction does not move rubble when sources are exhausted");
                }
            }
        }
        World earlyWorld = session.world();
        for (std::size_t player = 0; player < session.startingAnchors().size(); ++player) {
            const auto anchor = session.startingAnchors()[player];
            // Reserve the largest footprint first: this is a joint capacity check,
            // not a prescribed player build order or a greedy fragmentation test.
            for (const char* id : {"drone_factory",
                                   "alloy_processor",
                                   "fuel_processor",
                                   "charging_pad",
                                   "basic_generator",
                                   "storage_silo"}) {
                const auto* definition = definitions.archetype(EntityArchetypeId{id});
                require(definition && definition->footprint,
                        "Early plan uses real building footprints");
                bool placed = false;
                for (const glm::vec2 offset :
                     {glm::vec2{18, 0},    glm::vec2{0, 18},   glm::vec2{-18, 0},
                      glm::vec2{0, -18},   glm::vec2{18, 6},   glm::vec2{18, -6},
                      glm::vec2{-18, 6},   glm::vec2{-18, -6}, glm::vec2{6, 18},
                      glm::vec2{-6, 18},   glm::vec2{6, -18},  glm::vec2{-6, -18},
                      glm::vec2{18, 18},   glm::vec2{-18, 18}, glm::vec2{18, -18},
                      glm::vec2{-18, -18}, glm::vec2{26, 0},   glm::vec2{-26, 0},
                      glm::vec2{0, 26},    glm::vec2{0, -26}}) {
                    const auto point = anchor + offset;
                    if (overlapsObject(
                            earlyWorld,
                            definitions,
                            spatialShape(definitions, EntityArchetypeId{id}, point, 0)) ||
                        !session.terrain()
                             .evaluatePlacement(
                                 point.x, point.y, *definition->footprint, definition->placement)
                             .valid())
                        continue;
                    auto& building = earlyWorld.createEntity(
                        "early plan fixture", id, static_cast<PlayerId>(player + 1));
                    definitions.initializeEntity(building);
                    building.transform.position = {point.x, 0, point.y};
                    placed = true;
                    break;
                }
                if (!placed)
                    std::cerr << "opening failure layout=" << layoutId << " player=" << player + 1
                              << " building=" << id << std::endl;
                require(placed, "Every opening accommodates a joint processing and charging plan");
            }
        }
        require(!navigation.findPath(earlyWorld, approachA, approachB, 0.65F, 0).empty(),
                "Joint early building plans retain usable ground exits");
        std::cout << layoutId << ": joint opening plan and exits passed\n";
    }
    return 0;
}
