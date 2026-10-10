#include "terrain/SurfaceGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <map>
#include <numeric>
namespace strategy::surface_geometry {
double cross(Point a, Point b) {
    return a.x * b.y - a.y * b.x;
}
Polygon clip(const Polygon& input, Point normal, double limit) {
    Polygon output;
    if (input.empty())
        return output;
    Point previous = input.back();
    double before = glm::dot(previous, normal) - limit;
    for (auto point : input) {
        const double now = glm::dot(point, normal) - limit;
        if ((now <= 0) != (before <= 0))
            output.push_back(previous + (point - previous) * (before / (before - now)));
        if (now <= 0)
            output.push_back(point);
        previous = point;
        before = now;
    }
    return output;
}
double area(const Polygon& polygon) {
    double result = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i)
        result += cross(polygon[i], polygon[(i + 1) % polygon.size()]);
    return std::abs(result) * 0.5;
}
// Integral of each clipped polygon edge inside a disk, or its circular sector outside.
double diskArea(const Polygon& polygon, double radius) {
    double result = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point a = polygon[i], b = polygon[(i + 1) % polygon.size()], d = b - a;
        const double aa = glm::dot(d, d), bb = 2 * glm::dot(a, d),
                     cc = glm::dot(a, a) - radius * radius;
        std::vector<double> cuts{0, 1};
        const double discriminant = bb * bb - 4 * aa * cc;
        if (aa > 0 && discriminant > 0) {
            for (double t : {(-bb - std::sqrt(discriminant)) / (2 * aa),
                             (-bb + std::sqrt(discriminant)) / (2 * aa)})
                if (t > 0 && t < 1)
                    cuts.push_back(t);
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t j = 1; j < cuts.size(); ++j) {
            const auto p = a + d * cuts[j - 1], q = a + d * cuts[j], mid = (p + q) * 0.5;
            result += glm::dot(mid, mid) < radius * radius
                          ? cross(p, q) * 0.5
                          : radius * radius * std::atan2(cross(p, q), glm::dot(p, q)) * 0.5;
        }
    }
    return std::abs(result);
}
bool segmentContact(Point a, Point b, Point halfExtents, bool circular) {
    const Point direction = b - a;
    double low = 0, high = 1;
    if (circular) {
        const double aa = glm::dot(direction, direction), bb = 2 * glm::dot(a, direction);
        const double cc = glm::dot(a, a) - halfExtents.x * halfExtents.x;
        const double discriminant = bb * bb - 4 * aa * cc;
        if (aa <= 1e-20 || discriminant <= 0)
            return false;
        low = std::max(low, (-bb - std::sqrt(discriminant)) / (2 * aa));
        high = std::min(high, (-bb + std::sqrt(discriminant)) / (2 * aa));
    } else {
        for (int axis = 0; axis < 2; ++axis) {
            if (std::abs(direction[axis]) < 1e-12) {
                if (std::abs(a[axis]) > halfExtents[axis])
                    return false;
                continue;
            }
            const double p = (-halfExtents[axis] - a[axis]) / direction[axis];
            const double q = (halfExtents[axis] - a[axis]) / direction[axis];
            low = std::max(low, std::min(p, q));
            high = std::min(high, std::max(p, q));
        }
    }
    return (high - low) * glm::length(direction) > 1e-8;
}
double largestConnectedArea(std::span<const SupportTriangle> triangles) {
    std::vector<std::size_t> parent(triangles.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t index) {
        while (parent[index] != index) {
            parent[index] = parent[parent[index]];
            index = parent[index];
        }
        return index;
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::size_t> edges;
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        if (triangles[i].area <= 1e-10)
            continue;
        for (std::size_t edge = 0; edge < 3; ++edge) {
            if (!triangles[i].edgeContact[edge])
                continue;
            const auto a = triangles[i].vertices[edge], b = triangles[i].vertices[(edge + 1) % 3];
            const auto [it, inserted] = edges.try_emplace(std::minmax(a, b), i);
            if (!inserted) {
                const auto own = root(i), other = root(it->second);
                parent[std::max(own, other)] = std::min(own, other);
            }
        }
    }
    std::vector<double> areas(triangles.size(), 0);
    double maximum = 0;
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        if (triangles[i].area <= 1e-10)
            continue;
        const auto component = root(i);
        areas[component] += triangles[i].area;
        maximum = std::max(maximum, areas[component]);
    }
    return maximum;
}
} // namespace strategy::surface_geometry
