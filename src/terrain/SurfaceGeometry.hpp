#pragma once
#include <array>
#include <cstdint>
#include <glm/vec2.hpp>
#include <span>
#include <vector>
namespace strategy::surface_geometry {
using Point = glm::dvec2;
using Polygon = std::vector<Point>;
double cross(Point a, Point b);
Polygon clip(const Polygon& input, Point normal, double limit);
double area(const Polygon& polygon);
// Analytic disk/polygon intersection; no polygon approximation of a circle.
double diskArea(const Polygon& polygon, double radius);
// Positive-length contact with an actual rectangle or disk, in footprint-local space.
bool segmentContact(Point a, Point b, Point halfExtents, bool circular);
struct SupportTriangle {
    std::array<std::uint64_t, 3> vertices;
    std::array<bool, 3> edgeContact;
    double area{};
};
// Shared vertex IDs alone do not connect support: both triangles need a usable shared edge.
double largestConnectedArea(std::span<const SupportTriangle> triangles);
} // namespace strategy::surface_geometry
