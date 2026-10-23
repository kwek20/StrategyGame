#include "TerrainLevelingFixture.hpp"
#include "simulation/GameSession.hpp"
#include "ui/PlacementFeedback.hpp"

#include <cmath>
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
    GameSession session{definitions,
                        7,
                        "spain",
                        "japan",
                        "unassigned",
                        "unassigned",
                        10,
                        1,
                        1,
                        TerrainLayoutId{"open_plains"},
                        nullptr,
                        TerrainMode::v2};
    const auto start = session.startingAnchors().front();
    session.replaceWorld({}, 7, {}, 10, TerrainLayoutId{"open_plains"}, TerrainMode::v2);
    auto& drone = session.world().createEntity("Builder", "construction_drone", 1);
    definitions.initializeEntity(drone);
    const auto builder = drone.id;
    drone.transform.position = {start.x, 6, start.y};
    auto* player = session.players().find(1);
    player->resources["alloy"] = 10000;
    const auto cost = definitions.recipe(RecipeId{"construct.basic_generator"})->cost.at("alloy");
    const auto place = [&](std::uint64_t sequence, glm::vec3 position, EntityId source = 0) {
        require(session.submit(
                    {1,
                     sequence,
                     PlaceBuildingCommand{
                         source ? source : builder, "construct.basic_generator", position, {}}}),
                "Placement command queues");
        session.advanceTicks();
        auto events = session.consumePlacementEvents();
        require(events.size() == 1 && events[0].sequence == sequence && events[0].player == 1,
                "Exactly one correlated placement outcome");
        require(session.consumePlacementEvents().empty(), "Placement events drain once");
        return events.front();
    };
    require(place(1, {10000, 0, 0}).failure == PlacementFailure::outsideMap,
            "Outside-map rejection is explicit");
    require(player->resources.at("alloy") == 10000 && session.world().entities().size() == 1,
            "Rejected placement does not spend or spawn");
    const auto accepted = place(2, {start.x, 0, start.y});
    require(accepted.accepted() && accepted.building &&
                session.world().findEntity(accepted.building),
            "Accepted result identifies the created building");
    require(player->resources.at("alloy") == 10000 - cost,
            "Successful placement spends exactly once");
    require(place(3, {start.x, 0, start.y}).failure == PlacementFailure::collision,
            "Occupancy change reports a collision");
    require(player->resources.at("alloy") == 10000 - cost, "Collision rejection spends nothing");
    require(place(4, {start.x, 0, start.y}, 999999).failure == PlacementFailure::invalidBuilder,
            "Missing builders return a result rather than silently failing");
    const auto* building = definitions.archetype("basic_generator");
    glm::vec2 second{};
    bool found = false;
    for (float z = -90; z <= 90 && !found; z += 12)
        for (float x = -90; x <= 90 && !found; x += 12)
            if (glm::distance(glm::vec2{x, z}, start) > 15 &&
                session.terrain()
                    .evaluatePlacement(x, z, *building->footprint, building->placement)
                    .valid()) {
                second = {x, z};
                found = true;
            }
    require(found, "A second legal placement exists");
    player->resources["alloy"] = 0;
    require(place(5, {second.x, 0, second.y}).failure == PlacementFailure::resources,
            "Resource loss between preview and commit returns an explicit failure");
    require(session.world().entities().size() == 2, "Failed retries never create duplicates");
    const auto checksum = session.stateChecksum();
    (void)session.consumePlacementEvents();
    require(checksum == session.stateChecksum(), "UI outcomes are not authoritative state");
    // Replace only this mutable session's surface with a known analytical bump. The public
    // query is const; the owned Terrain itself is not. No production test-only API is needed.
    const_cast<Terrain&>(session.terrain()) = test::levelingFixture(.5F, false, false, 3);
    const glm::vec2 levelingPosition{0, 0};
    require(session.terrain()
                .evaluatePlacement(0, 0, *building->footprint, building->placement)
                .leveling.has_value(),
            "The analytical fixture supplies a repairable placement");
    std::vector<std::pair<glm::vec2, float>> originalSurface;
    for (float z = -8; z <= 8; z += Terrain::spacing)
        for (float x = -8; x <= 8; x += Terrain::spacing) {
            const auto p = levelingPosition + glm::vec2{x, z};
            originalSurface.emplace_back(p, session.terrain().heightAt(p.x, p.y));
        }
    require(place(6, {levelingPosition.x, 0, levelingPosition.y}).failure ==
                PlacementFailure::resources,
            "A repairable site still rejects before excavation when resources are missing");
    for (const auto& [p, h] : originalSurface)
        require(session.terrain().heightAt(p.x, p.y) == h,
                "Rejected commands do not alter terrain");
    player->resources["alloy"] = 10000;
    const auto leveledBuilding = place(7, {levelingPosition.x, 0, levelingPosition.y});
    require(leveledBuilding.accepted() && session.world().foundations().back().levelsTerrain,
            "Authoritative placement commits the evaluated leveling plan");
    bool changedGround = false;
    for (const auto& [p, h] : originalSurface)
        changedGround |= session.terrain().heightAt(p.x, p.y) < h - 1e-5F;
    require(changedGround, "Accepted placement actually excavates terrain");
    require(session.submit({1, 8, CancelConstructionCommand{leveledBuilding.building}}),
            "Cancellation queues");
    session.advanceTicks();
    for (const auto& [p, h] : originalSurface)
        require(session.terrain().heightAt(p.x, p.y) == h,
                "Cancellation restores the previous surface");
    require(player->resources.at("alloy") == 10000,
            "Leveling cancellation retains the normal refund");
    for (const auto failure : {TerrainPlacementFailure::insufficientSupport,
                               TerrainPlacementFailure::foundationTooDeep,
                               TerrainPlacementFailure::excavationBudget})
        require(std::string_view{placementFailureKey(failure)} != "placement.terrain",
                "Specific support failures retain a distinct UI explanation");
    const_cast<Terrain&>(session.terrain()) = test::levelingFixture(0);
    PlaceBuildingCommand rotated{builder, "construct.alloy_processor", {40, 0, 40}, {builder}, 45};
    TerrainPlacementCache cache;
    const auto planned = session.planBuildingPlacement(1, rotated, &cache);
    require(planned.valid() && planned.footprint.rotationDegrees == 45 &&
                planned.shape.rotationDegrees == 45 && planned.foundation.rotationDegrees == 45,
            "Rotated geometry agrees across shared placement, collision and foundation plans");
    (void)session.planBuildingPlacement(1, rotated, &cache);
    require(cache.evaluations() == 1, "Repeated preview reuses terrain analysis");
    player->resources["alloy"] = 0;
    require(session.planBuildingPlacement(1, rotated, &cache).failure ==
                PlacementFailure::resources,
            "Cached geometry never freezes affordability");
    player->resources["alloy"] = 10000;
    auto* obstruction = session.world().findEntity(accepted.building);
    const auto originalPosition = obstruction->transform.position;
    obstruction->transform.position = rotated.position;
    const auto blocked = session.planBuildingPlacement(1, rotated, &cache);
    require(blocked.failure == PlacementFailure::collision && blocked.blocker == accepted.building,
            "Cached geometry never freezes occupancy and identifies the current blocker");
    obstruction->transform.position = originalPosition;
    require(session.submit({1, 9, rotated}), "Rotated placement queues");
    session.advanceTicks();
    const auto rotatedEvents = session.consumePlacementEvents();
    require(rotatedEvents.size() == 1 && rotatedEvents.front().accepted(),
            "Rotated placement commits");
    const auto* placed = session.world().findEntity(rotatedEvents.front().building);
    require(placed && placed->transform.rotationDegrees.y == 45 &&
                session.world().foundations().back().rotationDegrees == 45,
            "Committed building and foundation retain the clicked yaw");
    std::cout << "Building placement outcomes and rejection conservation passed\n";
    return 0;
}
