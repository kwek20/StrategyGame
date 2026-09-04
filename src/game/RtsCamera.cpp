#include "game/RtsCamera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace strategy {

void RtsCamera::pan(float forward, float right, float deltaSeconds) {
    constexpr float speed = 28.0F;
    const glm::vec2 ground = groundMovement(forward, right);
    focus_.x += ground.x * speed * deltaSeconds;
    focus_.z += ground.y * speed * deltaSeconds;
}

glm::vec2 RtsCamera::groundMovement(float forward, float right) const {
    const float yaw = glm::radians(yawDegrees_);
    // The eye is offset from the focus by (+sin(yaw), +cos(yaw)), so
    // camera-forward on the ground plane is the inverse of that offset.
    const glm::vec3 forwardDirection{-std::sin(yaw), 0.0F, -std::cos(yaw)};
    const glm::vec3 rightDirection{std::cos(yaw), 0.0F, -std::sin(yaw)};
    glm::vec3 movement = forwardDirection * forward + rightDirection * right;
    if (glm::length(movement) > 0.0F) {
        movement = glm::normalize(movement);
    }
    return {movement.x, movement.z};
}

void RtsCamera::orbit(float deltaX, float deltaY) {
    yawDegrees_ = std::fmod(yawDegrees_ - deltaX * 0.28F, 360.0F);
    pitchDegrees_ = std::clamp(pitchDegrees_ + deltaY * 0.22F, 18.0F, 78.0F);
}

void RtsCamera::dragPan(float deltaX,float deltaY) {
    const float yaw=glm::radians(yawDegrees_);
    const glm::vec2 forward{-std::sin(yaw),-std::cos(yaw)};
    const glm::vec2 right{std::cos(yaw),-std::sin(yaw)};
    // Mouse right/up maps to screen-right/screen-forward at the current yaw.
    const float sensitivity=0.10F;
    const glm::vec2 movement=(right*deltaX+forward*(-deltaY))*sensitivity;
    focus_.x+=movement.x;focus_.z+=movement.y;
}

void RtsCamera::zoom(float wheelDelta) {
    distance_ = std::clamp(distance_ - wheelDelta * 4.0F, 16.0F, 120.0F);
}

glm::vec3 RtsCamera::eye(float groundHeight) const {
    const float yaw = glm::radians(yawDegrees_);
    const float pitch = glm::radians(pitchDegrees_);
    const float horizontalDistance = std::cos(pitch) * distance_;
    return {focus_.x + std::sin(yaw) * horizontalDistance,
            groundHeight + std::sin(pitch) * distance_,
            focus_.z + std::cos(yaw) * horizontalDistance};
}

glm::vec3 RtsCamera::target(float groundHeight) const {
    return {focus_.x, groundHeight, focus_.z};
}

CameraView RtsCamera::view(float aspect,float groundHeight) const {
    CameraView result; result.position=eye(groundHeight); result.target=target(groundHeight);
    result.view=glm::lookAt(result.position,result.target,{0.0F,1.0F,0.0F});
    result.projection=glm::perspective(glm::radians(55.0F),aspect,0.1F,350.0F);
    result.detailDistance=distance_; return result;
}

} // namespace strategy
