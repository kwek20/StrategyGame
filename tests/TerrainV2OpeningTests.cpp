#include "simulation/GameSession.hpp"
#include "world/Collision.hpp"
#include "world/Navigation.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
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
        World earlyWorld = session.world();
        for (std::size_t player = 0; player < session.startingAnchors().size(); ++player) {
            const auto anchor = session.startingAnchors()[player];
            for (const char* id : {"alloy_processor", "fuel_processor", "charging_pad"}) {
                const auto* definition = definitions.archetype(EntityArchetypeId{id});
                require(definition && definition->footprint,
                        "Early plan uses real building footprints");
                bool placed = false;
                for (const glm::vec2 offset : {glm::vec2{18, 0},
                                               glm::vec2{0, 18},
                                               glm::vec2{-18, 0},
                                               glm::vec2{0, -18},
                                               glm::vec2{18, 6},
                                               glm::vec2{18, -6},
                                               glm::vec2{-18, 6},
                                               glm::vec2{-18, -6},
                                               glm::vec2{6, 18},
                                               glm::vec2{-6, 18},
                                               glm::vec2{6, -18},
                                               glm::vec2{-6, -18}}) {
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
