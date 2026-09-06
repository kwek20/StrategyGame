#pragma once

#include "render/RenderPass.hpp"

#include <array>
#include <cstddef>

namespace strategy {

// A deliberately small frame graph. Render entry points declare their pass, and the
// graph validates that scene, overlay, and UI work cannot accidentally regress in order.
class RenderGraph final {
  public:
    void beginFrame();
    void enter(RenderPassKind pass);
    [[nodiscard]] std::size_t executionCount(RenderPassKind pass) const;

  private:
    std::array<std::size_t, 4> executions_{};
    int furthestPass_{-1};
};

} // namespace strategy
