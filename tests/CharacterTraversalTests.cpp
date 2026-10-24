#include "TerrainTiltFixture.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "world/Navigation.hpp"
#include "world/World.hpp"
#include <stdexcept>
#include <chrono>
#include <iostream>

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
}
int strategyTestMain() {
    using namespace strategy;
    constexpr auto land = movementDomainBit(MovementDomain::land);
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
    DefinitionRegistry definitions;
    World world;
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
    return 0;
}
