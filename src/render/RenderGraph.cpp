#include "render/RenderGraph.hpp"

#include <stdexcept>

namespace strategy {
namespace {
int order(RenderPassKind pass) {
    switch (pass) {
    case RenderPassKind::terrain: return 0;
    case RenderPassKind::world: return 1;
    case RenderPassKind::overlay: return 2;
    case RenderPassKind::userInterface: return 3;
    }
    return 0;
}
} // namespace

void RenderGraph::beginFrame() {
    executions_.fill(0);
    furthestPass_ = -1;
}

void RenderGraph::enter(RenderPassKind pass) {
    const int current = order(pass);
    if (current < furthestPass_)
        throw std::runtime_error("Render pass order regressed within a frame");
    furthestPass_ = current;
    ++executions_[static_cast<std::size_t>(current)];
}

std::size_t RenderGraph::executionCount(RenderPassKind pass) const {
    return executions_[static_cast<std::size_t>(order(pass))];
}

} // namespace strategy
