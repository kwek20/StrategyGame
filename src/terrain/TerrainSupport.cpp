#include "terrain/TerrainSupport.hpp"

#include "terrain/SurfaceGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <numbers>
#include <vector>

namespace strategy {
namespace {
using namespace surface_geometry;
struct Piece {
    std::array<std::uint64_t, 3> vertices;
    float maximum{};
    double dryArea{};
    bool flat{};
};
} // namespace
bool TerrainShoreSupport::valid() const {
    return std::isfinite(minimumWetFraction) && std::isfinite(maximumWetFraction) &&
           std::isfinite(minimumConnectedDrySupportFraction) && minimumWetFraction > 0 &&
           maximumWetFraction < 1 && minimumWetFraction <= maximumWetFraction &&
           minimumConnectedDrySupportFraction > 0 &&
           minimumConnectedDrySupportFraction + minimumWetFraction <= 1;
}
TerrainPlacementResult TerrainSupport::evaluate(const Terrain& terrain,
                                                glm::vec2 center,
                                                const TerrainFootprint& footprint,
                                                TerrainPlacementProfile profile) {
    TerrainPlacementResult result;
    result.failure = TerrainPlacementFailure::forbiddenTerrain;
    if (profile.support == TerrainBuildingSupport::shore &&
        (!profile.shore || !profile.shore->valid()))
        return result;
    const bool circular = footprint.shape == FootprintShape::circle;
    const Point extent = circular ? Point{footprint.radius} : Point{footprint.halfExtents};
    if (extent.x <= 0 || extent.y <= 0 || !std::isfinite(extent.x) || !std::isfinite(extent.y) ||
        !std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(footprint.rotationDegrees))
        return result;
    const double angle = footprint.rotationDegrees * std::numbers::pi / 180.0;
    const double cosine = std::cos(angle), sine = std::sin(angle);
    const auto local = [&](glm::vec2 p) {
        const Point delta = Point{p} - Point{center};
        return Point{cosine * delta.x + sine * delta.y, -sine * delta.x + cosine * delta.y};
    };
    const auto world = [&](Point p) {
        return center + glm::vec2{cosine * p.x - sine * p.y, sine * p.x + cosine * p.y};
    };
    const float activeHalf = terrain.v2Landscape() ? terrain.v2Landscape()->plan.extent() * 0.5F
                                                   : terrain.worldExtent() * 0.5F;
    const Point worldBounds = circular
                                  ? extent
                                  : Point{std::abs(cosine) * extent.x + std::abs(sine) * extent.y,
                                          std::abs(sine) * extent.x + std::abs(cosine) * extent.y};
    if (std::abs(center.x) + worldBounds.x > activeHalf ||
        std::abs(center.y) + worldBounds.y > activeHalf)
        return result;
    const double bounds = circular ? extent.x : glm::length(extent);
    const double half = terrain.worldExtent() * 0.5;
    const auto grid = [&](double value) {
        return static_cast<int>(std::floor((value + half) / Terrain::spacing));
    };
    const int x0 = std::max(0, grid(center.x - bounds)), z0 = std::max(0, grid(center.y - bounds));
    const int x1 = std::min(Terrain::cellCount - 1, grid(center.x + bounds)),
              z1 = std::min(Terrain::cellCount - 1, grid(center.y + bounds));
    const auto footprintArea = [&](const Polygon& polygon) {
        if (!circular || std::all_of(polygon.begin(), polygon.end(), [&](auto p) {
                return glm::dot(p, p) <= extent.x * extent.x;
            }))
            return area(polygon);
        return diskArea(polygon, extent.x);
    };
    result.footprintArea =
        circular ? std::numbers::pi * extent.x * extent.x : 4 * extent.x * extent.y;
    const auto clipFootprint = [&](Polygon polygon) {
        if (std::all_of(polygon.begin(), polygon.end(), [&](auto p) {
                return std::abs(p.x) <= extent.x && std::abs(p.y) <= extent.y;
            }))
            return polygon;
        polygon = clip(polygon, {1, 0}, extent.x);
        polygon = clip(polygon, {-1, 0}, extent.x);
        polygon = clip(polygon, {0, 1}, extent.y);
        return clip(polygon, {0, -1}, extent.y);
    };
    std::vector<Piece> pieces;
    float highest = std::numeric_limits<float>::lowest(),
          lowest = std::numeric_limits<float>::max();
    float highestWater = std::numeric_limits<float>::lowest();
    double covered = 0, wetArea = 0;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const auto vertex = [&](int vx, int vz) {
                return glm::vec2{vx * Terrain::spacing - half, vz * Terrain::spacing - half};
            };
            const std::array<glm::vec2, 4> positions{
                vertex(x, z), vertex(x + 1, z), vertex(x, z + 1), vertex(x + 1, z + 1)};
            for (const std::array<int, 3> indices : {std::array{0, 1, 2}, std::array{3, 2, 1}}) {
                Polygon polygon = clipFootprint({local(positions[indices[0]]),
                                                 local(positions[indices[1]]),
                                                 local(positions[indices[2]])});
                const double clippedArea = footprintArea(polygon);
                if (clippedArea <= 1e-10)
                    continue;
                const Point a = local(positions[indices[0]]), b = local(positions[indices[1]]),
                            c = local(positions[indices[2]]);
                const double ha =
                    terrain.heightAt(positions[indices[0]].x, positions[indices[0]].y);
                const double hb =
                    terrain.heightAt(positions[indices[1]].x, positions[indices[1]].y);
                const double hc =
                    terrain.heightAt(positions[indices[2]].x, positions[indices[2]].y);
                const double determinant = cross(b - a, c - a);
                const Point gradient{
                    ((hb - ha) * (c.y - a.y) - (hc - ha) * (b.y - a.y)) / determinant,
                    ((hc - ha) * (b.x - a.x) - (hb - ha) * (c.x - a.x)) / determinant};
                const double intercept = ha - glm::dot(gradient, a);
                auto candidates = polygon;
                if (circular && glm::length(gradient) > 1e-10) {
                    candidates.clear();
                    for (std::size_t i = 0; i < polygon.size(); ++i) {
                        const auto p = polygon[i], q = polygon[(i + 1) % polygon.size()], d = q - p;
                        if (glm::dot(p, p) <= extent.x * extent.x)
                            candidates.push_back(p);
                        const double aa = glm::dot(d, d), bb = 2 * glm::dot(p, d),
                                     cc = glm::dot(p, p) - extent.x * extent.x;
                        const double disc = bb * bb - 4 * aa * cc;
                        if (aa > 0 && disc >= 0)
                            for (double t : {(-bb - std::sqrt(disc)) / (2 * aa),
                                             (-bb + std::sqrt(disc)) / (2 * aa)})
                                if (t >= 0 && t <= 1)
                                    candidates.push_back(p + d * t);
                    }
                    if (glm::length(gradient) > 1e-10) {
                        for (auto direction : {gradient, -gradient}) {
                            const Point extremum = glm::normalize(direction) * extent.x;
                            bool inside = true;
                            for (std::size_t i = 0; i < polygon.size(); ++i)
                                inside &= cross(polygon[(i + 1) % polygon.size()] - polygon[i],
                                                extremum - polygon[i]) >= -1e-9;
                            if (inside)
                                candidates.push_back(extremum);
                        }
                    }
                    if (candidates.empty())
                        candidates.push_back({0, 0});
                }
                float minimum = std::numeric_limits<float>::max(),
                      maximum = std::numeric_limits<float>::lowest();
                for (auto p : candidates) {
                    const float h = static_cast<float>(intercept + glm::dot(gradient, p));
                    minimum = std::min(minimum, h);
                    maximum = std::max(maximum, h);
                }
                const auto midpoint = positions[indices[0]] +
                                      (positions[indices[1]] - positions[indices[0]]) / 3.0F +
                                      (positions[indices[2]] - positions[indices[0]]) / 3.0F;
                const float water = terrain.waterSurfaceAt(midpoint.x, midpoint.y);
                const bool wet = terrain.waterCoverageAt(midpoint.x, midpoint.y) > 0;
                const auto dry = wet ? clip(polygon, -gradient, intercept - water) : polygon;
                const double dryArea = footprintArea(dry);
                wetArea += clippedArea - dryArea;
                covered += clippedArea;
                if (wet && dryArea < clippedArea)
                    highestWater = std::max(highestWater, water);
                highest = std::max(highest, maximum);
                lowest = std::min(lowest, minimum);
                const std::array<std::uint64_t, 4> vertices{
                    static_cast<std::uint64_t>(z * Terrain::vertexCount + x),
                    static_cast<std::uint64_t>(z * Terrain::vertexCount + x + 1),
                    static_cast<std::uint64_t>((z + 1) * Terrain::vertexCount + x),
                    static_cast<std::uint64_t>((z + 1) * Terrain::vertexCount + x + 1)};
                pieces.push_back(
                    {{vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]},
                     maximum,
                     dryArea,
                     glm::length(gradient) < 1e-5});
            }
        }
    if (covered < result.footprintArea - 1e-6 * result.footprintArea)
        return result;
    const float plane = std::isfinite(highestWater)
                            ? std::max(highest, highestWater + profile.waterClearance)
                            : highest;
    result.fit = {plane, 0, {0, 0}, true};
    result.foundationDepth = plane - lowest;
    std::vector<SupportTriangle> supported;
    for (const auto& piece : pieces) {
        if (!piece.flat || std::abs(piece.maximum - plane) >= 1e-4F || piece.dryArea <= 1e-10)
            continue;
        SupportTriangle triangle{piece.vertices, {}, piece.dryArea};
        const auto point = [&](std::uint64_t vertex) {
            return local({(vertex % Terrain::vertexCount) * Terrain::spacing - half,
                          (vertex / Terrain::vertexCount) * Terrain::spacing - half});
        };
        for (std::size_t edge = 0; edge < 3; ++edge)
            triangle.edgeContact[edge] = segmentContact(point(piece.vertices[edge]),
                                                        point(piece.vertices[(edge + 1) % 3]),
                                                        extent,
                                                        circular);
        supported.push_back(triangle);
    }
    result.supportedArea = largestConnectedArea(supported);
    const bool land = profile.support == TerrainBuildingSupport::land;
    const bool water = profile.support == TerrainBuildingSupport::floating ||
                       profile.support == TerrainBuildingSupport::bed;
    if (land && result.supportedArea * 3 <= result.footprintArea * 2 + 1e-8) {
        result.failure = TerrainPlacementFailure::insufficientSupport;
        return result;
    }
    if (water && (wetArea < result.footprintArea - 1e-7 ||
                  highestWater - highest < profile.minimumWaterDepth))
        return result;
    if (profile.support == TerrainBuildingSupport::shore) {
        const double fraction = wetArea / result.footprintArea;
        if (fraction < profile.shore->minimumWetFraction - 1e-8 ||
            fraction > profile.shore->maximumWetFraction + 1e-8) {
            result.failure = TerrainPlacementFailure::shoreRequired;
            return result;
        }
        if (result.supportedArea + 1e-8 <
            result.footprintArea * profile.shore->minimumConnectedDrySupportFraction) {
            result.failure = TerrainPlacementFailure::insufficientSupport;
            return result;
        }
    }
    if (profile.support != TerrainBuildingSupport::floating &&
        result.foundationDepth > profile.maximumFoundationDepth + 1e-5F) {
        result.failure = TerrainPlacementFailure::foundationTooDeep;
        return result;
    }
    // Tile eligibility uses clipped areas, not the percentage of point samples that happen to be
    // wet.
    if (terrain.v2Landscape() && terrain.v2Landscape()->plan.hasWater()) {
        const float side = terrain.v2Landscape()->plan.recipe().cells.side;
        const float mapHalf = terrain.v2Landscape()->plan.extent() * 0.5F;
        const int firstX = static_cast<int>(std::floor((center.x - bounds + mapHalf) / side));
        const int lastX = static_cast<int>(std::floor((center.x + bounds + mapHalf) / side));
        const int firstZ = static_cast<int>(std::floor((center.y - bounds + mapHalf) / side));
        const int lastZ = static_cast<int>(std::floor((center.y + bounds + mapHalf) / side));
        for (int z = firstZ; z <= lastZ; ++z)
            for (int x = firstX; x <= lastX; ++x) {
                const glm::vec2 origin{x * side - mapHalf, z * side - mapHalf};
                const Polygon tile = clipFootprint({local(origin),
                                                    local(origin + glm::vec2{side, 0}),
                                                    local(origin + glm::vec2{side, side}),
                                                    local(origin + glm::vec2{0, side})});
                if (footprintArea(tile) <= 1e-8)
                    continue;
                bool fullWet = true;
                const int vertices = static_cast<int>(std::lround(side / Terrain::spacing));
                for (int sz = 0; sz < vertices && fullWet; ++sz)
                    for (int sx = 0; sx < vertices && fullWet; ++sx) {
                        std::array<float, 4> depth{}, coverage{};
                        const std::array<glm::ivec2, 4> corners{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
                        for (std::size_t i = 0; i < corners.size(); ++i) {
                            const auto p = origin + (glm::vec2{sx, sz} + glm::vec2{corners[i]}) *
                                                        Terrain::spacing;
                            depth[i] =
                                terrain.waterSurfaceAt(p.x, p.y) - terrain.heightAt(p.x, p.y);
                            coverage[i] = terrain.waterCoverageAt(p.x, p.y);
                        }
                        for (const std::array<int, 3> triangle :
                             {std::array{0, 1, 2}, std::array{3, 2, 1}}) {
                            const float minimum = std::min(
                                {depth[triangle[0]], depth[triangle[1]], depth[triangle[2]]});
                            const float maximum = std::max(
                                {depth[triangle[0]], depth[triangle[1]], depth[triangle[2]]});
                            const float mask = std::max({coverage[triangle[0]],
                                                         coverage[triangle[1]],
                                                         coverage[triangle[2]]});
                            fullWet &= minimum >= 0 && maximum > 0 && mask > 0;
                        }
                    }
                if (land && fullWet)
                    return result;
                if (water && !fullWet)
                    return result;
            }
    }
    bool interaction = false;
    for (int i = 0; i < 32; ++i) {
        const double a = 2 * std::numbers::pi * i / 32;
        const Point direction{std::cos(a), std::sin(a)};
        const double distance = circular
                                    ? extent.x
                                    : std::min(extent.x / std::max(std::abs(direction.x), 1e-8),
                                               extent.y / std::max(std::abs(direction.y), 1e-8));
        const auto point = world(
            direction * (distance + profile.interactionRadius + profile.interactionRange * 0.5F));
        const auto tangent = glm::normalize(world({-direction.y, direction.x}) - center);
        const auto before = point - tangent * profile.interactionRadius;
        const auto after = point + tangent * profile.interactionRadius;
        if (terrain.permitsMovementSegment(
                before, after, profile.interactionRadius, profile.interactionDomains)) {
            interaction = true;
            break;
        }
    }
    result.failure = interaction ? TerrainPlacementFailure::none
                                 : TerrainPlacementFailure::interactionUnavailable;
    return result;
}
} // namespace strategy
