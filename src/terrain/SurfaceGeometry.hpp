#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/vec2.hpp>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <vector>
namespace strategy::surface_geometry {
using Point = glm::dvec2;
// A clipped convex raster triangle has at most ten vertices after footprint, dry and
// support-band clipping. Inline storage avoids allocating for every triangle/plane.
class Polygon {
  public:
    Polygon() = default;
    Polygon(const Polygon& other)
        : size_(other.size_) {
        std::copy_n(other.begin(), size_, begin());
    }
    Polygon& operator=(const Polygon& other) {
        if (this != &other) {
            size_ = other.size_;
            std::copy_n(other.begin(), size_, begin());
        }
        return *this;
    }
    Polygon(std::initializer_list<Point> points) {
        for (auto p : points)
            push_back(p);
    }
    void push_back(Point p) {
        if (size_ == points_.size())
            throw std::length_error("Surface polygon capacity exceeded");
        points_[size_++] = p;
    }
    void clear() {
        size_ = 0;
    }
    bool empty() const {
        return size_ == 0;
    }
    std::size_t size() const {
        return size_;
    }
    Point& back() {
        return points_[size_ - 1];
    }
    const Point& back() const {
        return points_[size_ - 1];
    }
    Point& operator[](std::size_t i) {
        return points_[i];
    }
    const Point& operator[](std::size_t i) const {
        return points_[i];
    }
    Point* begin() {
        return points_.data();
    }
    Point* end() {
        return begin() + size_;
    }
    const Point* begin() const {
        return points_.data();
    }
    const Point* end() const {
        return begin() + size_;
    }

  private:
    std::array<Point, 16> points_;
    std::size_t size_{};
};

struct PlacementGeometry {
    // Footprint-local polygons. Circular clipping stays analytic for acceptance;
    // the renderer clips these polygons to the same disk for presentation.
    std::vector<Polygon> unsupported;
    std::vector<Polygon> supported;
};
void appendPlacementGeometry(PlacementGeometry& geometry,
                             const Polygon& footprint,
                             const Polygon& support);

struct EdgeInterval {
    double low{}, high{1};
};
double cross(Point a, Point b);
Polygon clip(const Polygon& input, Point normal, double limit);
double area(const Polygon& polygon);
// Analytic disk/polygon intersection; no polygon approximation of a circle.
double diskArea(const Polygon& polygon, double radius);
// Positive-length contact with an actual rectangle or disk, in footprint-local space.
bool segmentContact(Point a, Point b, Point halfExtents, bool circular);
EdgeInterval segmentInterval(Point a, Point b, Point halfExtents, bool circular);
struct SupportTriangle {
    std::array<std::uint64_t, 3> vertices;
    std::array<bool, 3> edgeContact;
    double area{};
    std::array<EdgeInterval, 3> intervals;
    double contactArea{};
};
// Shared vertex IDs alone do not connect support: both triangles need a usable shared edge.
// Contact-filtered queries exclude disconnected lower shelves; selected marks the winning patch.
double largestConnectedArea(std::span<const SupportTriangle> triangles,
                            std::span<const std::array<int, 3>> neighbors = {},
                            bool requireContact = false,
                            std::span<unsigned char> selected = {});
} // namespace strategy::surface_geometry
