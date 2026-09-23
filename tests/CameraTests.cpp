#include "game/RtsCamera.hpp"

#include <cmath>
#include <glm/geometric.hpp>
#include <iostream>

namespace {

bool nearlyEqual(float left, float right, float tolerance = 0.001F) {
    return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
    strategy::RtsCamera camera;
    const glm::vec3 start = camera.focus();

    camera.pan(1.0F, 0.0F, 0.1F, true);
    const glm::vec3 afterFirstBoostedFrame = camera.focus();
    camera.pan(1.0F, 0.0F, 1.15F, true);
    const glm::vec3 afterRamp = camera.focus();
    const float firstDistance = glm::distance(start, afterFirstBoostedFrame);
    const float laterSpeed = glm::distance(afterFirstBoostedFrame, afterRamp) / 1.15F;

    camera.pan(1.0F, 0.0F, 0.1F, false);
    const float releasedDistance = glm::distance(afterRamp, camera.focus());

    const bool valid = laterSpeed > firstDistance / 0.1F &&
                       nearlyEqual(releasedDistance, 2.8F);
    if (!valid)
        std::cerr << "Camera acceleration did not ramp or reset on Shift release\n";
    return valid ? 0 : 1;
}
