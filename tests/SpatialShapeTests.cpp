#include "world/SpatialShape.hpp"

#include <cmath>
#include <iostream>

namespace {
bool near(glm::vec2 left, glm::vec2 right) {
    return glm::length(left - right) < 0.001F;
}
}

int main() {
    using namespace strategy;
    bool valid = true;
    const SpatialShape circle{FootprintShape::circle, {0.0F, 0.0F}, 3.0F};
    valid = valid && near(closestBoundaryPoint(circle, {8.0F, 0.0F}), {3.0F, 0.0F});
    valid = valid && signedDistance(circle, {5.0F, 0.0F}) == 2.0F;

    SpatialShape rectangle;
    rectangle.kind = FootprintShape::rectangle;
    rectangle.center = {4.0F, 2.0F};
    rectangle.halfExtents = {3.0F, 1.0F};
    valid = valid && near(closestBoundaryPoint(rectangle, {-5.0F, 2.0F}), {1.0F, 2.0F});
    rectangle.rotationDegrees = 90.0F;
    valid = valid && near(closestBoundaryPoint(rectangle, {4.0F, -8.0F}), {4.0F, -1.0F});

    const SpatialShape actor{FootprintShape::circle, {4.0F, -2.0F}, 0.6F};
    valid = valid && overlaps(expanded(rectangle, 0.7F), actor);
    valid = valid && !overlaps(rectangle, SpatialShape{FootprintShape::circle, {20.0F, 20.0F}, 1.0F});
    if (!valid) std::cerr << "Spatial shape validation failed\n";
    return valid ? 0 : 1;
}
