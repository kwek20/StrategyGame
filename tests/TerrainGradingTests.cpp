#include "TerrainTiltFixture.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "persistence/SaveGame.hpp"
#include "players/PlayerRegistry.hpp"
#include "terrain/TerrainPlacementCache.hpp"
#include "world/World.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool valid, const char* message) {
    if (!valid)
        throw std::runtime_error(message);
}
} // namespace

int strategyTestMain() {
    using namespace strategy;
    const DefinitionRegistry definitions;
    const auto* building = definitions.archetype("storage_silo");
    const auto shape = *building->footprint;
    const auto profile = building->placement;
    require(profile.levelingMaximumFill == .5F && profile.levelingMaximumAverageEarthwork == .15F,
            "Production buildings use the accepted fill and total earthwork limits");
    auto ground = test::levelingFixture(-.5F, false, false, 3);
    const float original = ground.heightAt(1.5F, 1.5F);
    const auto plan = ground.evaluatePlacement(0, 0, shape, profile);
    std::cout << "Grading depression valid=" << plan.valid() << " cut=" << plan.excavationVolume
              << " fill=" << plan.fillVolume << " depth=" << plan.foundationDepth
              << " tilt=" << plan.fit.slopeDegrees << '\n';
    require(plan.valid() && plan.leveling && plan.fillVolume > 0 && !plan.requiresScaffold(),
            "A shallow dry depression prefers a graded pad over a metal scaffold");
    require(plan.excavationVolume <=
                    plan.footprintArea * profile.levelingMaximumAverageCut + 1e-8 &&
                plan.excavationVolume + plan.fillVolume <=
                    plan.footprintArea * profile.levelingMaximumAverageEarthwork + 1e-8,
            "Both cut and fill count toward the complete feathered earthwork budget");
    require(ground.heightAt(1.5F, 1.5F) == original,
            "Planning never edits the authoritative terrain");
    auto foundation = ground.foundationFromPlacement(77, {0, 0}, shape, profile, plan);
    require(!foundation.requiresSlab && foundation.levelingMaximumFill == .5F,
            "The accepted pad has no raised metal foundation");
    const float predicted = ground.foundationTerrainHeightAt(foundation, {1.5F, 1.5F});
    require(predicted > original + .1F,
            "The preview raises the depression instead of cutting only");
    ground.applyFoundation(foundation);
    require(std::abs(ground.heightAt(1.5F, 1.5F) - predicted) < 1e-5F,
            "Committed fill matches the preview");
    const float accepted = ground.heightAt(1.5F, 1.5F);
    foundation.influence = .4F;
    ground.rebuildFoundations({foundation});
    require(ground.heightAt(1.5F, 1.5F) == accepted,
            "Construction progress does not accumulate fill");
    ground.rebuildFoundations({});
    require(ground.heightAt(1.5F, 1.5F) == original,
            "Cancellation restores the original depression");

    auto smallFill = profile;
    smallFill.levelingMaximumFill = .05F;
    const auto limited = ground.evaluatePlacement(0, 0, shape, smallFill);
    require(!limited.valid() || limited.requiresScaffold(),
            "Insufficient fill depth retains a supported fallback");
    auto bump = test::levelingFixture(.5F, false, false, 3.75F);
    const auto* generator = definitions.archetype("basic_generator");
    auto partialProfile = generator->placement;
    partialProfile.levelingMaximumFill = .05F;
    partialProfile.levelingMaximumAverageCut = .02F;
    partialProfile.levelingMaximumAverageEarthwork = .5F;
    const auto partial = bump.evaluatePlacement(0, 0, *generator->footprint, partialProfile);
    auto ungradedProfile = partialProfile;
    ungradedProfile.levelingMaximumRelief = 0;
    const auto ungraded = bump.evaluatePlacement(0, 0, *generator->footprint, ungradedProfile);
    require(partial.valid() && partial.leveling &&
                partial.foundationDepth < ungraded.foundationDepth - .01F,
            "Later partial grading can replace a taller ungraded scaffold fallback");
    auto tight = profile;
    tight.levelingMaximumAverageEarthwork = .0001F;
    const auto budget = ground.evaluatePlacement(0, 0, shape, tight);
    require(!budget.leveling ||
                budget.excavationVolume + budget.fillVolume <=
                    budget.footprintArea * tight.levelingMaximumAverageEarthwork + 1e-8,
            "A tiny combined budget cannot authorize hidden earthwork");

    auto slope = test::tiltFixture(.25F, .25F);
    const auto* processor = definitions.archetype("alloy_processor");
    const auto angled = slope.evaluatePlacement(0, 0, *processor->footprint, processor->placement);
    require(angled.valid() && angled.fit.slopeDegrees > 1 && angled.fit.slopeDegrees <= 10.001F &&
                !angled.requiresScaffold(),
            "Shallow grading preserves the natural fitted angle without a raised scaffold");

    // Existing footprints protect both cut and fill, including feathered changes.
    auto protectedGround = test::levelingFixture(-.5F, false, false, 3);
    TerrainFootprint protectedShape{FootprintShape::circle, 1, {1, 1}};
    TerrainFoundation protectedBase{3, {1.5F, original, 1.5F}, protectedShape, {0, 0}};
    protectedGround.applyFoundation(protectedBase);
    const auto blockedEdit = protectedGround.evaluatePlacement(0, 0, shape, profile);
    if (blockedEdit.leveling)
        for (float z = 0; z <= 3; z += .375F)
            for (float x = 0; x <= 3; x += .375F)
                if (glm::length(glm::vec2{x - 1.5F, z - 1.5F}) <=
                    1.F + Terrain::spacing * std::sqrt(2.F))
                    require(std::abs(protectedGround.foundationTerrainHeightAt(
                                         *blockedEdit.leveling, {x, z}) -
                                     protectedGround.heightAt(x, z)) < 1e-5F,
                            "Grading cannot alter another building's protected footing");
    auto wet = test::levelingFixture(-.5F, true, false, 3);
    require(!wet.evaluatePlacement(0, 0, shape, profile).leveling,
            "Grading does not fill water or rewrite a shoreline");

    // Overlapping feather regions replay deterministically and roll back independently.
    auto first = test::levelingFixture(0), second = test::levelingFixture(0);
    TerrainFootprint square{FootprintShape::rectangle, 3, {3, 3}};
    TerrainFoundation cut{10, {-2, 15.8F, 0}, square, {0, 0}};
    cut.levelsTerrain = true;
    TerrainFoundation fill{20, {2, 16.2F, 0}, square, {0, 0}};
    fill.levelsTerrain = true;
    fill.levelingMaximumFill = .5F;
    first.rebuildFoundations({cut, fill});
    second.rebuildFoundations({fill, cut});
    for (float z = -6; z <= 6; z += .375F)
        for (float x = -8; x <= 8; x += .375F)
            require(first.heightAt(x, z) == second.heightAt(x, z),
                    "Overlapping fill/cut order cannot change the final raster");
    first.rebuildFoundations({cut});
    second.rebuildFoundations({cut});
    require(first.heightAt(2, 0) == second.heightAt(2, 0),
            "Removing fill preserves the remaining cut");

    // The accepted operation is sufficient to reconstruct the exact surface after save/load.
    const auto path =
        std::filesystem::temp_directory_path() /
        ("strategy-grading-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    World world;
    PlayerRegistry players{"spain", "japan"};
    world.foundations() = {foundation};
    SaveGame::write(path, 7, world, &players, 10, "open_plains", TerrainMode::v2);
    const auto saved = SaveGame::read(path);
    require(saved.foundations.size() == 1 && saved.foundations[0].levelingMaximumFill == .5F,
            "Save/load retains the accepted fill limit");
    auto restored = test::levelingFixture(-.5F, false, false, 3);
    restored.rebuildFoundations(saved.foundations);
    require(restored.heightAt(1.5F, 1.5F) == accepted,
            "Save/load reconstructs the same graded terrain");
    auto tiltedGrade = foundation;
    tiltedGrade.gradient = {.15F, 0}; // About 8.5 degrees, within the shared ten-degree cap.
    world.foundations() = {tiltedGrade};
    SaveGame::write(path, 7, world, &players, 10, "open_plains", TerrainMode::v2);
    require(
        SaveGame::read(path).foundations[0].gradient == tiltedGrade.gradient,
        "Graded saves accept the shared ten-degree tilt limit rather than the old five-degree cap");
    TerrainPlacementCache cache;
    cache.evaluate(ground, {0, 0}, shape, profile);
    ground.applyFoundation(foundation);
    cache.evaluate(ground, {0, 0}, shape, profile);
    require(cache.evaluations() == 2, "Accepted fill invalidates placement geometry caches");
    std::filesystem::remove(path);
    return 0;
}
