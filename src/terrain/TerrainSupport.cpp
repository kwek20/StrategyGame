#include "terrain/TerrainSupport.hpp"

#include "terrain/SurfaceGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <vector>

namespace strategy {
namespace {
using namespace surface_geometry;
struct Piece {
    std::array<std::uint64_t, 3> vertices;
    float minimum{};
    double dryArea{};
    bool canBear{}; // Continuous footing eligibility, independent of the floor tilt cap.
    bool cliff{};
    Polygon polygon;
    Point gradient{};
    double intercept{};
    float water{};
    bool wet{};
    Polygon dry;
};
struct PreparedSurface {
    bool ready{};
    float rawRelief{};
    std::vector<Piece> pieces;
    std::vector<std::array<int, 3>> neighbors;
    double covered{}, wetArea{};
    std::vector<std::pair<glm::vec2, float>> medians;
    std::vector<std::pair<glm::vec2, TerrainPlacementResult>> bases;
    std::unordered_map<std::uint64_t, bool> cutPermission;
    FootprintFit lastFit;
    bool lastWasLeveled{};
    std::vector<Polygon> lastContacts;
    float highestWater{std::numeric_limits<float>::lowest()};
};
bool permitsCutVertex(const Terrain& terrain,
                      int x,
                      int z,
                      const TerrainPlacementProfile& profile,
                      PreparedSurface& surface) {
    const auto id = static_cast<std::uint64_t>(z) * Terrain::vertexCount + x;
    if (const auto found = surface.cutPermission.find(id); found != surface.cutPermission.end())
        return found->second;
    const glm::vec2 p{x * Terrain::spacing - terrain.worldExtent() * .5F,
                      z * Terrain::spacing - terrain.worldExtent() * .5F};
    bool allowed = terrain.waterCoverageAt(p.x, p.y) == 0 && terrain.permitsTerrainCutAt(p);
    for (const glm::vec2 d : {glm::vec2{Terrain::spacing, 0}, glm::vec2{0, Terrain::spacing}})
        allowed &= terrain.v2Landscape()->terraces.maximumCliffDrop(p - d, p + d) <=
                       profile.levelingMaximumRelief + 1e-5F &&
                   terrain.waterCoverageAt(p.x - d.x, p.y - d.y) == 0 &&
                   terrain.waterCoverageAt(p.x + d.x, p.y + d.y) == 0;
    surface.cutPermission.emplace(id, allowed);
    return allowed;
}
} // namespace
bool TerrainShoreSupport::valid() const {
    return std::isfinite(minimumWetFraction) && std::isfinite(maximumWetFraction) &&
           std::isfinite(minimumConnectedDrySupportFraction) && minimumWetFraction > 0 &&
           maximumWetFraction < 1 && minimumWetFraction <= maximumWetFraction &&
           minimumConnectedDrySupportFraction > 0 &&
           minimumConnectedDrySupportFraction + minimumWetFraction <= 1;
}
static TerrainPlacementResult evaluateSurface(const Terrain& terrain,
                                              glm::vec2 center,
                                              const TerrainFootprint& footprint,
                                              TerrainPlacementProfile profile,
                                              const TerrainFoundation* leveling = nullptr,
                                              const glm::vec2* fittedGradient = nullptr,
                                              bool requireFullSupport = false,
                                              bool attemptExcavation = false,
                                              PreparedSurface* prepared = nullptr,
                                              float repairOffset = 0,
                                              bool wantGeometry = false) {
    TerrainPlacementResult result;
    result.failure = TerrainPlacementFailure::forbiddenTerrain;
    if (!std::isfinite(profile.supportHeightTolerance) || profile.supportHeightTolerance < 0)
        return result;
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
    const int x0 = std::max(0, grid(center.x - worldBounds.x)),
              z0 = std::max(0, grid(center.y - worldBounds.y));
    const int x1 = std::min(Terrain::cellCount - 1, grid(center.x + worldBounds.x)),
              z1 = std::min(Terrain::cellCount - 1, grid(center.y + worldBounds.y));
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
    const auto extrema = [&](const Polygon& polygon, Point gradient, double intercept) {
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
                    for (double t :
                         {(-bb - std::sqrt(disc)) / (2 * aa), (-bb + std::sqrt(disc)) / (2 * aa)})
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
        return std::pair{minimum, maximum};
    };
    PreparedSurface owned;
    auto& source = prepared ? *prepared : owned;
    if (!source.ready) {
        const int width = x1 - x0 + 1, height = z1 - z0 + 1;
        std::vector<int> raster(static_cast<std::size_t>(width * height * 2), -1);
        source.pieces.reserve(raster.size());
        float highest = std::numeric_limits<float>::lowest(),
              lowest = std::numeric_limits<float>::max();
        float highestWater = std::numeric_limits<float>::lowest();
        double covered = 0, wetArea = 0;
        const auto surfaceHeight = [&](glm::vec2 p) { return terrain.heightAt(p.x, p.y); };
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const auto vertex = [&](int vx, int vz) {
                    return glm::vec2{vx * Terrain::spacing - half, vz * Terrain::spacing - half};
                };
                const std::array<glm::vec2, 4> positions{
                    vertex(x, z), vertex(x + 1, z), vertex(x, z + 1), vertex(x + 1, z + 1)};
                int triangleIndex = 0;
                for (const std::array<int, 3> indices :
                     {std::array{0, 1, 2}, std::array{3, 2, 1}}) {
                    const int slot = ((z - z0) * width + x - x0) * 2 + triangleIndex++;
                    Polygon polygon = clipFootprint({local(positions[indices[0]]),
                                                     local(positions[indices[1]]),
                                                     local(positions[indices[2]])});
                    const double clippedArea = footprintArea(polygon);
                    if (clippedArea <= 1e-10)
                        continue;
                    const Point a = local(positions[indices[0]]), b = local(positions[indices[1]]),
                                c = local(positions[indices[2]]);
                    const double ha = surfaceHeight(positions[indices[0]]);
                    const double hb = surfaceHeight(positions[indices[1]]);
                    const double hc = surfaceHeight(positions[indices[2]]);
                    const double determinant = cross(b - a, c - a);
                    const Point gradient{
                        ((hb - ha) * (c.y - a.y) - (hc - ha) * (b.y - a.y)) / determinant,
                        ((hc - ha) * (b.x - a.x) - (hb - ha) * (c.x - a.x)) / determinant};
                    const double intercept = ha - glm::dot(gradient, a);
                    const auto [minimum, maximum] = extrema(polygon, gradient, intercept);
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
                    bool cliff = false;
                    if (terrain.v2Landscape()) {
                        const auto& terraces = terrain.v2Landscape()->terraces;
                        const float limit = terraces.extent() * .5F - 1e-4F;
                        const auto worldA =
                            glm::clamp(positions[indices[0]], glm::vec2{-limit}, glm::vec2{limit});
                        const auto worldB =
                            glm::clamp(positions[indices[1]], glm::vec2{-limit}, glm::vec2{limit});
                        const auto worldC =
                            glm::clamp(positions[indices[2]], glm::vec2{-limit}, glm::vec2{limit});
                        const float side = terraces.extent() / terraces.side();
                        const auto cell = [&](glm::vec2 p) {
                            return glm::ivec2{glm::floor((p + terraces.extent() * .5F) / side)};
                        };
                        if (cell(worldA) != cell(worldB) || cell(worldA) != cell(worldC))
                            cliff = terraces.crossesCliff(worldA, worldB) ||
                                    terraces.crossesCliff(worldB, worldC) ||
                                    terraces.crossesCliff(worldC, worldA);
                    }
                    raster[slot] = static_cast<int>(source.pieces.size());
                    source.pieces.push_back(
                        {{vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]},
                         minimum,
                         dryArea,
                         !cliff || glm::length(gradient) <= std::tan(footprint.maximumTiltDegrees *
                                                                     std::numbers::pi / 180.0) +
                                                                1e-6,
                         cliff,
                         std::move(polygon),
                         gradient,
                         intercept,
                         water,
                         wet,
                         dry});
                }
            }

