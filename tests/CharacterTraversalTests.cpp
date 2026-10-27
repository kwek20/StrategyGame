#include "TerrainTiltFixture.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "world/Navigation.hpp"
#include "world/World.hpp"
#include "simulation/GameSession.hpp"
#include <stdexcept>
#include <chrono>
#include <iostream>
#include <cmath>
#include <limits>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
strategy::Terrain ledge(float height) {
    using namespace strategy;
    const auto definitions = TerrainGenerationDefinitions::load();
    auto recipe = TerrainV2Recipes::load().resolve(definitions.layout(TerrainLayoutId{"open_plains"}));
    recipe.cells = TerrainCellSettings{};
    recipe.cells.elevationStep = height;
    recipe.cells.sourceWeight = 100;
    recipe.cells.isolatedWeight = 0;
    recipe.cells.boundaryWeight = 0;
    std::vector<TerrainCellSource> source(1600);
    for (int z = 0; z < 40; ++z)
        for (int x = 0; x < 40; ++x) {
            const float h = 16 + (x >= 20 ? height : 0);
            source[z * 40 + x] = {h, h, h, {}, TerrainCellRole::ordinary};
        }
    Terrain terrain{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(terrain, 7, definitions.layout(recipe.layout), definitions,
        {TerrainV2Plan::create(7, recipe, 240, 2), TerrainTerraces::resolve(source, 40, 240, recipe.cells)});
    return terrain;
}

strategy::Terrain hillside(float rise, bool diagonal = false,
                            strategy::TerrainGroundMovementSettings movement = {}, bool mound = false) {
    using namespace strategy;
    const auto definitions = TerrainGenerationDefinitions::load();
    auto recipe = TerrainV2Recipes::load().resolve(definitions.layout(TerrainLayoutId{"open_plains"}));
    recipe.cells = TerrainCellSettings{};
    recipe.cells.sourceWeight = 100;
    recipe.cells.isolatedWeight = recipe.cells.boundaryWeight = 0;
    recipe.groundMovement = movement;
    std::vector<TerrainCellSource> source(1600);
    const auto height = [&](int x, int z) {
        if (mound)
            return 40 + rise * std::max(0, 3 - std::abs(x - 20)) *
                        std::max(0.F, 1.F - std::abs(z - 20) / 2.F);
        return 40 + std::clamp(x - 20, -3, 3) * rise +
                    (diagonal ? std::clamp(z - 20, -3, 3) * rise : 0);
    };
    for (int z = 0; z < 40; ++z)
        for (int x = 0; x < 40; ++x) {
            const float h = height(x, z);
            source[z * 40 + x] = {h, h, h,
                {(height(x + 1, z) - height(x - 1, z)) / 12.F,
                 (height(x, z + 1) - height(x, z - 1)) / 12.F}, TerrainCellRole::hill};
        }
    Terrain terrain{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(terrain, 7, definitions.layout(recipe.layout), definitions,
        {TerrainV2Plan::create(7, recipe, 240, 2), TerrainTerraces::resolve(source, 40, 240, recipe.cells)});
    return terrain;
}
}
int strategyTestMain() {
    using namespace strategy;
    constexpr auto land = movementDomainBit(MovementDomain::land);
    TerrainGroundMovementSettings tuning;
    require(tuning.speedScale(0) == 1 && tuning.speedScale(25) == 1 &&
                tuning.speedScale(50) == .75F && tuning.speedScale(89) == .75F,
            "Steep slopes approach a positive speed floor instead of blocking movement");
    auto strong = tuning;
    strong.minimumSlopeSpeedScale = .6F;
    std::cout << "Slope tuning (mild/strong): 35deg=" << tuning.speedScale(35) << '/' << strong.speedScale(35)
              << " 50deg=" << tuning.speedScale(50) << '/' << strong.speedScale(50) << '\n';
    require(tuning.speedScale(35) > strong.speedScale(35), "Mild and strong curves can be compared without changing terrain access");
    for (const auto invalid : {0.F, -1.F, 1.1F, std::numeric_limits<float>::quiet_NaN()}) {
        auto bad = tuning; bad.minimumSlopeSpeedScale = invalid;
        bool rejected = false;
        try { bad.validate(); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Invalid or zero speed floors cannot create hidden barriers");
    }
    auto steep = hillside(8);
    require(steep.walkingSpeedScaleAt(0, 1) < .8F && steep.walkingSpeedScaleAt(0, 1) > 0,
            "A rendered hillside steeper than the old forty-degree limit remains traversable");
    for (const float radius : {.65F, 2.2F})
        require(steep.permitsMovementSegment({-8, 1}, {8, 1}, radius, land) &&
                steep.permitsMovementSegment({8, 1}, {-8, 1}, radius, land),
                "Foot and tank footprints cross the same steep continuous hillside in both directions");
    auto diagonalHill = hillside(8, true);
    require(diagonalHill.permitsMovementSegment({-8, -8}, {8, 8}, 2.2F, land),
            "Diagonal source contours do not become accidental cliff barriers");
    auto gentle = test::tiltFixture(2);
    require(gentle.permitsMovementSegment({-2, 1}, {2, 1}, .65F, land),
            "Character crosses a continuous gentle hillside");
    auto hill = test::tiltFixture(3, 0, false, true);
    const float speed = hill.walkingSpeedScaleAt(0, 1);
    require(speed > .65F && speed < 1, "A 35-degree hill slows smoothly without blocking");
    require(hill.permitsMovementSegment({-2, 1}, {2, 1}, .65F, land),
            "Highland biome does not block a physically walkable hill");
    auto step = ledge(.3F);
    require(step.permitsMovementSegment({-2, 1}, {2, 1}, .65F, land) &&
            step.permitsMovementSegment({2, 1}, {-2, 1}, .65F, land),
            "Small ledges can be crossed in either direction");
    auto cliff = ledge(2);
    require(!cliff.permitsMovementSegment({-2, 1}, {2, 1}, .65F, land),
            "Substantial cliffs remain barriers");
    require(cliff.permitsMovementSegment({-1.04F, 1}, {-1.04F, 3}, .65F, land),
            "A character can walk beside a cliff without the old square and buffer exclusion");
    require(!cliff.permitsMovementSegment({-8, 1}, {8, 1}, 2.2F, land) &&
            !cliff.permitsMovementSegment({-8, -8}, {8, 8}, 2.2F, land),
            "Tank-sized footprints cannot bypass an explicit cliff, including diagonally");
    auto wet = test::levelingFixture(0, true);
    require(!wet.permitsMovementSegment({0, 0}, {1, 0}, .65F, land) &&
                wet.movementSpeedScaleAt(0, 0, land) == 0,
            "Relaxed hills do not permit land units through water");
    DefinitionRegistry definitions;
    World world;
    Navigation steepNavigation(steep, definitions, 10);
    require(!steepNavigation.findPath(steep, world, {-8, 0, 1}, {8, 0, 1}, 2.2F, 0).empty(),
            "RTS navigation accepts the same steep hillside as direct tank control");
    // Refresh must replace the cached travel costs as well as the vertex heights.
    steepNavigation.refreshTerrainHeights(diagonalHill);
    require(!steepNavigation.findPath(diagonalHill, world, {-8, 0, -8}, {8, 0, 8}, 2.2F, 0).empty(),
            "Changed terrain invalidates slope costs and clearance caches");
    auto costly = tuning;
    costly.minimumSlopeSpeedScale = .1F;
    costly.slowdownStartDegrees = 0;
    costly.slowdownFullDegrees = 10;
    auto mound = hillside(8, false, costly, true);
    Navigation detourNavigation(mound, definitions, 10);
    const auto detour = detourNavigation.findPath(mound, world, {-30, 0, 1}, {30, 0, 1}, .65F, 0);
    require(!detour.empty() && std::any_of(detour.begin(), detour.end(), [](auto p) { return std::abs(p.z) > 8; }),
            "Travel-time costs prefer a faster detour around a slow, passable hill");
    auto flattened = hillside(0);
    detourNavigation.refreshTerrainHeights(flattened);
    const auto shortcut = detourNavigation.findPath(flattened, world, {-30, 0, 1}, {30, 0, 1}, .65F, 0);
    require(!shortcut.empty() && std::all_of(shortcut.begin(), shortcut.end(), [](auto p) { return std::abs(p.z) < 2; }),
            "Grading clears cached flow fields and slope costs so the faster direct route is restored");
    Navigation navigation(hill, definitions, 10);
    const auto began = std::chrono::steady_clock::now();
    const auto path = navigation.findPath(hill, world, {-4, 0, 1}, {4, 0, 1}, .65F, 0);
    std::cout << "First hillside route seconds: " << std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count() << '\n';
    require(!path.empty(), "Navigation finds a route over the same walkable hill");
    glm::vec2 previous{-4, 1};
    for (const auto& point : path) {
        require(hill.permitsMovementSegment(previous, {point.x, point.z}, .65F, land),
                "Every planned segment is accepted by actual movement");
        previous = {point.x, point.z};
    }
    require(!navigation.findPath(hill, world, {-4, 0, 1}, {4, 0, 1}, .65F, 0).empty(),
            "Cached traversal edges preserve repeated routing");
    GameSession session{definitions, 7, "france", "brazil", "unassigned", "unassigned", 10};
    session.world().replaceEntities({});
    const_cast<Terrain&>(session.terrain()) = steep;
    const auto spawn = [&](const char* type, glm::vec3 position) {
        const auto id = session.world().createEntity(type, type, 1).id;
        auto* unit = session.world().findEntity(id);
        definitions.initializeEntity(*unit);
        unit->transform.position = position;
        unit->transform.rotationDegrees.y = 90;
        return id;
    };
    const auto foot = spawn("worker", {-8, 0, -8});
    const auto tank = spawn("light_tank", {-8, 0, 8});
    require(session.submit({1, 1, PossessUnitCommand{foot}}) &&
                session.submit({1, 2, PossessUnitCommand{tank}}) &&
                session.submit({1, 3, DirectUnitInputCommand{foot, {1, 0}, false, 90}}) &&
                session.submit({1, 4, DirectVehicleInputCommand{tank, 1, 0, false}}),
            "Submit foot and tank input on the same steep hillside");
    session.advanceTicks(210);
    require(session.world().findEntity(foot)->transform.position.x > 8 &&
                session.world().findEntity(tank)->transform.position.x > 8,
            "Actual possessed foot and tank movement cross the formerly blocked hill");
    return 0;
}
