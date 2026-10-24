#include "gameplay/DefinitionRegistry.hpp"
#include "simulation/GameSession.hpp"
#include "world/Collision.hpp"

#include <cmath>
#include <future>
#include <memory>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int strategyTestMain() {
    using namespace strategy;
    const DefinitionRegistry definitions;
    auto generation = std::async(std::launch::async, [&] {
        return std::make_unique<GameSession>(definitions, 7U, "spain", "japan",
            "unassigned", "unassigned", 10U, 1.0F, 1.0F, TerrainLayoutId{"open_plains"},
            nullptr, TerrainMode::v2);
    });
    auto source = generation.get();
    const auto checksum = source->stateChecksum();
    auto prepared = std::make_unique<GameSession>(std::move(*source));
    source.reset();
    auto play = std::make_unique<GameSession>(std::move(*prepared));
    prepared.reset();
    require(play->stateChecksum() == checksum, "Startup handoff preserves authoritative match state");

    EntityId drone = 0, worker = 0;
    for (const auto& entity : play->world().entities()) {
        if (entity.authority.owner != 1) continue;
        if (entity.archetype.value == "construction_drone") drone = entity.id;
        if (entity.archetype.value == "worker") worker = entity.id;
    }
    require(drone && worker, "Moved match retains its starting units");
    play->players().find(1)->resources["alloy"] = 1000;
    const auto origin = play->world().findEntity(drone)->transform.position;
    PlaceBuildingCommand placement{drone, "construct.basic_generator"};
    bool found = false;
    for (float z = origin.z - 32; z <= origin.z + 32 && !found; z += 4)
        for (float x = origin.x - 32; x <= origin.x + 32 && !found; x += 4) {
            if (glm::length(glm::vec2{x - origin.x, z - origin.z}) < 16) continue;
            placement.position = {x, 0, z};
            const auto plan = play->planBuildingPlacement(1, placement);
            // A terrain repair used to incidentally rebind the stale pointer and hide the bug.
            found = plan.valid() && !plan.foundation.levelsTerrain;
        }
    require(found, "Find an ordinary placement that does not rebuild navigation terrain");
    require(play->submit({1, 1, placement}), "Placement submitted after both source owners are destroyed");
    play->advanceTicks();
    const auto feedback = play->consumePlacementEvents();
    require(feedback.size() == 1 && feedback.front().accepted(), "Moved match accepts the building");
    const auto* builder = play->world().findEntity(drone);
    require(builder->unitControl.orderTarget == feedback.front().building &&
                !builder->transient.navigationPath.empty(),
            "Placement routes its drone using the live terrain after startup handoff");
    require(play->players().find(1)->resources.at("alloy") == 900, "Placement spends its cost once");

    const auto position = play->world().findEntity(worker)->transform.position;
    glm::vec3 destination{};
    found = false;
    for (const auto offset : {glm::vec2{8, 0}, glm::vec2{-8, 0}, glm::vec2{0, 8}, glm::vec2{0, -8}}) {
        const glm::vec2 start{position.x, position.z}, end = start + offset;
        if (!MapArea{10}.contains(end, 1) ||
            !play->terrain().permitsMovementSegment(start, end, .65F,
                movementDomainBit(MovementDomain::land)) ||
            overlapsObject(play->world(), definitions, end, .65F, worker)) continue;
        destination = {end.x, 0, end.y};
        found = true;
        break;
    }
    require(found, "Find a nearby walkable destination");
    require(play->submit({1, 2, MoveUnitCommand{worker, destination}}), "Ground movement submitted");
    play->advanceTicks();
    const auto* moving = play->world().findEntity(worker);
    require(moving->unitControl.hasStrategicDestination &&
                moving->unitControl.order == UnitOrderKind::move &&
                !moving->transient.navigationPath.empty(),
            "Ground navigation also survives the destroyed source sessions");
    const auto movedChecksum = play->stateChecksum();
    auto relocated = std::make_unique<GameSession>(std::move(*play));
    play.reset();
    require(relocated->stateChecksum() == movedChecksum, "A warmed session can also be moved safely");
    relocated->advanceTicks();
    return 0;
}
