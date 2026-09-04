#include "game/RtsCamera.hpp"
#include "game/ThirdPersonCamera.hpp"
#include "world/World.hpp"
#include "simulation/GameSession.hpp"

#include <iostream>
#include <cmath>

int main() {
    strategy::World world;
    const strategy::EntityId first = world.createEntity("First").id;
    const strategy::EntityId second = world.createEntity("Second", "house").id;
    bool valid = first != second && world.size() == 2;
    valid = valid && world.findEntity(first) != nullptr;
    valid = valid && world.findEntity(second)->modelKey == "house";
    valid = valid && world.destroyEntity(first) && world.findEntity(first) == nullptr;
    valid = valid && world.size() == 1;

    strategy::RtsCamera camera;
    const auto originalFocus = camera.focus();
    camera.pan(1.0F, 0.0F, 1.0F);
    valid = valid && camera.focus().z < originalFocus.z;

    strategy::RtsCamera rotatedCamera;
    rotatedCamera.orbit(-90.0F / 0.28F, 0.0F);
    const auto rotatedStart = rotatedCamera.focus();
    rotatedCamera.pan(1.0F, 0.0F, 1.0F);
    valid = valid && rotatedCamera.focus().x < rotatedStart.x;
    const auto beforeStrafe = rotatedCamera.focus();
    rotatedCamera.pan(0.0F, 1.0F, 1.0F);
    valid = valid && rotatedCamera.focus().z < beforeStrafe.z;
    camera.orbit(10000.0F, 10000.0F);
    valid = valid && camera.pitchDegrees() >= 18.0F && camera.pitchDegrees() <= 78.0F;
    camera.zoom(10000.0F);
    valid = valid && camera.distance() == 16.0F;
    camera.zoom(-10000.0F);
    valid = valid && camera.distance() == 120.0F;

    strategy::ThirdPersonCamera thirdPerson;
    const auto thirdPersonView = thirdPerson.view(16.0F / 9.0F, {2.0F, 3.0F, 4.0F});
    valid = valid && thirdPersonView.position.y > 3.0F;
    valid = valid && thirdPersonView.detailDistance < camera.distance();
    const glm::vec2 thirdPersonForward = thirdPerson.groundMovement(1.0F, 0.0F);
    valid = valid && std::abs(glm::length(thirdPersonForward) - 1.0F) < 0.002F;

    strategy::GameSession session{123U};
    strategy::Entity* playerOneUnit = nullptr;
    strategy::Entity* playerTwoUnit = nullptr;
    for (strategy::Entity& candidate : session.world().entities()) {
        if (candidate.modelKey == "worker" && candidate.authority.owner == 1)
            playerOneUnit = &candidate;
        if (candidate.modelKey == "worker" && candidate.authority.owner == 2)
            playerTwoUnit = &candidate;
    }
    valid = valid && playerOneUnit != nullptr && playerTwoUnit != nullptr;
    valid = valid && session.players().players().size() == 2;
    std::size_t resourceCount = 0;
    for (const strategy::Entity& resource : session.world().entities()) {
        if (resource.authority.owner != 0) continue;
        ++resourceCount;
        bool mirrored = false;
        for (const strategy::Entity& candidate : session.world().entities()) {
            if (candidate.authority.owner == 0 && candidate.modelKey == resource.modelKey
                && std::abs(candidate.transform.position.x + resource.transform.position.x) < 0.001F
                && std::abs(candidate.transform.position.z + resource.transform.position.z) < 0.001F) {
                mirrored = true;
                break;
            }
        }
        valid = valid && mirrored;
    }
    valid = valid && resourceCount >= 20;
    const auto playerOneStart = playerOneUnit->transform.position;
    valid = valid && session.submit({1, 1,
        strategy::PossessUnitCommand{playerTwoUnit->id}});
    valid = valid && session.submit({1, 2,
        strategy::PossessUnitCommand{playerOneUnit->id}});
    valid = valid && session.submit({1, 3,
        strategy::DirectUnitInputCommand{playerOneUnit->id, {1.0F, 0.0F}}});
    valid = valid && !session.submit({1, 3,
        strategy::ReleaseUnitCommand{playerOneUnit->id}});
    session.update(strategy::GameSession::fixedTickSeconds);
    valid = valid && playerTwoUnit->authority.directController == 0;
    valid = valid && playerOneUnit->authority.directController == 1;
    valid = valid && playerOneUnit->transform.position.x > playerOneStart.x;

    if (!valid) {
        std::cerr << "Game system validation failed\n";
        return 1;
    }
    std::cout << "Game system validation passed\n";
    return 0;
}
