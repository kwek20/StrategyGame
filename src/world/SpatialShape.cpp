#include "world/SpatialShape.hpp"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace strategy {
namespace {
glm::vec2 rotate(glm::vec2 value, float degrees) {
    const float radians = degrees * glm::pi<float>() / 180.0F;
    const float cosine = std::cos(radians), sine = std::sin(radians);
    return {value.x * cosine - value.y * sine, value.x * sine + value.y * cosine};
}

glm::vec2 localPoint(const SpatialShape& shape, glm::vec2 point) {
    return rotate(point - shape.center, -shape.rotationDegrees);
}

glm::vec2 worldPoint(const SpatialShape& shape, glm::vec2 point) {
    return shape.center + rotate(point, shape.rotationDegrees);
}
} // namespace

SpatialShape expanded(SpatialShape shape, float distance) {
    const float amount = std::max(0.0F, distance);
    if (shape.kind == FootprintShape::circle)
        shape.radius += amount;
    else
        shape.halfExtents += glm::vec2{amount};
    return shape;
}

glm::vec2 closestPoint(const SpatialShape& shape, glm::vec2 point) {
    if (shape.kind == FootprintShape::circle) {
        const glm::vec2 offset = point - shape.center;
        const float distance = glm::length(offset);
        return distance <= shape.radius || distance <= 0.0001F
                   ? point
                   : shape.center + offset * (shape.radius / distance);
    }
    const glm::vec2 local = localPoint(shape, point);
    return worldPoint(shape, glm::clamp(local, -shape.halfExtents, shape.halfExtents));
}

glm::vec2 closestBoundaryPoint(const SpatialShape& shape, glm::vec2 point) {
    if (shape.kind == FootprintShape::circle) {
        glm::vec2 offset = point - shape.center;
        const float distance = glm::length(offset);
        if (distance <= 0.0001F) offset = {1.0F, 0.0F};
        else offset /= distance;
        return shape.center + offset * shape.radius;
    }
    glm::vec2 local = localPoint(shape, point);
    const glm::vec2 clamped = glm::clamp(local, -shape.halfExtents, shape.halfExtents);
    if (glm::any(glm::notEqual(local, clamped)))
        return worldPoint(shape, clamped);
    const glm::vec2 remaining = shape.halfExtents - glm::abs(local);
    glm::vec2 boundary = local;
    if (remaining.x <= remaining.y)
        boundary.x = local.x < 0.0F ? -shape.halfExtents.x : shape.halfExtents.x;
    else
        boundary.y = local.y < 0.0F ? -shape.halfExtents.y : shape.halfExtents.y;
    return worldPoint(shape, boundary);
}

float signedDistance(const SpatialShape& shape, glm::vec2 point) {
    if (shape.kind == FootprintShape::circle)
        return glm::length(point - shape.center) - shape.radius;
    const glm::vec2 difference = glm::abs(localPoint(shape, point)) - shape.halfExtents;
    const glm::vec2 outside = glm::max(difference, glm::vec2{0.0F});
    return glm::length(outside) + std::min(std::max(difference.x, difference.y), 0.0F);
}

bool overlaps(const SpatialShape& left, const SpatialShape& right) {
    if (left.kind == FootprintShape::circle)
        return signedDistance(right, left.center) < left.radius;
    if (right.kind == FootprintShape::circle)
        return signedDistance(left, right.center) < right.radius;

    // Separating-axis test for two oriented rectangles.
    const glm::vec2 leftX = rotate({1.0F, 0.0F}, left.rotationDegrees);
    const glm::vec2 leftY = rotate({0.0F, 1.0F}, left.rotationDegrees);
    const glm::vec2 rightX = rotate({1.0F, 0.0F}, right.rotationDegrees);
    const glm::vec2 rightY = rotate({0.0F, 1.0F}, right.rotationDegrees);
    const glm::vec2 offset = right.center - left.center;
    for (const glm::vec2 axis : {leftX, leftY, rightX, rightY}) {
        const float separation = std::abs(glm::dot(offset, axis));
        const float leftProjection = std::abs(glm::dot(leftX, axis)) * left.halfExtents.x +
                                     std::abs(glm::dot(leftY, axis)) * left.halfExtents.y;
        const float rightProjection = std::abs(glm::dot(rightX, axis)) * right.halfExtents.x +
                                      std::abs(glm::dot(rightY, axis)) * right.halfExtents.y;
        if (separation >= leftProjection + rightProjection)
            return false;
    }
    return true;
}

glm::vec2 axisAlignedHalfExtents(const SpatialShape& shape) {
    if (shape.kind == FootprintShape::circle) return {shape.radius, shape.radius};
    const glm::vec2 x = glm::abs(rotate({1.0F, 0.0F}, shape.rotationDegrees));
    const glm::vec2 y = glm::abs(rotate({0.0F, 1.0F}, shape.rotationDegrees));
    return x * shape.halfExtents.x + y * shape.halfExtents.y;
}

} // namespace strategy
