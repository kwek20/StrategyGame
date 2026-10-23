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
void appendPlacementGeometry(PlacementGeometry& geometry,
                             const Polygon& footprint,
                             const Polygon& support) {
    if (support.empty()) {
        geometry.unsupported.push_back(footprint);
        return;
    }
    geometry.supported.push_back(support);
    if (footprint.size() == support.size() &&
        std::equal(footprint.begin(), footprint.end(), support.begin()))
        return;
    // Partition footprint minus its convex supported region into disjoint convex polygons.
    // This avoids drawing every fully supported triangle twice and keeps translucent colors exact.
    Polygon remainder = footprint;
    for (std::size_t i = 0; i < support.size() && !remainder.empty(); ++i) {
        const auto a = support[i], b = support[(i + 1) % support.size()], edge = b - a;
        if (glm::dot(edge, edge) < 1e-20)
            continue;
        const Point normal{edge.y, -edge.x};
        const double limit = glm::dot(normal, a);
        auto outside = clip(remainder, -normal, -limit);
        if (area(outside) > 1e-10)
            geometry.unsupported.push_back(std::move(outside));
        remainder = clip(remainder, normal, limit);
    }
}

// Integral of each clipped polygon edge inside a disk, or its circular sector outside.
double diskArea(const Polygon& polygon, double radius) {
    double result = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point a = polygon[i], b = polygon[(i + 1) % polygon.size()], d = b - a;
        const double aa = glm::dot(d, d), bb = 2 * glm::dot(a, d),
                     cc = glm::dot(a, a) - radius * radius;
        std::array<double, 4> cuts{0, 1};
        std::size_t cutCount = 2;
        const double discriminant = bb * bb - 4 * aa * cc;
        if (aa > 0 && discriminant > 0) {
            for (double t : {(-bb - std::sqrt(discriminant)) / (2 * aa),
                             (-bb + std::sqrt(discriminant)) / (2 * aa)})
                if (t > 0 && t < 1)
                    cuts[cutCount++] = t;
        }
        std::sort(cuts.begin(), cuts.begin() + cutCount);
        for (std::size_t j = 1; j < cutCount; ++j) {
            const auto p = a + d * cuts[j - 1], q = a + d * cuts[j], mid = (p + q) * 0.5;
            result += glm::dot(mid, mid) < radius * radius
                          ? cross(p, q) * 0.5
                          : radius * radius * std::atan2(cross(p, q), glm::dot(p, q)) * 0.5;
        }
    }
    return std::abs(result);
}
EdgeInterval segmentInterval(Point a, Point b, Point halfExtents, bool circular) {
    if (circular ? (glm::dot(a, a) <= halfExtents.x * halfExtents.x &&
                    glm::dot(b, b) <= halfExtents.x * halfExtents.x)
                 : (std::abs(a.x) <= halfExtents.x && std::abs(a.y) <= halfExtents.y &&
                    std::abs(b.x) <= halfExtents.x && std::abs(b.y) <= halfExtents.y))
        return {0, 1};
    const Point direction = b - a;
    double low = 0, high = 1;
    if (circular) {
        const double aa = glm::dot(direction, direction), bb = 2 * glm::dot(a, direction);
        const double cc = glm::dot(a, a) - halfExtents.x * halfExtents.x;
        const double discriminant = bb * bb - 4 * aa * cc;
        if (aa <= 1e-20 || discriminant <= 0)
            return {1, 0};
        low = std::max(low, (-bb - std::sqrt(discriminant)) / (2 * aa));
        high = std::min(high, (-bb + std::sqrt(discriminant)) / (2 * aa));
    } else {
        for (int axis = 0; axis < 2; ++axis) {
            if (std::abs(direction[axis]) < 1e-12) {
                if (std::abs(a[axis]) > halfExtents[axis])
                    return {1, 0};
                continue;
            }
            const double p = (-halfExtents[axis] - a[axis]) / direction[axis];
            const double q = (halfExtents[axis] - a[axis]) / direction[axis];
            low = std::max(low, std::min(p, q));
            high = std::min(high, std::max(p, q));
        }
    }
    return {low, high};
}
bool segmentContact(Point a, Point b, Point halfExtents, bool circular) {
    const auto interval = segmentInterval(a, b, halfExtents, circular);
    return (interval.high - interval.low) * glm::length(b - a) > 1e-8;
}
double largestConnectedArea(std::span<const SupportTriangle> triangles,
                            std::span<const std::array<int, 3>> neighbors,
                            bool requireContact,
                            std::span<unsigned char> selected) {
    std::vector<std::size_t> parent(triangles.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t index) {
        while (parent[index] != index) {
            parent[index] = parent[parent[index]];
            index = parent[index];
        }
        return index;
    };
    struct Contact {
        std::size_t triangle{}, edge{};
    };
    const auto join = [&](std::size_t i, std::size_t edge, std::size_t j, std::size_t otherEdge) {
        const auto& a = triangles[i];
        const auto& b = triangles[j];
        if (!a.edgeContact[edge] || !b.edgeContact[otherEdge])
            return;
        const auto ai = a.intervals[edge], bi = b.intervals[otherEdge];
        if (std::min(ai.high, bi.high) - std::max(ai.low, bi.low) <= 1e-8)
            return;
        const auto own = root(i), other = root(j);
        parent[std::max(own, other)] = std::min(own, other);
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, Contact> edges;
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        if (triangles[i].area <= 1e-10)
            continue;
        for (std::size_t edge = 0; edge < 3; ++edge) {
            if (!triangles[i].edgeContact[edge])
                continue;
            const auto a = triangles[i].vertices[edge], b = triangles[i].vertices[(edge + 1) % 3];
            if (!neighbors.empty()) {
                const int neighbor = neighbors[i][edge];
                if (neighbor >= 0 && neighbor < static_cast<int>(i))
                    join(i, edge, static_cast<std::size_t>(neighbor), edge);
            } else {
                const auto [it, inserted] = edges.try_emplace(std::minmax(a, b), Contact{i, edge});
                if (!inserted)
                    join(i, edge, it->second.triangle, it->second.edge);
            }
        }
    }
    std::vector<double> areas(triangles.size(), 0);
    std::vector<double> contacts(triangles.size(), 0);
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        if (triangles[i].area <= 1e-10)
            continue;
        const auto component = root(i);
        areas[component] += triangles[i].area;
        contacts[component] += triangles[i].contactArea;
    }
    double maximum = 0;
    std::size_t winner = triangles.size();
    for (std::size_t i = 0; i < areas.size(); ++i)
        if ((!requireContact || contacts[i] > 1e-8) && areas[i] > maximum) {
            maximum = areas[i];
            winner = i;
        }
    for (std::size_t i = 0; i < selected.size(); ++i)
        selected[i] = i < triangles.size() && triangles[i].area > 1e-10 && root(i) == winner;
    return maximum;
}
} // namespace strategy::surface_geometry
