#pragma once
#include "render/CameraView.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
namespace strategy {
class ThirdPersonCamera final {
  public:
    void orbit(float deltaX, float deltaY);
    void zoom(float wheelDelta);
    [[nodiscard]] glm::vec2 groundMovement(float forward, float right) const;
    void updateAimZoom(bool held, float dt);
    void faceWeapon(float yawDegrees);
    [[nodiscard]] CameraView
    view(float aspect, const glm::vec3& feetPosition, bool weaponView = false) const;
    [[nodiscard]] float characterFacingDegrees() const;

  private:
    float yawDegrees_{0.0F};
    float pitchDegrees_{18.0F};
    float distance_{6.5F};
    float aimZoom_{0.F};
};
} // namespace strategy
