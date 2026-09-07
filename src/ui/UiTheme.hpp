#pragma once

#include <glm/vec3.hpp>

namespace strategy {

struct UiTheme final {
    static constexpr glm::vec3 panel{0.025F, 0.04F, 0.06F};
    static constexpr glm::vec3 control{0.10F, 0.16F, 0.22F};
    static constexpr glm::vec3 controlHover{0.22F, 0.38F, 0.52F};
    static constexpr glm::vec3 disabled{0.16F, 0.17F, 0.18F};
    static constexpr glm::vec3 text{0.95F, 0.98F, 0.82F};
    static constexpr glm::vec3 disabledText{0.58F, 0.61F, 0.62F};
};

} // namespace strategy
