#pragma once

#include "terrain/Terrain.hpp"

#include <glm/vec2.hpp>

namespace strategy {

// Authoritative horizontal-space geometry shared by collision, navigation and
// interaction queries. Shapes are intentionally independent of entity types.
struct SpatialShape {
    FootprintShape kind{FootprintShape::circle};
    glm::vec2 center{0.0F};
    float radius{0.0F};
    glm::vec2 halfExtents{0.0F};
    float rotationDegrees{0.0F};
};

[[nodiscard]] SpatialShape expanded(SpatialShape shape, float distance);
[[nodiscard]] glm::vec2 closestPoint(const SpatialShape& shape, glm::vec2 point);
[[nodiscard]] glm::vec2 closestBoundaryPoint(const SpatialShape& shape, glm::vec2 point);
[[nodiscard]] float signedDistance(const SpatialShape& shape, glm::vec2 point);
[[nodiscard]] bool overlaps(const SpatialShape& left, const SpatialShape& right);
[[nodiscard]] glm::vec2 axisAlignedHalfExtents(const SpatialShape& shape);

} // namespace strategy
