#pragma once

#include "render/CameraView.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace strategy {

class RtsCamera final {
  public:
    void pan(float forward, float right, float deltaSeconds);
    void orbit(float deltaX, float deltaY);
    void dragPan(float deltaX, float deltaY);
    void zoom(float wheelDelta);
    [[nodiscard]] glm::vec2 groundMovement(float forward, float right) const;

    [[nodiscard]] glm::vec3 eye(float groundHeight) const;
    [[nodiscard]] glm::vec3 target(float groundHeight) const;
    [[nodiscard]] const glm::vec3& focus() const {
        return focus_;
    }
    [[nodiscard]] float yawDegrees() const {
        return yawDegrees_;
    }
    [[nodiscard]] float pitchDegrees() const {
        return pitchDegrees_;
    }
    [[nodiscard]] float distance() const {
        return distance_;
    }
    [[nodiscard]] CameraView view(float aspect, float groundHeight) const;

  private:
    glm::vec3 focus_{0.0F, 0.0F, 10.0F};
    float yawDegrees_{0.0F};
    float pitchDegrees_{42.0F};
    float distance_{55.0F};
};

} // namespace strategy
