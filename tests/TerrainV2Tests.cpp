#include "TerrainLevelingFixture.hpp"
#include "TerrainTiltFixture.hpp"
#include "persistence/MatchSetupProfile.hpp"
#include "persistence/SaveGame.hpp"
#include "players/PlayerRegistry.hpp"
#include "terrain/FoundationOrientation.hpp"
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

    {
        const auto definitions = TerrainGenerationDefinitions::load();
        const auto recipes = TerrainV2Recipes::load();
        const auto lakeRecipe =
            recipes.resolve(definitions.layout(TerrainLayoutId{"central_water"}));
        std::vector<glm::vec2> firstStarts;
        std::size_t smallestLake = 4096, largestLake = 0;
        for (std::uint32_t seed = 0; seed < 12; ++seed) {
            const auto plan = TerrainV2Plan::create(seed, lakeRecipe, 240, 2);
            const auto repeat = TerrainV2Plan::create(seed, lakeRecipe, 240, 2);
            require(plan.openings() == repeat.openings(), "Start selection is repeatable");
            require(glm::distance(plan.openings()[0], plan.openings()[1]) >= 144,
                    "Random start locations retain separation");
            for (auto opening : plan.openings())
                require(plan.waterDistance(opening) > lakeRecipe.openingRadius,
                        "Opening construction cores do not intersect the basin");
            if (seed == 0)
                firstStarts = plan.openings();
            if (seed == 11)
                require(plan.openings() != firstStarts,
                        "Seeds change authoritative start positions");
            std::size_t wet = 0;
            for (int z = 0; z < 64; ++z)
                for (int x = 0; x < 64; ++x) {
                    const glm::vec2 point{-120 + (x + 0.5F) * 3.75F, -120 + (z + 0.5F) * 3.75F};
                    wet += plan.waterDistance(point) < 0;
                    require(plan.waterDistance(point) == repeat.waterDistance(point) &&
                                plan.peakInfluence(point) == repeat.peakInfluence(point),
                            "Lake and landform composition repeat exactly");
                }
            require(wet > 409 && wet < 1229,
                    "Small central basin preserves its broad land surround");
            smallestLake = std::min(smallestLake, wet);
            largestLake = std::max(largestLake, wet);
        }
        require(largestLake > smallestLake + 100,
                "Small-map lake area varies rather than clamping to one radius");
        auto dryRecipe = recipes.resolve(definitions.layout(TerrainLayoutId{"rolling_hills"}));
        const auto small = TerrainV2Plan::create(7, dryRecipe, 240, 2);
        const auto other = TerrainV2Plan::create(12345, dryRecipe, 240, 2);
        bool differentRelief = false;
        for (int z = -100; z <= 100; z += 10)
            for (int x = -100; x <= 100; x += 10)
                differentRelief |=
                    std::abs(small.peakInfluence({x, z}) - other.peakInfluence({x, z})) > 0.1F;
        require(differentRelief, "Seeds change landform placement and shape beyond cosmetic noise");
    }

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
    {
        const TerrainRidge ridge{{0, 0}, {1, 0}, 60, 14, 1, 0.2F, 0, 1};
        require(ridge.influence({0, 4.2F}) > 0.95F && ridge.influence({0, 7.7F}) == 0,
                "Escarpments drop sharply beside a sustained crest");
        require(ridge.influence({0, -7.2F}) > 0.4F,
                "The opposite shoulder climbs gradually from the valley");
        const float bentCrest = std::cos(0.6F * std::numbers::pi_v<float> * 0.5F) * 2.8F;
        require(ridge.influence({18, bentCrest}) > 0.7F && ridge.influence({31, 0}) == 0,
                "Long crests remain elevated between local peaks and end at bounded tips");
    }
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
    require(supported.valid(), "A connected majority of the upper cliff shelf is supported");
    require(supportTerrain.evaluatePlacement(-.375F, 0, rectangle, {}).valid(),
            "Exactly half connected dry support is allowed");
    require(!supportTerrain.evaluatePlacement(-.25F, 0, rectangle, {}).valid(),
            "Less than half upper-shelf support cannot borrow the disconnected lower shelf");
    require(std::abs(supported.footprintArea - 36) < 1e-8, "Exact projected rectangle area");
    TerrainFootprint circle{FootprintShape::circle, 3, {3, 3}};
    const auto roundSupport = supportTerrain.evaluatePlacement(-12, 0, circle, {});
    require(roundSupport.valid() &&
                std::abs(roundSupport.supportedArea - 9 * std::numbers::pi) < 1e-5,
            "Analytic circle support covers its actual footprint");
    require(supportTerrain.evaluatePlacement(-.375F, 0, circle, {}).valid(),
            "Exactly half analytic disk support is allowed");
    require(!supportTerrain.evaluatePlacement(-.25F, 0, circle, {}).valid(),
            "Less than half analytic disk support is rejected");
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

    // Slightly uneven natural ground used to report zero support because no triangle was
    // mathematically horizontal at the highest point of the footprint.
    auto gentleRecipe = dryRecipe;
    gentleRecipe.cells.elevationStep = .1F;
    std::vector<TerrainCellSource> gentleSource(1600);
    for (int z = 0; z < 40; ++z)
        for (int x = 0; x < 40; ++x) {
            const float h = x < 20 ? 16.0F : 16.1F;
            gentleSource[z * 40 + x] = {h, h, h, {.02F, 0}, TerrainCellRole::route};
        }
    Terrain gentleGround{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(
        gentleGround,
        7,
        definitions.layout(gentleRecipe.layout),
        definitions,
        {TerrainV2Plan::create(7, gentleRecipe, 240, 2),
         TerrainTerraces::resolve(gentleSource, 40, 240, gentleRecipe.cells)});
    auto strictProfile = TerrainPlacementProfile{};
    strictProfile.supportHeightTolerance = 0;
    const auto strictGround = gentleGround.evaluatePlacement(0, 0, rectangle, strictProfile);
    require(!strictGround.valid() && strictGround.supportedArea < .01,
            "Strict horizontal support reproduces the shallow-ground rejection");
    const auto gentleFit = gentleGround.evaluatePlacement(0, 0, rectangle, {});
    require(gentleFit.valid() && gentleFit.foundationDepth > .09F &&
                gentleFit.foundationDepth < .11F &&
                gentleFit.supportedArea > gentleFit.footprintArea - .01,
            "A 0.1-unit gentle rise supports the footprint through a shallow base");
    strictProfile.supportHeightTolerance = .02F;
    const auto narrowBand = gentleGround.evaluatePlacement(0, 0, rectangle, strictProfile);
    require(narrowBand.valid() && narrowBand.groundContactArea < gentleFit.groundContactArea &&
                narrowBand.supportedArea > narrowBand.footprintArea - .01,
            "A narrower floor-contact band does not discard continuous scaffold bearing");
    const float gentleBefore = gentleGround.heightAt(0, 0);
    const auto gentleBase = gentleGround.evaluateFoundation(15, 0, 0, rectangle);
    require(!gentleBase.requiresSlab, "An ordinary base conceals shallow tenth-unit relief without metal supports");
    gentleGround.applyFoundation(gentleBase);
    require(gentleGround.heightAt(0, 0) == gentleBefore,
            "Accommodating gentle relief does not flatten the terrain");

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
    require(!wetSupport.permitsMovementSegment(
                {12, -4.1F}, {12, 4.1F}, .65F, movementDomainBit(MovementDomain::land)),
            "The water-building regression site cannot provide a land-worker movement segment");
    TerrainPlacementProfile floating;
    floating.support = TerrainBuildingSupport::floating;
    require(
        wetSupport.evaluatePlacement(12, 0, rectangle, floating).valid(),
        "Water buildings accept fully wet tiles without requiring land-worker perimeter access");
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
    const auto anchors = fallbackOtherSeed.v2Landscape()->plan.openings();
    fallbackOtherSeed.repairV2Resources(12345, TerrainLayoutId{"central_water"}, 2);
    require(fallbackOtherSeed.v2Landscape()->plan.openings() == anchors,
            "Constructive fallback preserves the requested seed's opening locations");

    {
        auto sloped = test::tiltFixture();
        auto tilt = test::tiltProfile();
        TerrainFootprint shape{FootprintShape::rectangle, 5, {5, 4}};
        for (bool disk : {false, true})
            for (float rotation : {0.F, 37.F, 90.F}) {
                shape.shape = disk ? FootprintShape::circle : FootprintShape::rectangle;
                shape.rotationDegrees = rotation;
                const auto placement = sloped.evaluatePlacement(0, 0, shape, tilt);
                std::cout << "Tilt fit circle=" << disk << " yaw=" << rotation
                          << " angle=" << placement.fit.slopeDegrees
                          << " support=" << placement.supportedArea / placement.footprintArea
                          << '\n';
                require(placement.valid() &&
                            placement.strategy == TerrainPlacementResult::Strategy::fit &&
                            !placement.leveling && placement.fit.slopeDegrees > 4 &&
                            placement.fit.slopeDegrees <= 5.001F,
                        "Tilt-only is chosen before excavation or scaffolding on a gentle ramp");
                const auto fitted = sloped.evaluateFoundation(1, 0, 0, shape, tilt);
                require(
                    fitted.gradient == placement.fit.gradient && !fitted.requiresSlab,
                    "Accepted foundation retains the fitted slope without unnecessary supports");
                const auto basis = foundationOrientation(fitted.gradient, rotation);
                require(std::abs(glm::dot(glm::vec3{basis[0]}, glm::vec3{basis[1]})) < 1e-6F &&
                            std::abs(glm::dot(glm::vec3{basis[2]}, glm::vec3{basis[1]})) < 1e-6F,
                        "Rotated buildings have a rigid basis exactly aligned to the plane");
            }
    }
    {
        auto steep = test::tiltFixture(.75F);
        TerrainFootprint shape{FootprintShape::rectangle, 5, {5, 4}};
        shape.maximumTiltDegrees = 10;
        const auto fitted = steep.evaluatePlacement(0, 0, shape, test::tiltProfile());
        std::cout << "Tilt + cut angle=" << fitted.fit.slopeDegrees
                  << " volume=" << fitted.excavationVolume << " valid=" << fitted.valid()
                  << " strategy=" << static_cast<int>(fitted.strategy) << '\n';
        require(fitted.valid() && fitted.strategy == TerrainPlacementResult::Strategy::scaffold &&
                    fitted.leveling && fitted.fit.slopeDegrees > 4.99F &&
                    fitted.fit.slopeDegrees <= 5.001F,
                "A capped ramp retains bounded excavation and physical foundation support");
        require(fitted.leveling->gradient == fitted.fit.gradient,
                "Excavation follows the angled plane instead of flattening it horizontally");
    }
    {
        auto combined = test::tiltFixture(.25F, .25F, true);
        TerrainFootprint shape{FootprintShape::rectangle, 8, {8, 8}};
        shape.maximumTiltDegrees = 10;
        const auto fitted = combined.evaluatePlacement(-6, 0, shape, test::tiltProfile());
        std::cout << "Tilt + cut + scaffold angle=" << fitted.fit.slopeDegrees
                  << " cut=" << bool(fitted.leveling) << " valid=" << fitted.valid()
                  << " support=" << fitted.supportedArea / fitted.footprintArea << '\n';
        require(fitted.valid() &&
                    fitted.strategy == TerrainPlacementResult::Strategy::scaffold &&
                    fitted.fit.slopeDegrees > 0 && fitted.fit.slopeDegrees <= 5.001F,
                "A sloped cliff shelf uses a tilted scaffold without forcing unnecessary cuts");
        require(combined.evaluateFoundation(1, -6, 0, shape, test::tiltProfile()).requiresSlab,
                "The final tier retains actual cliff scaffolding");
        auto excessive = test::tiltFixture(.5F, .5F, true);
        auto shallow = test::tiltProfile();
        shallow.maximumFoundationDepth = 2;
        require(!excessive.evaluatePlacement(-6, 0, shape, shallow).valid(),
                "A scaffold cannot bypass the maximum foundation depth");
    }

    {
        auto diagonal = test::tiltFixture(.5F, 0, false, true);
        TerrainFootprint shape{FootprintShape::rectangle, 3, {3, 3}};
        shape.maximumTiltDegrees = 10;
        const auto placement = diagonal.evaluatePlacement(0, 0, shape, test::tiltProfile());
        require(placement.fit.slopeDegrees <= 5.001F && placement.fit.slopeDegrees > 4.99F,
                "The combined two-axis tilt is capped at five degrees");
    }

    auto bump = test::levelingFixture();
    auto levelingProfile = test::levelingProfile();
    const TerrainFootprint levelingFootprint{FootprintShape::rectangle, 8, {8, 8}};
    const auto unchangedBump = bump.heightAt(3, 3);
    const auto unadjusted = bump.evaluatePlacement(0, 0, levelingFootprint, {});
    const auto leveled = bump.evaluatePlacement(0, 0, levelingFootprint, levelingProfile);
    std::cout << "Leveling fixture: old support="
              << unadjusted.supportedArea / unadjusted.footprintArea
              << " plan=" << bool(leveled.leveling) << " cut=" << leveled.excavationVolume
              << " plane=" << leveled.fit.height << '\n';
    require(
        unadjusted.valid() && leveled.valid() && leveled.leveling,
        "A bounded bump repair is preferred over raising the entire scaffold to its peak");
    require(bump.heightAt(3, 3) == unchangedBump,
            "Planning and preview evaluation never change authoritative terrain");
    require(leveled.excavationVolume <= leveled.footprintArea * .1 &&
                leveled.fit.height < unadjusted.fit.height - .25,
            "The dominant ground sets the floor and the complete cut obeys its volume budget");
    auto levelingFoundation = bump.evaluateFoundation(77, 0, 0, levelingFootprint, levelingProfile);
    levelingFoundation.influence = 0;
    bump.applyFoundation(levelingFoundation);
    require(bump.heightAt(3, 3) < unchangedBump - .25 &&
                bump.entityBaseHeight(77, 0, 0) == leveled.fit.height,
            "Successful placement cuts ground immediately, even at zero construction progress");
    const float cutHeight = bump.heightAt(3, 3);
    const auto rebuilds = bump.foundationDiagnostics().rebuildCount;
    levelingFoundation.influence = .5F;
    bump.rebuildFoundations({levelingFoundation});
    require(bump.heightAt(3, 3) == cutHeight &&
                bump.foundationDiagnostics().rebuildCount == rebuilds,
            "Construction progress never accumulates excavation or rebuilds the terrain");
    bump.rebuildFoundations({});
    require(bump.heightAt(3, 3) == unchangedBump,
            "Removing the foundation restores its original terrain");
    auto tight = levelingProfile;
    tight.levelingMaximumCut = .1F;
    require(!bump.evaluatePlacement(0, 0, levelingFootprint, tight).leveling,
            "Maximum point excavation is a hard limit");
    tight = levelingProfile;
    tight.levelingMaximumAverageCut = .0001F;
    require(!bump.evaluatePlacement(0, 0, levelingFootprint, tight).leveling,
            "Average excavation budget blocks excessive total earthwork");
    auto tallBump = test::levelingFixture(1.0F);
    require(!tallBump.evaluatePlacement(0, 0, levelingFootprint, levelingProfile).leveling,
            "Relief beyond the shallow band cannot be repaired");
    auto cliffBump = test::levelingFixture(1.0F, false, true);
    require(!cliffBump.evaluatePlacement(0, 0, levelingFootprint, levelingProfile).leveling,
            "Cliffs beyond the shallow relief allowance are preserved");
    auto smallStep = test::levelingFixture(.5F, false, true);
    require(
        smallStep.evaluatePlacement(0, 0, levelingFootprint, levelingProfile).leveling.has_value(),
        "A half-unit bump is repairable even when the generator labels its edge a cliff");
    auto wetBump = test::levelingFixture(.5F, true);
    const auto waterBefore = wetBump.waterSurfaceAt(0, 0);
    require(!wetBump.evaluatePlacement(0, 0, levelingFootprint, levelingProfile).leveling &&
                wetBump.waterSurfaceAt(0, 0) == waterBefore,
            "Water and shoreline footprints never use dry-ground excavation");
    for (bool disk : {false, true}) {
        auto shape = levelingFootprint;
        shape.shape = disk ? FootprintShape::circle : FootprintShape::rectangle;
        if (disk)
            shape.radius = 10;
        shape.rotationDegrees = 37;
        require(bump.evaluatePlacement(0, 0, shape, levelingProfile).leveling.has_value(),
                "Circle and rotated rectangle plans use clipped area weights");
    }
    TerrainFoundation neighbor{
        88, {3, 16.5F, 3}, TerrainFootprint{FootprintShape::circle, .5F}, {0, 0}};
    bump.applyFoundation(neighbor);
    require(!bump.evaluatePlacement(0, 0, levelingFootprint, levelingProfile).leveling,
            "A proposed cut cannot undermine an existing building or its support perimeter");
    bump.rebuildFoundations({});

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
    world.foundations().push_back(levelingFoundation);
    SaveGame::write(savePath, 7, world, &players, 10, "open_plains", TerrainMode::v2);
    auto restored = SaveGame::read(savePath);
    require(restored.foundations.size() == 1 && restored.foundations[0].levelsTerrain &&
                restored.foundations[0].levelingMaximumCut == .6F,
            "Save data retains the accepted cut independently of construction progress");
    auto restoredTerrain = test::levelingFixture();
    restoredTerrain.rebuildFoundations(restored.foundations);
    require(restoredTerrain.heightAt(3, 3) == cutHeight,
            "Loaded foundations reconstruct identical terrain");
    world.foundations().clear();
    {
        auto angled = test::tiltFixture(.75F);
        TerrainFootprint shape{FootprintShape::rectangle, 5, {5, 4}};
        shape.maximumTiltDegrees = 10;
        const auto angledFoundation =
            angled.evaluateFoundation(77, 0, 0, shape, test::tiltProfile());
        require(angledFoundation.levelsTerrain && glm::length(angledFoundation.gradient) > 0,
                "Tilted save fixture excavates relative to its plane");
        world.foundations() = {angledFoundation};
        SaveGame::write(savePath, 7, world, &players, 10, "open_plains", TerrainMode::v2);
        const auto saved = SaveGame::read(savePath);
        require(saved.foundations.size() == 1 &&
                    saved.foundations[0].gradient == angledFoundation.gradient,
                "Save/load retains the exact angled excavation plane");
        angled.applyFoundation(angledFoundation);
        auto loadedTerrain = test::tiltFixture(.75F);
        loadedTerrain.rebuildFoundations(saved.foundations);
        require(std::abs(loadedTerrain.heightAt(2, 1) - angled.heightAt(2, 1)) < 1e-6F,
                "Loaded angled excavation reproduces the accepted surface");
    }
    world.foundations().clear();
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
