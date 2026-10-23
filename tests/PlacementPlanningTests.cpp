#include "TerrainTiltFixture.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "persistence/SaveGame.hpp"
#include "players/PlayerRegistry.hpp"
#include "simulation/CommandCodec.hpp"
#include "terrain/SurfaceGeometry.hpp"
#include "terrain/TerrainPlacementCache.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
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
    const auto* processor = definitions.archetype("alloy_processor");
    const auto* generator = definitions.archetype("basic_generator");
    auto shelf = test::tiltFixture(.25F, .25F, true);
    const auto alternate =
        shelf.evaluatePlacement(-6, 0, *processor->footprint, processor->placement);
    std::cout << "Alternative plane valid=" << alternate.valid()
              << " tilt=" << alternate.fit.slopeDegrees
              << " support=" << alternate.supportedArea / alternate.footprintArea
              << " cut=" << alternate.excavationVolume << '\n';
    require(alternate.valid(),
            "A feasible shelf placement must not be rejected by one fitted direction");
    require(alternate.fit.slopeDegrees <= 5.001F &&
                alternate.excavationVolume <=
                    alternate.footprintArea * processor->placement.levelingMaximumAverageCut + 1e-8,
            "Alternative directions obey unchanged tilt and excavation budgets");
    auto bump = test::levelingFixture(.5F, false, false, 3.75F);
    const auto raised = bump.evaluatePlacement(0, 0, *generator->footprint, generator->placement);
    std::cout << "Repair intercept valid=" << raised.valid()
              << " support=" << raised.supportedArea / raised.footprintArea
              << " cut=" << raised.excavationVolume << '\n';
    require(raised.valid(), "A feasible higher repair intercept must be found");
    require(raised.excavationVolume <=
                raised.footprintArea * generator->placement.levelingMaximumAverageCut + 1e-8,
            "Intercept search preserves the complete feathered cut budget");

    // A tiny isolated high point used to bypass repairs entirely because its relief
    // was inside the support tolerance, leaving the entire floor at the maximum.
    auto smallBump = test::levelingFixture(.5F, false, false, 3.75F);
    auto tolerant = generator->placement;
    tolerant.supportHeightTolerance = 1.F;
    const auto averagePlan = smallBump.evaluatePlacement(0, 0, *generator->footprint, tolerant);
    auto noRepair = tolerant;
    noRepair.levelingMaximumRelief = 0;
    const auto maximumPlan = smallBump.evaluatePlacement(0, 0, *generator->footprint, noRepair);
    std::cout << "Average plane=" << averagePlan.fit.height
              << " maximum plane=" << maximumPlan.fit.height << '\n';
    require(averagePlan.valid() && averagePlan.leveling &&
                averagePlan.fit.height < maximumPlan.fit.height - .01F,
            "Small bumps must not silently raise the whole floor to the highest point");
    auto slope = test::tiltFixture(.25F);
    const auto angled = slope.evaluatePlacement(0, 0, *processor->footprint, processor->placement);
    std::cout << "Gentle slope angle=" << angled.fit.slopeDegrees << '\n';
    require(angled.valid() && angled.fit.slopeDegrees > .5F && angled.fit.slopeDegrees <= 5.001F,
            "Gentle overall slopes must produce a visible fitted angle before excavation");

    require(processor->placement.maximumBuildingTiltDegrees == 10.F,
            "The live building catalogue permits ten-degree placement");
    auto steeperSlope = test::tiltFixture(1.F);
    const auto steeper =
        steeperSlope.evaluatePlacement(0, 0, *processor->footprint, processor->placement);
    std::cout << "Raised-limit slope angle=" << steeper.fit.slopeDegrees << '\n';
    require(steeper.valid() && steeper.fit.slopeDegrees > 9.F &&
                steeper.fit.slopeDegrees <= 10.001F && !steeper.leveling,
            "A nine-degree slope tilts the building before attempting excavation");
    auto cappedProfile = processor->placement;
    cappedProfile.maximumBuildingTiltDegrees = 30;
    auto cappedShape = *processor->footprint;
    cappedShape.maximumTiltDegrees = 30;
    auto diagonalSlope = test::tiltFixture(.9F, 0, false, true);
    const auto capped = diagonalSlope.evaluatePlacement(0, 0, cappedShape, cappedProfile);
    require(capped.fit.valid && capped.fit.slopeDegrees > 9.99F &&
                capped.fit.slopeDegrees <= 10.001F,
            "The total two-axis tilt retains the hard ten-degree limit");

    // Use the actual generation path: small steep ramp triangles used to suppress all
    // fitted directions even when the building-scale hillside was gentle.
    Terrain natural{1, TerrainLayoutId{"continental"}, TerrainMode::v2, 15};
    const auto hillside =
        natural.evaluatePlacement(-64, -160, *processor->footprint, processor->placement);
    std::cout << "Natural hillside valid=" << hillside.valid()
              << " angle=" << hillside.fit.slopeDegrees
              << " contact=" << hillside.groundContactArea / hillside.footprintArea
              << " bearing=" << hillside.supportedArea / hillside.footprintArea << '\n';
    require(hillside.valid() && hillside.fit.slopeDegrees > 1 &&
                hillside.fit.slopeDegrees <= 10.001F,
            "Natural continuous hillsides must produce legal angled placements");
    auto roughHill = test::tiltFixture(1.75F);
    const auto supportedHill =
        roughHill.evaluatePlacement(0, 0, *processor->footprint, processor->placement);
    std::cout << "Rough hillside valid=" << supportedHill.valid()
              << " angle=" << supportedHill.fit.slopeDegrees
              << " contact=" << supportedHill.groundContactArea / supportedHill.footprintArea
              << " bearing=" << supportedHill.supportedArea / supportedHill.footprintArea
              << " depth=" << supportedHill.foundationDepth
              << " strategy=" << static_cast<int>(supportedHill.strategy) << '\n';
    require(supportedHill.valid() &&
                supportedHill.supportedArea > supportedHill.footprintArea * .99 &&
                supportedHill.groundContactArea < supportedHill.supportedArea &&
                supportedHill.strategy == TerrainPlacementResult::Strategy::scaffold,
            "Connected dry hillside bearing is distinct from contact with the floor");

    auto limitedRepair = generator->placement;
    limitedRepair.levelingMaximumFill = 0;
    limitedRepair.levelingMaximumAverageEarthwork = 0;
    limitedRepair.levelingMaximumAverageCut = .02F;
    const auto lowered = smallBump.evaluatePlacement(0, 0, *generator->footprint, limitedRepair);
    require(lowered.valid() && lowered.leveling &&
                lowered.fit.height < maximumPlan.fit.height - .01F &&
                lowered.excavationVolume <=
                    lowered.footprintArea * limitedRepair.levelingMaximumAverageCut + 1e-8,
            "A legal high scaffold cannot hide a lower repair within the remaining cut budget");

    TerrainPlacementCache cache;
    cache.evaluate(bump, {0, 0}, *generator->footprint, generator->placement);
    cache.evaluate(bump, {0, 0}, *generator->footprint, generator->placement);
    require(cache.evaluations() == 1, "Unchanged pose and surface reuse geometric analysis");
    const auto before = bump.heightAt(20, 20);
    TerrainFoundation protection{500, {20, before, 20}, *generator->footprint, {0, 0}};
    bump.applyFoundation(protection);
    require(bump.heightAt(20, 20) == before, "The protection fixture makes no terrain edit");
    cache.evaluate(bump, {0, 0}, *generator->footprint, generator->placement);
    require(cache.evaluations() == 2,
            "A new foundation invalidates analysis even without height changes");
    protection.influence = .5F;
    bump.rebuildFoundations({protection});
    cache.evaluate(bump, {0, 0}, *generator->footprint, generator->placement);
    require(cache.evaluations() == 2,
            "Construction progress alone does not invalidate V2 analysis");
    bump.rebuildFoundations({});
    cache.evaluate(bump, {0, 0}, *generator->footprint, generator->placement);
    require(cache.evaluations() == 3, "Removing protection invalidates cached analysis");
    auto rotated = *processor->footprint;
    rotated.rotationDegrees = 45;
    cache.evaluate(shelf, {-6, 0}, rotated, processor->placement);
    require(cache.evaluations() == 4, "Shape rotation and terrain replacement invalidate analysis");

    using namespace surface_geometry;
    std::array contacts{SupportTriangle{{0, 1, 2}, {true, true, true}, 1},
                        SupportTriangle{{1, 0, 3}, {true, true, true}, 1}};
    contacts[0].intervals[0] = {0, .4};
    contacts[1].intervals[0] = {.6, 1};
    require(largestConnectedArea(contacts) == 1,
            "Disjoint support intervals on one raster edge stay separate");
    contacts[1].intervals[0] = {.4, 1};
    require(largestConnectedArea(contacts) == 1,
            "Single-point contact cannot connect support islands");
    contacts[1].intervals[0] = {.3, 1};
    require(largestConnectedArea(contacts) == 2, "Positive shared-edge support joins components");

    std::array isolated{SupportTriangle{{0, 1, 2}, {}, 4},
                        SupportTriangle{{3, 4, 5}, {}, 6}};
    isolated[0].contactArea = .1;
    std::array<unsigned char, 2> selected{};
    require(largestConnectedArea(isolated, {}, true, selected) == 4 && selected[0] && !selected[1],
            "A larger disconnected lower shelf cannot replace the floor's anchored ground patch");

    const Polygon square{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const Polygon half{{-1, -1}, {0, -1}, {0, 1}, {-1, 1}};
    PlacementGeometry partition;
    appendPlacementGeometry(partition, square, half);
    double unsupportedArea = 0, supportedArea = 0, diskPartition = 0;
    for (const auto& p : partition.unsupported) {
        unsupportedArea += area(p);
        diskPartition += diskArea(p, 1);
    }
    for (const auto& p : partition.supported) {
        supportedArea += area(p);
        diskPartition += diskArea(p, 1);
    }
    require(std::abs(unsupportedArea - 2) < 1e-9 && std::abs(supportedArea - 2) < 1e-9 &&
                std::abs(diskPartition - std::numbers::pi) < 1e-9,
            "Overlay regions partition both rectangular and circular footprints without overlap");
    PlacementGeometry fullSupport;
    appendPlacementGeometry(fullSupport, square, square);
    require(fullSupport.unsupported.empty() && fullSupport.supported.size() == 1,
            "Fully supported terrain is drawn once");

    const PlayerCommand command{
        1, 99, PlaceBuildingCommand{7, "construct.alloy_processor", {1, 0, 2}, {7, 8}, 45}};
    const auto decoded = CommandCodec::decode(CommandCodec::encode(command));
    require(decoded && std::get<PlaceBuildingCommand>(decoded->payload).rotationDegrees == 45 &&
                std::get<PlaceBuildingCommand>(decoded->payload).builders ==
                    std::vector<EntityId>{7, 8},
            "Placement command serialization retains yaw and builder intent");

    auto level = test::levelingFixture(0);
    World world;
    auto& entity = world.createEntity("Rotated processor", "alloy_processor", 1);
    definitions.initializeEntity(entity);
    entity.transform.rotationDegrees.y = 45;
    const auto placement = level.evaluatePlacement(0, 0, rotated, processor->placement);
    require(placement.valid(), "Rotated save fixture is legal");
    world.foundations().push_back(
        level.foundationFromPlacement(entity.id, {0, 0}, rotated, processor->placement, placement));
    const auto savePath =
        std::filesystem::temp_directory_path() / "strategy_rotated_placement_roundtrip.json";
    PlayerRegistry players;
    SaveGame::write(savePath, 7, world, &players, 10, "open_plains", TerrainMode::v2);
    const auto loaded = SaveGame::read(savePath);
    require(loaded.entities.front().transform.rotationDegrees.y == 45 &&
                loaded.foundations.front().rotationDegrees == 45,
            "Save/load retains building and foundation yaw using existing fields");
    std::filesystem::remove(savePath);

    // Diagnostic timings, not fragile wall-clock assertions.
    for (const auto* type : {generator, processor}) {
        std::vector<double> timings;
        for (int i = 0; i < 15; ++i) {
            const auto started = std::chrono::steady_clock::now();
            (void)shelf.evaluatePlacement(-6 + .03F * i, 0, *type->footprint, type->placement);
            timings.push_back(std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - started)
                                  .count());
        }
        std::sort(timings.begin(), timings.end());
        std::cout << "PLACEMENT_QUERY " << type->id << " median_ms=" << timings[7]
                  << " p95_ms=" << timings[14] << '\n';
    }
    return 0;
}
