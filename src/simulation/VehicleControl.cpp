#include "simulation/VehicleControl.hpp"

#include "gameplay/DefinitionRegistry.hpp"
#include "world/Entity.hpp"

#include <algorithm>
#include <cmath>
#include <glm/trigonometric.hpp>

namespace strategy {
glm::vec2 driveVehicle(Entity& entity, const MovementDefinition& tuning, float topSpeed, float dt) {
    auto& unit = entity.unitControl;
    const float throttle = unit.vehicleBrake ? 0.F : std::clamp(unit.directInput.y, -1.F, 1.F);
    const float steering = unit.vehicleBrake ? 0.F : std::clamp(unit.directInput.x, -1.F, 1.F);
    // A/D always turn the nose left/right, including in reverse and at rest.
    entity.transform.rotationDegrees.y = std::remainder(
        entity.transform.rotationDegrees.y + steering * tuning.turnRateDegrees * dt, 360.F);
    const float target = throttle * topSpeed * (throttle < 0 ? tuning.reverseSpeedScale : 1.F);
    const bool changingDirection = target * unit.driveSpeed < 0;
    const float rate = unit.vehicleBrake || changingDirection ? tuning.braking
                       : throttle == 0 ? tuning.coastingDeceleration
                                       : tuning.acceleration;
    // Opposing throttle first brakes to a stop; reverse engages on the next tick.
    const float nextTarget = changingDirection ? 0.F : target;
    unit.driveSpeed += std::clamp(nextTarget - unit.driveSpeed, -rate * dt, rate * dt);
    // Clamp immediately when a movement modifier reduces the permitted speed.
    unit.driveSpeed = std::clamp(unit.driveSpeed, -topSpeed * tuning.reverseSpeedScale, topSpeed);
    const float yaw = glm::radians(entity.transform.rotationDegrees.y);
    return glm::vec2{std::sin(yaw), std::cos(yaw)} * unit.driveSpeed;
}
} // namespace strategy
