#include "render/TerrainBackdrop.hpp"

#include "terrain/Terrain.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <glad/glad.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <vector>

namespace strategy {
namespace {
struct BackdropVertex {
    glm::vec3 position, normal, color;
    glm::vec4 weights;
};
} // namespace

TerrainBackdrop::~TerrainBackdrop() {
    if (ebo_)
        glDeleteBuffers(1, &ebo_);
    if (vbo_)
        glDeleteBuffers(1, &vbo_);
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
}

void TerrainBackdrop::rebuild(const Terrain& terrain, float extent) {
    indexCount_ = 0;
    if (terrain.mode() != TerrainMode::v2)
        return;
    const float half = extent * 0.5F;
    const int segments = static_cast<int>(std::ceil(extent / Terrain::spacing));
    const int perimeter = segments * 4;
    constexpr std::array distances{0.0F, 0.75F, 2.0F, 5.0F, 10.0F, 20.0F, 36.0F, 60.0F, 96.0F};
    std::vector<BackdropVertex> vertices;
    vertices.reserve(perimeter * distances.size());
    struct EdgeSample {
        glm::vec2 direction;
        float height, broadHeight;
        glm::vec3 normal, color;
        glm::vec4 weights;
    };
    std::vector<EdgeSample> edgeSamples;
    edgeSamples.reserve(perimeter);
    const auto sample = [&](glm::vec2 p) {
        p = glm::clamp(p, glm::vec2{-half}, glm::vec2{half});
        return terrain.heightAt(p.x, p.y);
    };
    constexpr float terrainHalf = Terrain::cellCount * Terrain::spacing * 0.5F;
    for (int side = 0; side < 4; ++side) {
        for (int step = 0; step < segments; ++step) {
            const float along = -1.0F + 2.0F * step / segments;
            const glm::vec2 direction = side == 0   ? glm::vec2{-1, along}
                                        : side == 1 ? glm::vec2{along, 1}
                                        : side == 2 ? glm::vec2{1, -along}
                                                    : glm::vec2{-along, -1};
            const glm::vec2 edge = direction * half;
            // Smooth around the perimeter rather than carrying every terrace outward.
            const glm::vec2 tangent = side % 2 == 0 ? glm::vec2{0, 1} : glm::vec2{1, 0};
            float broadHeight = 0.0F, totalWeight = 0.0F;
            for (int offset = -8; offset <= 8; ++offset) {
                const float weight = 9.0F - std::abs(static_cast<float>(offset));
                broadHeight += sample(edge + tangent * (offset * 3.0F)) * weight;
                totalWeight += weight;
            }
            const int x = static_cast<int>(std::round((edge.x + terrainHalf) / Terrain::spacing));
            const int z = static_cast<int>(std::round((edge.y + terrainHalf) / Terrain::spacing));
            edgeSamples.push_back({direction,
                                   sample(edge),
                                   broadHeight / totalWeight,
                                   terrain.normalAt(x, z),
                                   terrain.colorAt(edge.x, edge.y),
                                   terrain.materialWeightsAt(edge.x, edge.y)});
        }
    }
    for (float distance : distances) {
        for (const auto& edge : edgeSamples) {
            const glm::vec2 position = edge.direction * (half + distance);
            const float blend = glm::smoothstep(0.0F, 10.0F, distance);
            // The first ring exactly matches the heightfield, including cliff corners.
            // Beyond it, low relief carries the landscape into atmospheric distance.
            const float relief =
                std::sin(position.x * 0.045F) * std::cos(position.y * 0.038F) * 2.0F;
            const float height = glm::mix(edge.height, edge.broadHeight + relief, blend);
            vertices.push_back(
                {{position.x, height, position.y}, {0, 1, 0}, edge.color, edge.weights});
        }
    }
    std::vector<std::uint32_t> indices;
    indices.reserve((distances.size() - 1) * perimeter * 6);
    for (std::size_t ring = 0; ring + 1 < distances.size(); ++ring) {
        for (int point = 0; point < perimeter; ++point) {
            const auto a = static_cast<std::uint32_t>(ring * perimeter + point);
            const auto b = a + perimeter;
            const auto c = static_cast<std::uint32_t>(ring * perimeter + (point + 1) % perimeter);
            const auto d = c + perimeter;
            indices.insert(indices.end(), {a, b, c, c, b, d});
        }
    }
    for (auto& vertex : vertices)
        vertex.normal = glm::vec3{0};
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        auto& a = vertices[indices[i]];
        auto& b = vertices[indices[i + 1]];
        auto& c = vertices[indices[i + 2]];
        const glm::vec3 normal = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += normal;
        b.normal += normal;
        c.normal += normal;
    }
    for (auto& vertex : vertices)
        vertex.normal = glm::normalize(vertex.normal);
    // Match the inner edge lighting as well as its positions to avoid a visible join.
    for (int point = 0; point < perimeter; ++point)
        vertices[point].normal = edgeSamples[point].normal;
    if (!vao_) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER, vertices.size() * sizeof(BackdropVertex), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(std::uint32_t),
                 indices.data(),
                 GL_STATIC_DRAW);
    constexpr std::array offsets{offsetof(BackdropVertex, position),
                                 offsetof(BackdropVertex, normal),
                                 offsetof(BackdropVertex, color),
                                 offsetof(BackdropVertex, weights)};
    for (GLuint attribute = 0; attribute < offsets.size(); ++attribute) {
        glEnableVertexAttribArray(attribute);
        glVertexAttribPointer(attribute,
                              attribute == 3 ? 4 : 3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(BackdropVertex),
                              reinterpret_cast<void*>(offsets[attribute]));
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    indexCount_ = static_cast<std::uint32_t>(indices.size());
}

void TerrainBackdrop::draw() const {
    if (!indexCount_)
        return;
    // Disabled semantic attributes use zero: the backdrop is dry and has no traversal data.
    glVertexAttrib1f(4, 0);
    glVertexAttrib1f(5, 0);
    glVertexAttrib1f(6, 0);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
} // namespace strategy
