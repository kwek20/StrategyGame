#pragma once
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

namespace strategy {
// A rigid basis whose up vector is exactly normal to the foundation plane. The footprint's
// heading is projected onto that plane, avoiding independent pitch/roll caps and yaw errors.
inline glm::mat4 foundationOrientation(glm::vec2 gradient, float yawDegrees) {
    const float yaw = -yawDegrees * 0.017453292519943295F;
    const auto up = glm::normalize(glm::vec3{-gradient.x, 1, -gradient.y});
    const float s = std::sin(yaw), c = std::cos(yaw);
    const auto forward = glm::normalize(glm::vec3{s, gradient.x * s + gradient.y * c, c});
    const auto right = glm::normalize(glm::cross(up, forward));
    return glm::mat4{
        glm::vec4{right, 0}, glm::vec4{up, 0}, glm::vec4{forward, 0}, glm::vec4{0, 0, 0, 1}};
}
} // namespace strategy