        source.neighbors.resize(source.pieces.size());
        for (int z = 0; z < height; ++z)
            for (int x = 0; x < width; ++x)
                for (int t = 0; t < 2; ++t) {
                    const int slot = (z * width + x) * 2 + t, index = raster[slot];
                    if (index < 0)
                        continue;
                    const auto at = [&](int nx, int nz, int nt) {
                        return nx < 0 || nz < 0 || nx >= width || nz >= height
                                   ? -1
                                   : raster[(nz * width + nx) * 2 + nt];
                    };
                    source.neighbors[index] =
                        t == 0 ? std::array{at(x, z - 1, 1), at(x, z, 1), at(x - 1, z, 1)}
                               : std::array{at(x, z + 1, 0), at(x, z, 0), at(x + 1, z, 0)};
                }
        source.covered = covered;
        source.wetArea = wetArea;
        source.highestWater = highestWater;
        source.rawRelief = highest - lowest;
        source.ready = true;
    }
    std::vector<Piece> repaired;
    if (leveling) {
        repaired = source.pieces;
        for (auto& piece : repaired) {
            std::array<Point, 3> points;
            std::array<double, 3> heights;
            for (std::size_t i = 0; i < 3; ++i) {
                const int x = static_cast<int>(piece.vertices[i] % Terrain::vertexCount);
                const int z = static_cast<int>(piece.vertices[i] / Terrain::vertexCount);
                points[i] = local({x * Terrain::spacing - half, z * Terrain::spacing - half});
                heights[i] = terrain.foundationTerrainVertexHeight(*leveling, x, z);
            }
            const auto a = points[0], b = points[1], c = points[2];
            const double determinant = cross(b - a, c - a);
            piece.gradient = {((heights[1] - heights[0]) * (c.y - a.y) -
                               (heights[2] - heights[0]) * (b.y - a.y)) /
                                  determinant,
                              ((heights[2] - heights[0]) * (b.x - a.x) -
                               (heights[1] - heights[0]) * (c.x - a.x)) /
                                  determinant};
            piece.intercept = heights[0] - glm::dot(piece.gradient, a);
            piece.canBear =
                !piece.cliff ||
                glm::length(piece.gradient) <=
                    std::tan(footprint.maximumTiltDegrees * std::numbers::pi / 180.0) + 1e-6;
            piece.dry = piece.wet
                            ? clip(piece.polygon, -piece.gradient, piece.intercept - piece.water)
                            : piece.polygon;
            piece.dryArea = footprintArea(piece.dry);
        }
    }
    const auto& pieces = leveling ? repaired : source.pieces;
    const double covered = source.covered, wetArea = source.wetArea;
    const float highestWater = source.highestWater;
    float highest = std::numeric_limits<float>::lowest(),
          lowest = std::numeric_limits<float>::max();
    if (covered < result.footprintArea - 1e-6 * result.footprintArea)
        return result;
    glm::vec2 planeGradient{0};
    if (fittedGradient)
        planeGradient = *fittedGradient;
    else if (profile.support == TerrainBuildingSupport::land && wetArea < 1e-8 &&
             profile.maximumBuildingTiltDegrees > 0) {
        std::vector<std::pair<Point, double>> gradients;
        double weight = 0;
        for (const auto& piece : pieces)
            if (piece.canBear && piece.dryArea > 0) {
                const Point worldGradient{cosine * piece.gradient.x - sine * piece.gradient.y,
                                          sine * piece.gradient.x + cosine * piece.gradient.y};
                gradients.emplace_back(worldGradient, piece.dryArea);
                weight += piece.dryArea;
            }
        // Dominant dry-surface direction: local bumps and a cliff lip cannot tip the whole floor.
        if (weight > 0) {
            const auto median = [&](int axis) {
                auto sorted = gradients;
                std::sort(sorted.begin(), sorted.end(), [axis](const auto& a, const auto& b) {
                    return a.first[axis] < b.first[axis];
                });
                double accumulated = 0;
                for (const auto& [gradient, area] : sorted) {
                    accumulated += area;
                    if (accumulated >= weight * .5)
                        return static_cast<float>(gradient[axis]);
                }
                return 0.F;
            };
            planeGradient = {median(0), median(1)};
            const float maximum =
                std::tan(std::min(profile.maximumBuildingTiltDegrees,
                                  TerrainPlacementProfile::maximumAllowedTiltDegrees) *
                         std::numbers::pi_v<float> / 180);
            const float magnitude = glm::length(planeGradient);
            if (magnitude > maximum)
                planeGradient *= maximum / magnitude;
        }
    }
    const Point localGradient{cosine * planeGradient.x + sine * planeGradient.y,
                              -sine * planeGradient.x + cosine * planeGradient.y};
    highest = std::numeric_limits<float>::lowest();
    lowest = std::numeric_limits<float>::max();
    std::vector<std::pair<float, float>> relativeBounds;
    relativeBounds.reserve(pieces.size());
    for (const auto& piece : pieces) {
        const auto [minimum, maximum] =
            extrema(piece.polygon, piece.gradient - localGradient, piece.intercept);
        highest = std::max(highest, maximum);
        lowest = std::min(lowest, minimum);
        relativeBounds.emplace_back(minimum, maximum);
    }
    bool budgetBlocked = false;
    double rejectedVolume = 0;
    const auto applyTier = [&](TerrainPlacementResult value) {
        if (value.valid() && requireFullSupport && value.groundContactArea < value.footprintArea - 1e-6)
            value.failure = TerrainPlacementFailure::insufficientSupport;
        if (budgetBlocked && !value.valid()) {
            value.failure = TerrainPlacementFailure::excavationBudget;
            value.excavationVolume = rejectedVolume;
        }
        return value;
    };
    const auto rememberBase = [&](TerrainPlacementResult value) {
        if (!leveling && !wantGeometry)
            source.bases.emplace_back(planeGradient, value);
        return applyTier(std::move(value));
    };
    const auto cachedBase =
        std::find_if(source.bases.begin(), source.bases.end(), [&](const auto& value) {
            return value.first == planeGradient;
        });
    if (!leveling && !wantGeometry && !attemptExcavation && cachedBase != source.bases.end())
        return applyTier(cachedBase->second);
    // Find an area-weighted median on the actual clipped surface, not a vertex-count median.
    // Only shallow dry land may be cut; water and canonical cliffs retain their old contract.
    if (!leveling && attemptExcavation && profile.support == TerrainBuildingSupport::land &&
        wetArea < 1e-8 && profile.levelingMaximumRelief > 0 && highest - lowest > .01F &&
        (!requireFullSupport || highest - lowest <= profile.levelingMaximumRelief + 1e-5F)) {
        const auto areaBelow = [&](double h) {
            double below = 0;
            for (std::size_t i = 0; i < pieces.size(); ++i) {
                const auto& piece = pieces[i];
                // Conservative fast paths leave near-boundary arithmetic on the exact clipper.
                if (h >= relativeBounds[i].second + 1e-6)
                    below += piece.dryArea;
                else if (h >= relativeBounds[i].first - 1e-6)
                    below += footprintArea(
                        clip(piece.polygon, piece.gradient - localGradient, h - piece.intercept));
            }
            return below;
        };
        // A median below this bound cannot reach the highest surface even after the
        // requested offset. Prove that cheaply before doing a full median bisection.
        const double minimumMedian = highest - profile.levelingMaximumCut - repairOffset;
        if (repairOffset >= 0 && minimumMedian > lowest &&
            areaBelow(minimumMedian - 1e-5) > result.footprintArea * .5 + 1e-8)
            return evaluateSurface(terrain,
                                   center,
                                   footprint,
                                   profile,
                                   nullptr,
                                   &planeGradient,
                                   requireFullSupport,
                                   false,
                                   &source,
                                   0,
                                   wantGeometry);
        const auto median =
            std::find_if(source.medians.begin(), source.medians.end(), [&](const auto& item) {
                return item.first == planeGradient;
            });
        double low = lowest, high = highest;
        if (repairOffset < 0)
            high = lowest; // The average target below does not need a median search.
        else if (median != source.medians.end())
            high = median->second;
        else {
            if (areaBelow(low) >= result.footprintArea * .5)
                high = low;
            for (int iteration = 0; iteration < 22 && high > low + 1e-6; ++iteration) {
                const double h = (low + high) * .5;
                if (areaBelow(h) < result.footprintArea * .5)
                    low = h;
                else
                    high = h;
            }
            source.medians.emplace_back(planeGradient, static_cast<float>(high));
        }
        // Aim at the area-weighted ground height first. Median-based offsets remain
        // fallbacks when the average cannot meet the existing cut/support budgets.
        if (repairOffset < 0) {
            double moment = 0;
            for (const auto& piece : pieces) {
                Point centroid{};
                double weight = 0;
                for (std::size_t i = 1; i + 1 < piece.polygon.size(); ++i) {
                    const auto a = piece.polygon[0], b = piece.polygon[i], c = piece.polygon[i + 1];
                    const double triangleArea = std::abs(cross(b - a, c - a)) * .5;
                    centroid += (a + b + c) * (triangleArea / 3);
                    weight += triangleArea;
                }
                if (weight > 0)
                    moment += footprintArea(piece.polygon) *
                              (piece.intercept +
                               glm::dot(piece.gradient - localGradient, centroid / weight));
            }
            high = moment / covered;
            repairOffset = 0;
        }
        TerrainFoundation proposed{
            0,
            {center.x,
             static_cast<float>(std::min<double>(high + repairOffset, highest)),
             center.y},
            footprint,
            planeGradient};
        proposed.levelsTerrain = true;
        proposed.levelingBlendWidth = profile.levelingBlendWidth;
        proposed.levelingMaximumCut = profile.levelingMaximumCut;
        // Fill is confined to shallow residual relief. Larger slopes retain the existing
        // cut/scaffold fallback and cannot acquire a broad artificial embankment.
        proposed.levelingMaximumFill = highest - lowest <= profile.levelingMaximumRelief + 1e-5F
                                           ? profile.levelingMaximumFill
                                           : 0;
        const double reach = bounds + profile.levelingBlendWidth + Terrain::spacing * 2;
        const int minX = std::max(0, grid(center.x - reach));
        const int maxX = std::min(Terrain::cellCount, grid(center.x + reach) + 1);
        const int minZ = std::max(0, grid(center.y - reach));
        const int maxZ = std::min(Terrain::cellCount, grid(center.y + reach) + 1);
        bool allowed = (highest - proposed.center.y > 1e-3F ||
                        (proposed.levelingMaximumFill > 0 && proposed.center.y - lowest > 1e-3F)) &&
                       highest - proposed.center.y <= profile.levelingMaximumCut + 1e-5F;
        double volume = 0, fill = 0;
        // Sum vertex cuts over the complete feathered region. On the regular triangular raster
        // this equals the integrated removed volume (the outer, unmodified ring contributes zero).
        for (int z = minZ; z <= maxZ && allowed; ++z)
            for (int x = minX; x <= maxX && allowed; ++x) {
                const glm::vec2 p{x * Terrain::spacing - half, z * Terrain::spacing - half};
                const float before = terrain.vertexHeight(x, z);
                const float after = terrain.foundationTerrainVertexHeight(proposed, x, z);
                const float delta = before - after;
                if (std::abs(delta) <= 1e-6F)
                    continue;
                allowed = delta <= profile.levelingMaximumCut + 1e-5F &&
                          -delta <= proposed.levelingMaximumFill + 1e-5F &&
                          permitsCutVertex(terrain, x, z, profile, source);
                volume += std::max(delta, 0.F) * Terrain::spacing * Terrain::spacing;
                fill += std::max(-delta, 0.F) * Terrain::spacing * Terrain::spacing;
            }
        const bool overBudget =
            volume > result.footprintArea * profile.levelingMaximumAverageCut + 1e-8 ||
            ((profile.levelingMaximumFill > 0 || profile.levelingMaximumAverageEarthwork > 0) &&
             volume + fill > result.footprintArea * profile.levelingMaximumAverageEarthwork + 1e-8);
        if (allowed && volume + fill > 1e-6 && overBudget) {
            // An over-budget candidate cannot be accepted regardless of its support.
            // Keep the ungraded fallback and avoid rebuilding/clipping the repaired surface.
            budgetBlocked = true;
            rejectedVolume = volume;
        }
        if (allowed && volume + fill > 1e-6 && !overBudget) {
            auto adjusted = evaluateSurface(terrain,
                                            center,
                                            footprint,
                                            profile,
                                            &proposed,
                                            &planeGradient,
                                            requireFullSupport,
                                            false,
                                            &source,
                                            0,
                                            wantGeometry);
            if (adjusted.valid() && std::abs(adjusted.fit.height - proposed.center.y) < 1e-4F) {
                adjusted.leveling = proposed;
                adjusted.excavationVolume = volume;
                adjusted.fillVolume = fill;
                // Diagnostic signal only; acceptance uses actual relief, cut depth and volume.
                double mean = 0;
                for (const auto& piece : pieces) {
                    Point centroid{};
                    for (auto p : piece.polygon)
                        centroid += p;
                    centroid /= static_cast<double>(piece.polygon.size());
                    mean += footprintArea(piece.polygon) *
                            (piece.intercept + glm::dot(piece.gradient - localGradient, centroid));
                }
                adjusted.averageDistanceBelowHighest = highest - static_cast<float>(mean / covered);
                return adjusted;
            }
        }
    }
    if (!leveling && !wantGeometry && cachedBase != source.bases.end())
        return applyTier(cachedBase->second);
    const float plane = std::isfinite(highestWater)
                            ? std::max(highest, highestWater + profile.waterClearance)
                            : highest;
    result.fit = {plane,
                  std::atan(glm::length(planeGradient)) * 180 / std::numbers::pi_v<float>,
                  planeGradient,
                  true};
    result.foundationDepth = plane - lowest;
    std::vector<SupportTriangle> supported;
    supported.reserve(pieces.size());
    auto geometry = wantGeometry ? std::make_shared<PlacementGeometry>() : nullptr;
    if (geometry) {
        geometry->unsupported.reserve(pieces.size());
        geometry->supported.reserve(pieces.size());
    }
    source.lastFit = result.fit;
    source.lastWasLeveled = leveling != nullptr;
    source.lastContacts.resize(pieces.size());
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        const auto& piece = pieces[index];
        const Point relative = piece.gradient - localGradient;
        const double contactThreshold = plane - profile.supportHeightTolerance;
        // Scaffold bearing extends down a continuous dry hillside, but does not bridge a
        // canonical cliff component or count submerged ground as connected dry support.
        const double threshold = plane - (profile.support == TerrainBuildingSupport::land
                                              ? profile.maximumFoundationDepth
                                              : profile.supportHeightTolerance);
        const bool wholeBand = relativeBounds[index].first >= threshold + 1e-6;
        Polygon contact;
        if (piece.canBear && relativeBounds[index].second >= threshold - 1e-6)
            contact =
                wholeBand ? piece.dry : clip(piece.dry, -relative, piece.intercept - threshold);
        source.lastContacts[index] = contact;
        SupportTriangle triangle{piece.vertices, {}, footprintArea(contact)};
        if (piece.canBear && relativeBounds[index].second >= contactThreshold - 1e-6) {
            const auto direct =
                relativeBounds[index].first >= contactThreshold + 1e-6
                    ? piece.dry
                    : clip(piece.dry, -relative, piece.intercept - contactThreshold);
            triangle.contactArea = footprintArea(direct);
            result.groundContactArea += triangle.contactArea;
        }
        if (triangle.area <= 1e-10)
            source.lastContacts[index].clear();
        const auto point = [&](std::uint64_t vertex) {
            return local({(vertex % Terrain::vertexCount) * Terrain::spacing - half,
                          (vertex / Terrain::vertexCount) * Terrain::spacing - half});
        };
        for (std::size_t edge = 0; edge < 3 && triangle.area > 1e-10; ++edge) {
            auto a = point(piece.vertices[edge]), b = point(piece.vertices[(edge + 1) % 3]);
            // Intervals are oriented by vertex ID, so neighboring triangles compare the same t.
            if (piece.vertices[edge] > piece.vertices[(edge + 1) % 3])
                std::swap(a, b);
            auto interval = segmentInterval(a, b, extent, circular);
            const auto above = [&](Point gradient, double intercept, double threshold) {
                const double start = intercept + glm::dot(gradient, a) - threshold;
                const double delta = glm::dot(gradient, b - a);
                if (std::abs(delta) < 1e-12) {
                    if (start < -1e-8)
                        interval = {1, 0};
                } else if (delta > 0)
                    interval.low = std::max(interval.low, -start / delta);
                else
                    interval.high = std::min(interval.high, -start / delta);
            };
            if (!wholeBand)
                above(relative, piece.intercept, threshold);
            if (piece.wet)
                above(piece.gradient, piece.intercept, piece.water);
            triangle.intervals[edge] = interval;
            triangle.edgeContact[edge] = (interval.high - interval.low) * glm::length(b - a) > 1e-8;
        }
        supported.push_back(triangle);
    }
    std::vector<unsigned char> bearing(pieces.size());
    result.supportedArea = largestConnectedArea(
        supported, source.neighbors, profile.support == TerrainBuildingSupport::land, bearing);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (!bearing[i])
            source.lastContacts[i].clear();
        if (geometry)
            appendPlacementGeometry(*geometry, pieces[i].polygon, source.lastContacts[i]);
    }
    result.geometry = std::move(geometry);
    const bool land = profile.support == TerrainBuildingSupport::land;
    const bool water = profile.support == TerrainBuildingSupport::floating ||
                       profile.support == TerrainBuildingSupport::bed;
    if (land && result.supportedArea + 1e-8 <
                    result.footprintArea * TerrainPlacementProfile::minimumDrySupportFraction) {
        result.failure = TerrainPlacementFailure::insufficientSupport;
        return rememberBase(std::move(result));
    }
    if (water && (wetArea < result.footprintArea - 1e-7 ||
                  highestWater - highest < profile.minimumWaterDepth))
        return rememberBase(std::move(result));
    if (profile.support == TerrainBuildingSupport::shore) {
        const double fraction = wetArea / result.footprintArea;
        if (fraction < profile.shore->minimumWetFraction - 1e-8 ||
            fraction > profile.shore->maximumWetFraction + 1e-8) {
            result.failure = TerrainPlacementFailure::shoreRequired;
            return rememberBase(std::move(result));
        }
        if (result.supportedArea + 1e-8 <
            result.footprintArea * profile.shore->minimumConnectedDrySupportFraction) {
            result.failure = TerrainPlacementFailure::insufficientSupport;
            return rememberBase(std::move(result));
        }
    }
    if (profile.support != TerrainBuildingSupport::floating &&
        result.foundationDepth > profile.maximumFoundationDepth + 1e-5F) {
        result.failure = TerrainPlacementFailure::foundationTooDeep;
        return rememberBase(std::move(result));
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
                    return rememberBase(std::move(result));
                if (water && !fullWet)
                    return rememberBase(std::move(result));
            }
    }
    result.failure = TerrainPlacementFailure::none;
    return rememberBase(std::move(result));
}
TerrainPlacementResult TerrainSupport::evaluate(const Terrain& terrain,
                                                glm::vec2 center,
                                                const TerrainFootprint& footprint,
                                                TerrainPlacementProfile profile) {
    PreparedSurface surface;
    const auto finish = [&](TerrainPlacementResult result) {
        if (result.fit.valid) {
            result.strategy = result.requiresScaffold()
                                  ? TerrainPlacementResult::Strategy::scaffold
                                  : (result.leveling ? TerrainPlacementResult::Strategy::excavation
                                                     : TerrainPlacementResult::Strategy::fit);
            if (surface.lastFit.valid && surface.lastFit.height == result.fit.height &&
                surface.lastFit.gradient == result.fit.gradient &&
                surface.lastWasLeveled == result.leveling.has_value()) {
                auto geometry = std::make_shared<PlacementGeometry>();
                geometry->unsupported.reserve(surface.pieces.size());
                geometry->supported.reserve(surface.pieces.size());
                for (std::size_t i = 0; i < surface.pieces.size(); ++i) {
                    appendPlacementGeometry(
                        *geometry, surface.pieces[i].polygon, surface.lastContacts[i]);
                }
                result.geometry = std::move(geometry);
            } else {
                auto detail = evaluateSurface(terrain,
                                              center,
                                              footprint,
                                              profile,
                                              result.leveling ? &*result.leveling : nullptr,
                                              &result.fit.gradient,
                                              false,
                                              false,
                                              &surface,
                                              0,
                                              true);
                result.geometry = std::move(detail.geometry);
            }
        }
        return result;
    };
    // Floating/shore contracts keep their water-level plane. Legacy opt-out profiles retain
    // their original single-pass evaluation.
    if (profile.support != TerrainBuildingSupport::land ||
        (profile.maximumBuildingTiltDegrees == 0 && profile.levelingMaximumRelief == 0))
        return evaluateSurface(
            terrain, center, footprint, profile, nullptr, nullptr, false, false, &surface, 0, true);
    auto fit = evaluateSurface(
        terrain, center, footprint, profile, nullptr, nullptr, true, false, &surface);
    if (!fit.fit.valid || (fit.valid() && surface.rawRelief < .01F))
        return finish(std::move(fit));
    // Conservative impossibility proof: immutable steep triangles cannot become support at
    // any plane. Count every other dry triangle in full, ignoring height bands. If even that
    // optimistic connected area is insufficient, searching more planes cannot rescue the site.
    if (surface.wetArea < 1e-8 && std::any_of(
                                      surface.pieces.begin(),
                                      surface.pieces.end(),
                                      [](const auto& p) { return !p.canBear; })) {
        std::vector<SupportTriangle> potential;
        potential.reserve(surface.pieces.size());
        const double angle = footprint.rotationDegrees * std::numbers::pi / 180;
        const auto local = [&](std::uint64_t vertex) {
            const Point p{(vertex % Terrain::vertexCount) * Terrain::spacing -
                              terrain.worldExtent() * .5F - center.x,
                          (vertex / Terrain::vertexCount) * Terrain::spacing -
                              terrain.worldExtent() * .5F - center.y};
            return Point{std::cos(angle) * p.x + std::sin(angle) * p.y,
                         -std::sin(angle) * p.x + std::cos(angle) * p.y};
        };
        const bool circle = footprint.shape == FootprintShape::circle;
        const Point extent = circle ? Point{footprint.radius} : Point{footprint.halfExtents};
        for (const auto& piece : surface.pieces) {
            bool possible = piece.canBear;
            if (!possible && profile.levelingMaximumRelief > 0)
                for (const auto id : piece.vertices)
                    possible |= permitsCutVertex(terrain,
                                                 static_cast<int>(id % Terrain::vertexCount),
                                                 static_cast<int>(id / Terrain::vertexCount),
                                                 profile,
                                                 surface);
            SupportTriangle triangle{piece.vertices, {}, possible ? piece.dryArea : 0};
            if (possible)
                for (std::size_t edge = 0; edge < 3; ++edge)
                    triangle.edgeContact[edge] =
                        segmentContact(local(piece.vertices[edge]),
                                       local(piece.vertices[(edge + 1) % 3]),
                                       extent,
                                       circle);
            potential.push_back(triangle);
        }
        if (largestConnectedArea(potential, surface.neighbors) + 1e-8 < fit.footprintArea * TerrainPlacementProfile::minimumDrySupportFraction)
            return finish(std::move(fit));
    }
    // Keep the dominant fit first. A mean direction recovers distributed gentle ramps that
    // the component-wise median misses; zero and a blend cover local asymmetric bumps.
    std::vector<glm::vec2> candidates{fit.fit.gradient};
    const auto add = [&](glm::vec2 gradient) {
        const float limit = std::tan(std::min(profile.maximumBuildingTiltDegrees,
                                              TerrainPlacementProfile::maximumAllowedTiltDegrees) *
                                     std::numbers::pi_v<float> / 180);
        const float length = glm::length(gradient);
        if (length > limit && length > 0)
            gradient *= limit / length;
        if (std::none_of(candidates.begin(), candidates.end(), [&](auto other) {
                return glm::length(other - gradient) < 1e-5F;
            }))
            candidates.push_back(gradient);
    };
    if (surface.wetArea < 1e-8 && profile.maximumBuildingTiltDegrees > 0) {
        // Fit the largest connected gentle patch. A cliff's lower shelf cannot pull this
        // plane toward itself, and flat cell cores cannot hide the overall hillside trend.
        std::vector<unsigned char> visited(surface.pieces.size());
        std::vector<int> largest, pending;
        double largestArea = 0;
        for (std::size_t start = 0; start < surface.pieces.size(); ++start) {
            if (visited[start] || !surface.pieces[start].canBear ||
                surface.pieces[start].dryArea <= 1e-10)
                continue;
            pending.clear();
            pending.push_back(static_cast<int>(start));
            visited[start] = 1;
            double area = 0;
            for (std::size_t index = 0; index < pending.size(); ++index) {
                const int current = pending[index];
                area += surface.pieces[current].dryArea;
                for (const int neighbor : surface.neighbors[current])
                    if (neighbor >= 0 && !visited[neighbor] && surface.pieces[neighbor].canBear &&
                        surface.pieces[neighbor].dryArea > 1e-10) {
                        visited[neighbor] = 1;
                        pending.push_back(neighbor);
                    }
            }
            if (area > largestArea) {
                largestArea = area;
                largest = pending;
            }
        }
        Point centerOfPatch{};
        double meanHeight = 0;
        const auto centroid = [](const Piece& piece) {
            Point point{};
            for (auto p : piece.dry)
                point += p;
            return point / static_cast<double>(piece.dry.size());
        };
        for (const int index : largest) {
            const auto& piece = surface.pieces[index];
            const auto p = centroid(piece);
            centerOfPatch += p * piece.dryArea;
            meanHeight += (piece.intercept + glm::dot(piece.gradient, p)) * piece.dryArea;
        }
        const double angle = footprint.rotationDegrees * std::numbers::pi / 180.0;
        const auto worldGradient = [&](Point p) {
            return glm::vec2{std::cos(angle) * p.x - std::sin(angle) * p.y,
                             std::sin(angle) * p.x + std::cos(angle) * p.y};
        };
        if (largestArea > 0) {
            centerOfPatch /= largestArea;
            meanHeight /= largestArea;
            double xx = 0, zz = 0, xz = 0, xh = 0, zh = 0;
            for (const int index : largest) {
                const auto& piece = surface.pieces[index];
                const auto p = centroid(piece);
                const auto delta = p - centerOfPatch;
                const double dh = piece.intercept + glm::dot(piece.gradient, p) - meanHeight;
                xx += delta.x * delta.x * piece.dryArea;
                zz += delta.y * delta.y * piece.dryArea;
                xz += delta.x * delta.y * piece.dryArea;
                xh += delta.x * dh * piece.dryArea;
                zh += delta.y * dh * piece.dryArea;
            }
            const double determinant = xx * zz - xz * xz;
            if (determinant > 1e-10)
                add(worldGradient(
                    {(xh * zz - zh * xz) / determinant, (zh * xx - xh * xz) / determinant}));
        }
        Point mean{};
        double weight = 0;
        for (const auto& piece : surface.pieces)
            if (piece.canBear && piece.dryArea > 0) {
                mean += piece.gradient * piece.dryArea;
                weight += piece.dryArea;
            }
        if (weight > 0) {
            mean /= weight;
            add({std::cos(angle) * mean.x - std::sin(angle) * mean.y,
                 std::sin(angle) * mean.x + std::cos(angle) * mean.y});
        }
        add({0, 0});
        if (candidates.size() > 1)
            add((candidates[0] + candidates[1]) * .5F);
    }
    // Prefer the direction with the smallest residual relief, rather than accepting a
    // horizontal median-gradient plane before considering the hillside's overall trend.
    const double angle = footprint.rotationDegrees * std::numbers::pi / 180;
    const auto relief = [&](glm::vec2 gradient) {
        const Point local{std::cos(angle) * gradient.x + std::sin(angle) * gradient.y,
                          -std::sin(angle) * gradient.x + std::cos(angle) * gradient.y};
        double low = std::numeric_limits<double>::max(), high = -low;
        for (const auto& piece : surface.pieces) {
            for (const auto p : piece.polygon) {
                const double height = piece.intercept + glm::dot(piece.gradient - local, p);
                low = std::min(low, height);
                high = std::max(high, height);
            }
        }
        return high - low;
    };
    std::vector<std::pair<glm::vec2, double>> ranked;
    for (const auto gradient : candidates)
        ranked.emplace_back(gradient, relief(gradient));
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    for (std::size_t i = 0; i < candidates.size(); ++i)
        candidates[i] = ranked[i].first;
    const float extent = footprint.shape == FootprintShape::circle
                             ? footprint.radius
                             : glm::length(footprint.halfExtents);
    const float bestPossibleRelief =
        surface.rawRelief -
        2 * extent *
            std::tan(std::min(profile.maximumBuildingTiltDegrees,
                              TerrainPlacementProfile::maximumAllowedTiltDegrees) *
                     std::numbers::pi_v<float> / 180);
    std::optional<TerrainPlacementResult> uncut;
    for (const auto& [gradient, residual] : ranked) {
        if (bestPossibleRelief > profile.supportHeightTolerance + 1e-4F)
            break;
        // Rectangle extrema are exact on the clipped polygon. A larger residual cannot
        // give full floor contact, so defer it directly to repair/scaffold evaluation.
        // Disk ranking uses conservative box extrema; do not apply this proof to disks.
        if (footprint.shape == FootprintShape::rectangle &&
            residual > profile.supportHeightTolerance + 1e-4F)
            continue;
        auto alternative = evaluateSurface(
            terrain, center, footprint, profile, nullptr, &gradient, true, false, &surface);
        if (alternative.valid()) {
            if (!alternative.requiresScaffold() || relief(gradient) <= .01 || profile.levelingMaximumRelief <= 0 ||
                surface.wetArea >= 1e-8)
                return finish(std::move(alternative));
            if (!uncut)
                uncut = std::move(alternative);
        }
    }
    const std::array<float, 4> offsets{
        -1, 0, profile.supportHeightTolerance * .4F, profile.supportHeightTolerance * .8F};
    TerrainPlacementResult failure = fit;
    std::optional<TerrainPlacementResult> scaffoldFallback;
    // A repaired candidate already measures connected support. Classify that single result
    // as excavation or scaffold instead of solving it once for each acceptance tier.
    for (const auto& [gradient, residual] : ranked) {
        // Once a supported fallback exists, worse rectangular fits cannot avoid a frame
        // if fill is disabled by relief and the remaining cut plus base depth is insufficient.
        const float noFrameRelief = std::max(profile.levelingMaximumRelief,
                                             profile.levelingMaximumCut + TerrainPlacementProfile::baseHeightTolerance);
        if (scaffoldFallback && footprint.shape == FootprintShape::rectangle &&
            residual > noFrameRelief + 1e-4F)
            break;
        for (const auto offset : offsets) {
            auto supported = evaluateSurface(terrain,
                                             center,
                                             footprint,
                                             profile,
                                             nullptr,
                                             &gradient,
                                             false,
                                             profile.levelingMaximumRelief > 0,
                                             &surface,
                                             offset);
            supported.strategy =
                supported.groundContactArea >= supported.footprintArea - 1e-6
                    ? (supported.leveling ? TerrainPlacementResult::Strategy::excavation
                                          : TerrainPlacementResult::Strategy::fit)
                    : TerrainPlacementResult::Strategy::scaffold;
            if (supported.valid()) {
                if (!supported.requiresScaffold() || profile.levelingMaximumRelief <= 0 ||
                    surface.wetArea >= 1e-8 ||
                    (supported.leveling && profile.levelingMaximumFill <= 0))
                    return finish(std::move(supported));
                // Broad bearing makes an uncut raised floor valid, but it must not hide
                // a lower, budget-compliant repair intercept later in the bounded search.
                if (!scaffoldFallback ||
                    supported.foundationDepth + .01F < scaffoldFallback->foundationDepth)
                    scaffoldFallback = std::move(supported);
                continue;
            }
            if (supported.supportedArea > failure.supportedArea ||
                (supported.failure == TerrainPlacementFailure::excavationBudget &&
                 failure.failure != TerrainPlacementFailure::excavationBudget))
                failure = std::move(supported);
            if (profile.levelingMaximumRelief == 0 || surface.wetArea >= 1e-8)
                break;
        }
        // Once the best viable direction has a complete plan, do not try worse fits
        // merely to find optional earthwork. All repair heights for this direction
        // have already been checked; alternate directions still rescue invalid sites.
        if (scaffoldFallback && (profile.levelingMaximumFill <= 0 ||
                                 bestPossibleRelief > profile.levelingMaximumRelief + 1e-4F))
            return finish(scaffoldFallback->leveling || !uncut ? std::move(*scaffoldFallback)
                                                               : std::move(*uncut));
    }
    if (uncut &&
        (!scaffoldFallback || uncut->foundationDepth + .01F < scaffoldFallback->foundationDepth))
        return finish(std::move(*uncut));
    if (scaffoldFallback)
        return finish(std::move(*scaffoldFallback));
    return finish(std::move(failure));
}
} // namespace strategy
