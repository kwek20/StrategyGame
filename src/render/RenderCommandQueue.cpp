#include "render/RenderCommandQueue.hpp"

#include <algorithm>

namespace strategy {

void RenderCommandQueue::submit(ModelRenderCommand command) {
    commands_.push_back(std::move(command));
}

void RenderCommandQueue::sort() {
    std::stable_sort(commands_.begin(), commands_.end(), [](const auto& left, const auto& right) {
        if (left.material.index != right.material.index)
            return left.material.index < right.material.index;
        return left.model.index < right.model.index;
    });
}

void RenderCommandQueue::clear() { commands_.clear(); }

} // namespace strategy
