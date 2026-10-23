#include "render/ScaffoldGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <utility>

namespace strategy {
bool sameScaffoldGeometry(const TerrainFoundation& a, const TerrainFoundation& b) {
    return a.sourceEntity == b.sourceEntity && a.shape == b.shape && a.center == b.center &&
           a.outerRadius == b.outerRadius && a.halfExtents == b.halfExtents &&
           a.rotationDegrees == b.rotationDegrees && a.gradient == b.gradient &&
           a.requiresSlab == b.requiresSlab;
}

ScaffoldGeometry buildScaffoldGeometry(const Terrain& terrain, const TerrainFoundation& f) {
    ScaffoldGeometry result;
    const bool circular = f.shape == FootprintShape::circle;
    const glm::vec2 extent = circular ? glm::vec2{f.outerRadius} : f.halfExtents;
    if (extent.x <= 0 || extent.y <= 0 || !f.requiresSlab)
        return result;
    const float angle = glm::radians(f.rotationDegrees), c = std::cos(angle), s = std::sin(angle);
    const auto world = [&](glm::vec2 p) {
        return glm::vec2{f.center.x + c * p.x - s * p.y, f.center.z + s * p.x + c * p.y};
    };
    const auto plane = [&](glm::vec2 p) {
        return f.center.y + glm::dot(f.gradient, p - glm::vec2{f.center.x, f.center.z});
    };
    const auto ground = [&](glm::vec2 p) {
        return f.levelsTerrain ? terrain.foundationTerrainHeightAt(f, p)
                               : terrain.heightAt(p.x, p.y);
    };
    const glm::vec3 steel{.065F, .085F, .10F}, edgeSteel{.105F, .125F, .14F},
        rust{.21F, .12F, .065F}, hazard{.58F, .39F, .07F};
    const auto face = [&](std::array<glm::vec3, 4> points, glm::vec3 color, float stage) {
        const auto normal =
            glm::normalize(glm::cross(points[1] - points[0], points[2] - points[0]));
        const auto first = static_cast<std::uint32_t>(result.vertices.size());
        const bool triangle = points[2] == points[3];
        for (std::size_t i = 0; i < (triangle ? 3U : 4U); ++i) {
            const auto p = points[i];
            result.vertices.push_back({p,
                                       normal,
                                       color,
                                       stage,
                                       terrain.waterSurfaceAt(p.x, p.z),
                                       terrain.waterCoverageAt(p.x, p.z)});
        }
        result.indices.insert(result.indices.end(), {first, first + 1, first + 2});
        if (!triangle)
            result.indices.insert(result.indices.end(), {first, first + 2, first + 3});
    };
    // Closed rectangular members with flat face normals; also used for feet and gusset plates.
    const auto beam =
        [&](glm::vec3 a, glm::vec3 b, float width, float depth, glm::vec3 color, float stage) {
            const auto delta = b - a;
            if (glm::length(delta) < .001F)
                return;
            const auto direction = glm::normalize(delta);
            const glm::vec3 reference =
                std::abs(direction.y) > .9F ? glm::vec3{1, 0, 0} : glm::vec3{0, 1, 0};
            const auto u = glm::normalize(glm::cross(direction, reference)) * width * .5F;
            const auto v = glm::normalize(glm::cross(direction, u)) * depth * .5F;
            const std::array<glm::vec3, 8> p{{a - u - v,
                                              a + u - v,
                                              a + u + v,
                                              a - u + v,
                                              b - u - v,
                                              b + u - v,
                                              b + u + v,
                                              b - u + v}};
            face({p[0], p[3], p[2], p[1]}, color, stage);
            face({p[4], p[5], p[6], p[7]}, color, stage);
            face({p[0], p[1], p[5], p[4]}, color, stage);
            face({p[1], p[2], p[6], p[5]}, color, stage);
            face({p[2], p[3], p[7], p[6]}, color, stage);
            face({p[3], p[0], p[4], p[7]}, color, stage);
        };
    const float inset = std::min(.32F, std::min(extent.x, extent.y) * .2F);
    const glm::vec2 inner = extent - glm::vec2{inset};
    std::vector<glm::vec2> nodes;
    std::vector<std::pair<std::size_t, std::size_t>> links;
    if (!circular) {
        const int nx = std::max(1, static_cast<int>(std::ceil(inner.x * 2 / 3.2F)));
        const int nz = std::max(1, static_cast<int>(std::ceil(inner.y * 2 / 3.2F)));
        for (int z = 0; z <= nz; ++z)
            for (int x = 0; x <= nx; ++x) {
                const auto index = nodes.size();
                nodes.push_back(
                    world({-inner.x + inner.x * 2 * x / nx, -inner.y + inner.y * 2 * z / nz}));
                if (x)
                    links.emplace_back(index - 1, index);
                if (z)
                    links.emplace_back(index - (nx + 1), index);
            }
    } else {
        const int n =
            std::max(8, static_cast<int>(std::ceil(glm::two_pi<float>() * inner.x / 3.2F)));
        for (int i = 0; i < n; ++i) {
            const float a = glm::two_pi<float>() * i / n;
            nodes.push_back(world({std::cos(a) * inner.x, std::sin(a) * inner.x}));
            links.emplace_back(i, (i + 1) % n);
        }
        if (inner.x > 2) {
            const auto center = nodes.size();
            nodes.push_back(world({0, 0}));
            for (int i = 0; i < n; ++i)
                links.emplace_back(i, center);
        }
    }
    struct Support {
        float bottom{}, top{}, drop{};
    };
    std::vector<Support> supports;
    supports.reserve(nodes.size());
    for (auto p : nodes) {
        const float top = plane(p) - .16F;
        const float bed = ground(p);
        const float drop = top - bed;
        supports.push_back({bed, top, drop});
        if (drop <= .025F)
            continue;
        ++result.columns;
        // Feet sit slightly into the immutable bed. All dimensions remain inside the footprint.
        const float plate = std::min(.44F, inset * 1.25F);
        beam({p.x, bed - .035F, p.y}, {p.x, bed + .055F, p.y}, plate, plate, edgeSteel, .015F);
        const int pieces = std::max(1, static_cast<int>(std::ceil(drop / 1.1F)));
        for (int i = 0; i < pieces; ++i) {
            const float low = std::lerp(bed + .03F, top, static_cast<float>(i) / pieces);
            const float high = std::lerp(bed + .03F, top, static_cast<float>(i + 1) / pieces);
            beam({p.x, low, p.y}, {p.x, high, p.y}, .16F, .16F, steel, .06F + .24F * i / pieces);
        }
        beam({p.x, top - .055F, p.y}, {p.x, top + .025F, p.y}, .28F, .28F, edgeSteel, .34F);
    }
    for (const auto [a, b] : links) {
        const auto pa = nodes[a], pb = nodes[b];
        const auto& sa = supports[a];
        const auto& sb = supports[b];
        if (std::max(sa.drop, sb.drop) <= .025F)
            continue;
        beam({pa.x, sa.top, pa.y}, {pb.x, sb.top, pb.y}, .18F, .22F, edgeSteel, .70F);
        if (sa.drop < .65F || sb.drop < .65F)
            continue;
        // Braces stay above the bed along their entire length, including cliff corners.
        const auto brace = [&](glm::vec3 from, glm::vec3 to) {
            for (int i = 0; i <= 16; ++i) {
                const auto p = glm::mix(from, to, static_cast<float>(i) / 16);
                if (p.y < ground({p.x, p.z}) + .04F)
                    return;
            }
            beam(from, to, .10F, .10F, steel, .46F);
            ++result.braces;
        };
        const auto before = result.braces;
        brace({pa.x, sa.bottom + .18F, pa.y}, {pb.x, sb.top - .12F, pb.y});
        brace({pa.x, sa.top - .12F, pa.y}, {pb.x, sb.bottom + .18F, pb.y});
        if (result.braces == before + 2) {
            const float low = sa.bottom + .18F, high = sa.top - .12F;
            const float t = (high - low) / ((sb.top - .12F - low) - (sb.bottom + .18F - high));
            const auto middle = glm::mix(pa, pb, t);
            const float midHeight = std::lerp(low, sb.top - .12F, t);
            const auto direction = glm::normalize(pb - pa) * .12F;
            beam({middle.x - direction.x, midHeight, middle.y - direction.y},
                 {middle.x + direction.x, midHeight, middle.y + direction.y},
                 .07F,
                 .23F,
                 rust,
                 .58F);
        }
    }
    // A closed, thin cap. The building's elevation does not change as it is assembled.
    std::vector<glm::vec2> rim;
    if (circular) {
        constexpr int n = 48;
        for (int i = 0; i < n; ++i) {
            const float a = glm::two_pi<float>() * i / n;
            rim.push_back(
                world({std::cos(a) * (extent.x - .025F), std::sin(a) * (extent.x - .025F)}));
        }
    } else
        for (auto p : std::array<glm::vec2, 4>{{{-extent.x + .025F, -extent.y + .025F},
                                                {extent.x - .025F, -extent.y + .025F},
                                                {extent.x - .025F, extent.y - .025F},
                                                {-extent.x + .025F, extent.y - .025F}}})
            rim.push_back(world(p));
    const auto center = world({0, 0});
    for (std::size_t i = 0; i < rim.size(); ++i) {
        const auto a = rim[i], b = rim[(i + 1) % rim.size()];
        const float ah = plane(a) - .015F, bh = plane(b) - .015F, ch = plane(center) - .015F;
        // Repeated center vertex is a triangle stored through the common quad helper.
        face({glm::vec3{a.x, ah, a.y}, {center.x, ch, center.y}, {b.x, bh, b.y}, {b.x, bh, b.y}},
             steel,
             .84F);
        face({glm::vec3{a.x, ah - .13F, a.y},
              {b.x, bh - .13F, b.y},
              {center.x, ch - .13F, center.y},
              {center.x, ch - .13F, center.y}},
             steel,
             .84F);
        face(
            {glm::vec3{a.x, ah, a.y}, {b.x, bh, b.y}, {b.x, bh - .13F, b.y}, {a.x, ah - .13F, a.y}},
            edgeSteel,
            .78F);
    }
    // Sparse corner/rim markings, rather than a bright stripe around the whole building.
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (supports[i].drop < .4F || i % 2)
            continue;
        const auto p = nodes[i];
        const float h = plane(p);
        beam({p.x, h - .28F, p.y}, {p.x, h - .12F, p.y}, .23F, .23F, hazard, .90F);
    }
    return result;
}
} // namespace strategy
