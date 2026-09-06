#include "render/MaterialManager.hpp"
#include "render/RenderCommandQueue.hpp"
#include "render/RenderGraph.hpp"

#include <stdexcept>

int main() {
    strategy::MaterialManager materials;
    const strategy::ShaderHandle shader{2, 1};
    const strategy::MaterialHandle world = materials.create({"world", shader});
    const strategy::MaterialHandle duplicate = materials.create({"world", shader});
    if (world != duplicate || materials.find("world") != world ||
        materials.get(world).shader != shader)
        return 1;
    bool staleRejected = false;
    try {
        (void)materials.get({world.index, world.generation + 1});
    } catch (const std::runtime_error&) {
        staleRejected = true;
    }
    if (!staleRejected)
        return 2;

    const strategy::MaterialHandle remembered =
        materials.create({"remembered", shader, {}, {}, 0.0F, strategy::MaterialPass::transparent});
    strategy::RenderCommandQueue queue;
    queue.submit({{4, 1}, remembered});
    queue.submit({{3, 1}, world});
    queue.submit({{1, 1}, world});
    queue.sort();
    if (queue.commands().size() != 3 || queue.commands()[0].model.index != 1 ||
        queue.commands()[1].model.index != 3 || queue.commands()[2].material != remembered)
        return 3;

    strategy::RenderGraph graph;
    graph.beginFrame();
    graph.enter(strategy::RenderPassKind::terrain);
    graph.enter(strategy::RenderPassKind::world);
    graph.enter(strategy::RenderPassKind::overlay);
    graph.enter(strategy::RenderPassKind::overlay);
    graph.enter(strategy::RenderPassKind::userInterface);
    if (graph.executionCount(strategy::RenderPassKind::overlay) != 2)
        return 4;
    bool regressionRejected = false;
    try {
        graph.enter(strategy::RenderPassKind::world);
    } catch (const std::runtime_error&) {
        regressionRejected = true;
    }
    return regressionRejected ? 0 : 5;
}
