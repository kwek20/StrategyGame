#include "persistence/MatchSetupProfile.hpp"
#include "persistence/SaveGame.hpp"
#include "players/PlayerRegistry.hpp"
#include "terrain/SurfaceGeometry.hpp"
#include "terrain/Terrain.hpp"
#include "terrain/TerrainGenerationPipeline.hpp"
#include "terrain/TerrainTerraces.hpp"
#include "terrain/TerrainV2Generator.hpp"
#include "world/GenerationProgress.hpp"
#include "world/World.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int strategyTestMain() {
    using namespace strategy;

    require(std::abs(surface_geometry::diskArea({{-4, -4}, {4, -4}, {4, 4}, {-4, 4}}, 3) -
                     9 * std::numbers::pi) < 1e-10,
            "Full disk area is analytic");
    require(std::abs(surface_geometry::diskArea({{0, -4}, {4, -4}, {4, 4}, {0, 4}}, 3) -
                     4.5 * std::numbers::pi) < 1e-10,
            "Half disk intersection is exact");
    require(surface_geometry::segmentContact({-2, 0}, {2, 0}, {1, 1}, true) &&
                !surface_geometry::segmentContact({-2, 1}, {2, 1}, {1, 1}, true) &&
                !surface_geometry::segmentContact({-2, 0}, {0, 2}, {1, 1}, false),
            "Footprint contact requires positive length, not a circle tangent or rectangle corner");
    using surface_geometry::SupportTriangle;
    std::array patches{SupportTriangle{{0, 1, 2}, {true, true, true}, 1},
                       SupportTriangle{{2, 3, 4}, {true, true, true}, 1}};
    require(surface_geometry::largestConnectedArea(patches) == 1,
            "Corner-only triangle contacts cannot combine support areas");
    patches[1].vertices = {2, 1, 3};
    require(surface_geometry::largestConnectedArea(patches) == 2,
            "Positive shared edges connect support triangles");
    patches[1].edgeContact[0] = false;
    require(surface_geometry::largestConnectedArea(patches) == 1,
            "Footprint clipping can remove a mesh edge's support connection");
    const auto definitions = TerrainGenerationDefinitions::load();
    const auto recipes = TerrainV2Recipes::load();
    bool rejectedRange = false;
    try {
        (void)TerrainRange{1, 2}.intersect({3, 4});
    } catch (const std::runtime_error&) {
        rejectedRange = true;
    }
    require(rejectedRange, "Empty range intersections are definition errors");
    require(TerrainRange{1, 3}.intersect({2, 4}).minimum == 2, "Intersect profile/layout ranges");
    TerrainCellSettings settings;
    std::vector<TerrainCellSource> synthetic(81, {16, 16, 16, {0, 0}, TerrainCellRole::ordinary});
    synthetic[40] = {17.05F, 17.05F, 17.05F, {0, 0}, TerrainCellRole::ordinary};
    const auto cleaned = TerrainTerraces::resolve(synthetic, 9, 54, settings);
    require(cleaned.diagnostics().isolatedBefore == 1 && cleaned.diagnostics().isolatedAfter == 0,
            "Remove accidental singleton spikes");
    require(cleaned.cells()[40].source.mean == 17.05F, "Keep source samples immutable");
    for (auto& sample : synthetic)
        sample = {16, 16, 16, {0, 0}, TerrainCellRole::opening};
    for (int z = 0; z < 9; ++z)
        for (int x = 4; x < 9; ++x)
            synthetic[z * 9 + x] = {20, 20, 20, {0, 0}, TerrainCellRole::opening};
    const auto cliff = TerrainTerraces::resolve(synthetic, 9, 54, settings);
    require(cliff.crossesCliff({-6, 0}, {6, 0}), "Owned cliff rejects crossings");
    require(!cliff.crossesCliff({-12, -12}, {-12, 12}), "Flat movement stays legal");
    require(cliff.surfaceAt({-6, 0}) == 16 && cliff.surfaceAt({6, 0}) == 20,
            "Cliffs preserve flat cell cores");
    for (int z = 0; z < 9; ++z)
        for (int x = 0; x < 9; ++x) {
            const float h = (x < 4) == (z < 4) ? 16.0F : 20.0F;
            synthetic[z * 9 + x] = {h, h, h, {0, 0}, TerrainCellRole::opening};
        }
    const auto junction = TerrainTerraces::resolve(synthetic, 9, 54, settings);
    require(junction.surfaceAt({-3, -3}) == 16,
            "Alternating corner has no invented intermediate landing");
    require(junction.crossesCliff({-6, -6}, {0, 0}),
            "Alternating corners forbid diagonal shortcuts");
    auto zeroWater = recipes.resolve(definitions.layout(TerrainLayoutId{"open_plains"}));
    const auto directDry =
        TerrainV2Generator::generate(7, zeroWater, definitions.activeGenerator(), 240, 2);
    zeroWater.waterPolicy = TerrainWaterPolicy::optional;
    zeroWater.waterFeature = TerrainWaterFeature::centralLake;
    zeroWater.waterCoverage = {0, 0};
    zeroWater.waterLevel = -100;
    zeroWater.lakeDepth = -100;
    const auto optionalDry =
        TerrainV2Generator::generate(7, zeroWater, definitions.activeGenerator(), 240, 2);
    require(!optionalDry.plan.hasWater(),
            "Optional zero water resolves to the independent dry path");
    for (std::size_t i = 0; i < directDry.terraces.cells().size(); ++i)
        require(directDry.terraces.cells()[i].elevation ==
                    optionalDry.terraces.cells()[i].elevation,
                "Optional-zero water cannot shape dry terrain");
    WorldGenerationProgress cancelled;
    cancelled.requestCancellation();
    bool cancelledCorrectly = false;
    try {
        (void)TerrainV2Generator::generate(1,
                                           recipes.resolve(definitions.activeLayout()),
                                           definitions.activeGenerator(),
                                           240,
                                           2,
                                           &cancelled);
    } catch (const WorldGenerationCancelled&) {
        cancelledCorrectly = true;
    }
    require(cancelledCorrectly, "Generation honors cancellation before publication");
    Terrain central{7, TerrainLayoutId{"central_water"}, TerrainMode::v2, 10};
    require(central.waterCoverageAt(0, 0) > 0.99F, "Central lake is visible in runtime fields");
    require(central.waterCoverageAt(90, 90) == 0, "Opening land remains dry");

    auto dryRecipe = recipes.resolve(definitions.layout(TerrainLayoutId{"open_plains"}));
    // Keep this analytical cliff fixture independent of authored production granularity.
    dryRecipe.cells = TerrainCellSettings{};
    auto supportPlan = TerrainV2Plan::create(7, dryRecipe, 240, 2);
    std::vector<TerrainCellSource> supportSource(1600);
    for (int z = 0; z < 40; ++z)
        for (int x = 0; x < 40; ++x) {
            const float h = x < 20 ? 16.0F : 12.0F;
            supportSource[z * 40 + x] = {h, h, h, {0, 0}, TerrainCellRole::opening};
        }
    Terrain supportTerrain{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(
        supportTerrain,
        7,
        definitions.layout(dryRecipe.layout),
        definitions,
        {supportPlan, TerrainTerraces::resolve(supportSource, 40, 240, dryRecipe.cells)});
    TerrainFootprint rectangle{FootprintShape::rectangle, 3, {3, 3}};
    const auto supported = supportTerrain.evaluatePlacement(-1.5F, 0, rectangle, {});
    require(supported.valid(), "Less than one third cliff overhang is supported");
    require(!supportTerrain.evaluatePlacement(-1.375F, 0, rectangle, {}).valid(),
            "Exactly one third unsupported is rejected");
    require(!supportTerrain.evaluatePlacement(-1.25F, 0, rectangle, {}).valid(),
            "More than one third unsupported is rejected");
    require(std::abs(supported.footprintArea - 36) < 1e-8, "Exact projected rectangle area");
    TerrainFootprint circle{FootprintShape::circle, 3, {3, 3}};
    const auto roundSupport = supportTerrain.evaluatePlacement(-12, 0, circle, {});
    require(roundSupport.valid() &&
                std::abs(roundSupport.supportedArea - 9 * std::numbers::pi) < 1e-5,
            "Analytic circle support covers its actual footprint");
    auto shallowProfile = TerrainPlacementProfile{};
    shallowProfile.maximumFoundationDepth = 3.99F;
    require(!supportTerrain.evaluatePlacement(-1.5F, 0, rectangle, shallowProfile).valid(),
            "Foundation depth bound is enforced");
    shallowProfile.maximumFoundationDepth = 4;
    require(supportTerrain.evaluatePlacement(-1.5F, 0, rectangle, shallowProfile).valid(),
            "Foundation depth equality is allowed");
    const float before = supportTerrain.heightAt(0, 0);
    const auto foundation = supportTerrain.evaluateFoundation(1, -1.5F, 0, rectangle);
    supportTerrain.applyFoundation(foundation);
    supportTerrain.rebuildFoundations({foundation});
    require(supportTerrain.heightAt(0, 0) == before, "V2 foundations never mutate terrain");
    require(supportTerrain.entityBaseHeight(1, -1.5F, 0) == foundation.center.y &&
                supportTerrain.entityBaseHeight(1, 12, 0) == supportTerrain.heightAt(12, 0),
            "Foundation planes only apply to their actual entity location");

    // Resource fallback on a dry recipe must not consult inactive water dimensions.
    auto inactiveRecipe = dryRecipe;
    inactiveRecipe.bankWidth = std::numeric_limits<float>::quiet_NaN();
    inactiveRecipe.lakeDepth = std::numeric_limits<float>::quiet_NaN();
    Terrain inactiveWater{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(
        inactiveWater,
        7,
        definitions.layout(dryRecipe.layout),
        definitions,
        {TerrainV2Plan::create(7, inactiveRecipe, 240, 2),
         TerrainTerraces::resolve(supportSource, 40, 240, dryRecipe.cells)});
    inactiveWater.repairV2Resources(7, dryRecipe.layout, 2);
    require(inactiveWater.heightAt(0, 0) == dryRecipe.baseHeight &&
                inactiveWater.waterCoverageAt(0, 0) == 0,
            "Dry resource fallback ignores inactive water settings");

    auto lakePlan = TerrainV2Plan::create(
        7, recipes.resolve(definitions.layout(TerrainLayoutId{"central_water"})), 240, 2);
    auto fixtureLakeRecipe = lakePlan.recipe();
    fixtureLakeRecipe.cells = dryRecipe.cells;
    lakePlan = TerrainV2Plan::create(7, fixtureLakeRecipe, 240, 2);
    Terrain wetSupport{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(
        wetSupport,
        7,
        definitions.layout(TerrainLayoutId{"central_water"}),
        definitions,
        {lakePlan, TerrainTerraces::resolve(supportSource, 40, 240, dryRecipe.cells)});
    require(wetSupport.evaluatePlacement(-3.1F, 0, rectangle, {}).valid(),
            "Land placement allows partially wet support tiles below one third combined overhang");
    TerrainFootprint large{FootprintShape::rectangle, 10, {10, 3}};
    require(!wetSupport.evaluatePlacement(-7, 0, large, {}).valid(),
            "A fully wet tile rejects Land even when unsupported fraction is small");
    TerrainPlacementProfile floating;
    floating.support = TerrainBuildingSupport::floating;
    floating.interactionDomains = movementDomainBit(MovementDomain::water);
    require(wetSupport.evaluatePlacement(12, 0, rectangle, floating).valid(),
            "Water buildings accept fully wet tiles");
    require(!wetSupport.evaluatePlacement(-3, 0, rectangle, floating).valid(),
            "Water buildings reject partial/dry tiles");
    require(wetSupport.evaluatePlacement(12, 0, rectangle, floating).fit.height == 15.25F,
            "Water clearance sets the building plane");

    TerrainPlacementProfile shore;
    shore.support = TerrainBuildingSupport::shore;
    require(!wetSupport.evaluatePlacement(0, 0, rectangle, shore).valid(),
            "Shore never invents a mixed-water contract");
    shore.shore = TerrainShoreSupport{0.2F, 0.8F, 0.4F};
    require(wetSupport.evaluatePlacement(0, 0, rectangle, shore).valid(),
            "Authored Shore contract accepts adequate connected dry support and mixed water");
    shore.shore->minimumConnectedDrySupportFraction = 0.5F;
    require(!wetSupport.evaluatePlacement(0, 0, rectangle, shore).valid(),
            "Shore enforces its own connected support requirement");
    shore.shore->minimumConnectedDrySupportFraction = 0.4F;
    require(!wetSupport.evaluatePlacement(12, 0, rectangle, shore).valid(),
            "Shore contract rejects fully wet placement");
    central.repairV2Resources(7, TerrainLayoutId{"central_water"}, 2);
    require(central.repairLevel() == 2 && central.waterCoverageAt(0, 0) > 0.99F,
            "Constructive repair preserves the active lake and records its state");
    Terrain fallbackOtherSeed{12345, TerrainLayoutId{"central_water"}, TerrainMode::v2, 10};
    fallbackOtherSeed.repairV2Resources(12345, TerrainLayoutId{"central_water"}, 2);
    require(fallbackOtherSeed.fingerprint() == central.fingerprint(),
            "Constructive fallback eligibility and geometry are independent of the failed seed");

    require(terrainModeAvailable(TerrainMode::v1), "V1 remains playable");
    require(terrainModeAvailable(TerrainMode::v2), "The new V2 is independently available");
    const auto directory = std::filesystem::temp_directory_path() / "strategy_v2_generation_tests";
    std::filesystem::create_directories(directory);
    const auto profilePath = directory / "setup.json";
    MatchSetupProfile profile;
    profile.terrainMode = TerrainMode::v2;
    MatchSetupProfileStore::write(profilePath, profile);
    require(MatchSetupProfileStore::load(profilePath).terrainMode == TerrainMode::v2,
            "The V2 setup selection remains persisted");
    profile.terrainMode = TerrainMode::v1;
    MatchSetupProfileStore::write(profilePath, profile);
    require(MatchSetupProfileStore::load(profilePath).terrainMode == TerrainMode::v1,
            "Setup can switch back to V1");
    const auto savePath = directory / "save.json";
    World world;
    PlayerRegistry players{"spain", "japan"};
    SaveGame::write(savePath, 123, world, &players);
    require(SaveGame::read(savePath).terrainSeed == 123, "Legacy V1 save still loads");
    std::ifstream input(savePath);
    const std::string original{std::istreambuf_iterator<char>(input), {}};
    input.close();
    require(!original.empty() && original.front() == '{', "Valid generated save fixture");
    for (const auto mode : {"v1", "v2"}) {
        {
            std::ofstream output(savePath);
            output << "{\"terrainGeneration\":\"" << mode << "\"," << original.substr(1);
        }
        bool rejected = false;
        try {
            (void)SaveGame::read(savePath);
        } catch (const std::runtime_error& error) {
            rejected =
                std::string(error.what()).find("obsolete V2 terrain recipe") != std::string::npos;
            if (!rejected)
                throw;
        }
        require(rejected == (std::string_view{mode} == "v2"),
                "Prototype V2 saves reject while tagged V1 saves load");
    }
    std::filesystem::remove(profilePath);
    std::filesystem::remove(savePath);
    std::filesystem::remove(directory);
    std::cout << "V2 generation, support and save checks passed\n";
    return 0;
}
