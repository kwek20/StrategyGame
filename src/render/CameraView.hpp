#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
namespace strategy {
struct CameraView {
    glm::mat4 view{1.0F};
    glm::mat4 projection{1.0F};
    glm::vec3 position{0.0F};
    glm::vec3 target{0.0F};
    float detailDistance{55.0F};
    [[nodiscard]] glm::mat4 viewProjection() const { return projection * view; }
};
}
