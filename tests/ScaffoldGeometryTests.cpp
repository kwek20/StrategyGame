#include "render/ScaffoldGeometry.hpp"
#include "terrain/TerrainGenerationPipeline.hpp"
#include "terrain/TerrainV2Recipe.hpp"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
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
    const auto definitions = TerrainGenerationDefinitions::load();
    auto recipe =
        TerrainV2Recipes::load().resolve(definitions.layout(TerrainLayoutId{"open_plains"}));
    recipe.cells = TerrainCellSettings{};
    std::vector<TerrainCellSource> source(1600);
    for (int z = 0; z < 40; ++z)
        for (int x = 0; x < 40; ++x) {
            const float h = x < 20 ? 16.0F : 12.0F;
            source[z * 40 + x] = {h, h, h, {0, 0}, TerrainCellRole::opening};
        }
    Terrain terrain{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(
        terrain,
        7,
        definitions.layout(recipe.layout),
        definitions,
        {TerrainV2Plan::create(7, recipe, 240, 2),
         TerrainTerraces::resolve(source, 40, 240, recipe.cells)});
    const float before = terrain.heightAt(0, 0);
    for (bool circle : {false, true})
        for (float angle : {0.0F, 37.0F}) {
            TerrainFootprint footprint{
                circle ? FootprintShape::circle : FootprintShape::rectangle, 4, {5, 4}, angle};
            TerrainFoundation foundation{7, {circle ? -2.0F : -3.5F, 16, 0}, footprint, {0, 0}};
            foundation.requiresSlab = true;
            const auto mesh = buildScaffoldGeometry(terrain, foundation);
            std::cout << "Shape circle=" << circle << " angle=" << angle
                      << " columns=" << mesh.columns << " braces=" << mesh.braces << '\n';
            require(!mesh.indices.empty() && mesh.columns > 0 && mesh.braces > 0,
                    "Cliff foundations have a closed base, anchored columns and bracing");
            const float radians = glm::radians(angle), c = std::cos(radians), s = std::sin(radians);
            for (const auto& v : mesh.vertices) {
                const auto d = glm::vec2{v.position.x - foundation.center.x,
                                         v.position.z - foundation.center.z};
                const glm::vec2 local{c * d.x + s * d.y, -s * d.x + c * d.y};
                require(circle ? glm::length(local) <= 4.001F
                               : (std::abs(local.x) <= 5.001F && std::abs(local.y) <= 4.001F),
                        "All structural geometry stays inside the actual footprint, including "
                        "rotated feet");
                require(std::isfinite(v.normal.x) && std::abs(glm::length(v.normal) - 1) < .001F,
                        "Steel faces have finite unit normals");
                require(v.position.y <= 16.001F && v.position.y >= 11.9F,
                        "Supports reach the existing bed without changing the building plane");
                require(v.stage > 0 && v.stage < .95F,
                        "Every member has a bounded construction stage");
            }
            for (auto index : mesh.indices)
                require(index < mesh.vertices.size(), "Mesh indices remain valid");
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                const auto a = mesh.vertices[mesh.indices[i]].position,
                           b = mesh.vertices[mesh.indices[i + 1]].position,
                           third = mesh.vertices[mesh.indices[i + 2]].position;
                require(glm::length(glm::cross(b - a, third - a)) > 1e-7F,
                        "No degenerate scaffold triangles");
            }
            foundation.influence = .2F;
            const auto repeated = buildScaffoldGeometry(terrain, foundation);
            require(repeated.indices == mesh.indices &&
                        repeated.vertices.size() == mesh.vertices.size(),
                    "Construction does not change static geometry allocation");
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
                require(repeated.vertices[i].position == mesh.vertices[i].position,
                        "Member positions remain fixed throughout construction");
            auto changed = foundation;
            changed.center.z += 1;
            require(!sameScaffoldGeometry(foundation, changed),
                    "Moving the same foundation identity invalidates geometry");
            changed = foundation;
            changed.influence = 1;
            require(sameScaffoldGeometry(foundation, changed), "Progress changes reuse geometry");
        }
    require(terrain.heightAt(0, 0) == before, "Generating scaffold geometry never edits terrain");
    TerrainFoundation grounded{
        9, {-20, 16, 0}, TerrainFootprint{FootprintShape::rectangle, 4, {5, 4}}, {0, 0}};
    grounded.requiresSlab = true;
    const auto flat = buildScaffoldGeometry(terrain, grounded);
    require(flat.columns == 0 && flat.braces == 0,
            "Supported ground does not receive a forest of unnecessary columns");
    std::cout << "Scaffold footprint, anchoring, construction and geometry checks passed\n";
    return 0;
}
