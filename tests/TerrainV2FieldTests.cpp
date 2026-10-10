#include "terrain/TerrainV2Generator.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int strategyTestMain() {
    using namespace strategy;
    const auto definitions = TerrainGenerationDefinitions::load();
    const auto recipes = TerrainV2Recipes::load();
    const auto& layout = definitions.layout(TerrainLayoutId{STRATEGY_V2_AUDIT_LAYOUT});
    {
        auto recipe = recipes.resolve(layout);
        require(recipe.competitive, "Audit requires a competitive recipe");
        for (const float extent : {240.0F, 360.0F, 480.0F}) {
            for (std::uint32_t seed : {0U, 7U, 12345U}) {
                const auto a = TerrainV2Generator::generate(
                    seed, recipe, definitions.generator(layout.generator), extent, 2);
                const auto b = TerrainV2Generator::generate(
                    seed, recipe, definitions.generator(layout.generator), extent, 2);
                require(a.terraces.cells().size() == b.terraces.cells().size(), "Stable grid size");
                for (std::size_t i = 0; i < a.terraces.cells().size(); ++i) {
                    const auto& cell = a.terraces.cells()[i];
                    require(cell.elevation == b.terraces.cells()[i].elevation,
                            "Repeatable elevations");
                    require(std::abs(cell.elevation - cell.source.mean) <=
                                recipe.cells.elevationStep + 0.001F,
                            "No cumulative drift from immutable source");
                }
                for (auto opening : a.plan.openings())
                    require(a.terraces.surfaceAt(opening) == recipe.baseHeight,
                            "Reserved openings retain their flat plane");
                require(a.plan.hasWater() == (layout.id.value == "central_water"),
                        "Water belongs to the layout recipe");
                if (!a.plan.hasWater()) {
                    recipe.waterLevel = -100;
                    recipe.lakeDepth = std::numeric_limits<float>::quiet_NaN();
                    recipe.bankWidth = std::numeric_limits<float>::quiet_NaN();
                    recipe.isletHeight = std::numeric_limits<float>::quiet_NaN();
                    const auto dry = TerrainV2Generator::generate(
                        seed, recipe, definitions.generator(layout.generator), extent, 2);
                    for (std::size_t i = 0; i < dry.terraces.cells().size(); ++i)
                        require(dry.terraces.cells()[i].elevation ==
                                    a.terraces.cells()[i].elevation,
                                "Inactive water cannot affect dry terrain");
                }
            }
        }
    }
    std::cout << STRATEGY_V2_AUDIT_LAYOUT << " repeated field matrix passed\n";
    return 0;
}
