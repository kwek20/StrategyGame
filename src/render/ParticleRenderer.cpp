#include "render/ParticleRenderer.hpp"

#include "particles/ParticleSystem.hpp"
#include "assets/ResourceManager.hpp"
#include "assets/Texture.hpp"
#include "render/CameraView.hpp"
#include "render/RenderPass.hpp"
#include "render/ShaderManager.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include <glad/glad.h>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace strategy {
namespace {

struct ParticleVertex {
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec4 color;
};

struct ParticleBatch {
    ParticleBlendMode blend{ParticleBlendMode::alpha};
    std::uint32_t texture{0};
    std::vector<ParticleVertex> vertices;
};

void setBlend(ParticleBlendMode mode) {
    switch (mode) {
    case ParticleBlendMode::additive: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
    case ParticleBlendMode::premultiplied: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
    case ParticleBlendMode::alpha: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
    }
}

} // namespace

ParticleRenderer::ParticleRenderer(ShaderManager& shaders, ResourceManager& resources)
    : shaders_(&shaders), resources_(&resources) {
    program_ = shaders.loadFiles("particles", "assets/shaders/particle.vert",
                                 "assets/shaders/particle.frag");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex),
                          reinterpret_cast<void*>(offsetof(ParticleVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex),
                          reinterpret_cast<void*>(offsetof(ParticleVertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex),
                          reinterpret_cast<void*>(offsetof(ParticleVertex, color)));
    glBindVertexArray(0);
}

ParticleRenderer::~ParticleRenderer() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void ParticleRenderer::draw(const ParticleSystem& particles,
                            const ParticleEffectCatalogue& catalogue,
                            const CameraView& camera) const {
    if (particles.particles().empty()) return;
    RenderPass pass(RenderPassKind::world);
    shaders_->use(program_);
    glUniformMatrix4fv(shaders_->uniform(program_, "viewProjection"), 1, GL_FALSE,
                       glm::value_ptr(camera.viewProjection()));
    glBindVertexArray(vao_);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    const glm::vec3 cameraForward = glm::normalize(camera.target - camera.position);
    glm::vec3 cameraRight = glm::cross(cameraForward, glm::vec3{0, 1, 0});
    if (glm::length(cameraRight) < 0.001F) cameraRight = {1, 0, 0};
    else cameraRight = glm::normalize(cameraRight);
    const glm::vec3 cameraUp = glm::normalize(glm::cross(cameraRight, cameraForward));
    constexpr std::array<glm::vec2, 6> corners{{{-1, -1}, {1, -1}, {1, 1},
                                                {-1, -1}, {1, 1}, {-1, 1}}};
    std::vector<ParticleBatch> batches;
    for (const Particle& particle : particles.particles()) {
        const ParticleEffectDefinition* effect = catalogue.effect(particle.effect);
        if (!effect || glm::distance(camera.position, particle.position) > effect->maximumDistance)
            continue;
        const std::string& textureId = effect->render.texture.value;
        auto texture = textures_.find(textureId);
        if (texture == textures_.end())
            texture = textures_.emplace(textureId, resources_->requestTexture(textureId)).first;
        const Texture* loaded = resources_->textureOrMarker(texture->second);
        if (!loaded) continue;
        auto batch = std::find_if(batches.begin(), batches.end(), [&](const ParticleBatch& candidate) {
            return candidate.blend == effect->render.blend && candidate.texture == loaded->id();
        });
        if (batch == batches.end()) {
            batches.push_back({effect->render.blend, loaded->id(), {}});
            batch = std::prev(batches.end());
            batch->vertices.reserve(particles.particles().size() * 6);
        }
            glm::vec3 right = cameraRight;
            glm::vec3 up = cameraUp;
            if (effect->render.billboard == ParticleBillboardMode::vertical) {
                right = cameraRight;
                up = {0, 1, 0};
            } else if (effect->render.billboard == ParticleBillboardMode::velocity &&
                       glm::length(particle.velocity) > 0.001F) {
                up = glm::normalize(particle.velocity);
                right = glm::cross(up, cameraForward);
                if (glm::length(right) < 0.001F) right = cameraRight;
                else right = glm::normalize(right);
            }
            for (glm::vec2 corner : corners) {
                const glm::vec3 position = particle.position +
                    (right * corner.x + up * corner.y) * particle.size * 0.5F;
                batch->vertices.push_back({position, corner * 0.5F + 0.5F, particle.color});
            }
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(shaders_->uniform(program_, "particleTexture"), 0);
    // Alpha first, additive last. Stable sorting keeps texture batching repeatable.
    std::stable_sort(batches.begin(), batches.end(), [](const ParticleBatch& lhs,
                                                        const ParticleBatch& rhs) {
        const auto rank = [](ParticleBlendMode mode) {
            if (mode == ParticleBlendMode::alpha) return 0;
            if (mode == ParticleBlendMode::premultiplied) return 1;
            return 2;
        };
        return rank(lhs.blend) < rank(rhs.blend);
    });
    for (const ParticleBatch& batch : batches) {
        if (batch.vertices.empty()) continue;
        setBlend(batch.blend);
        glUniform1i(shaders_->uniform(program_, "premultiplyAlpha"),
                    batch.blend == ParticleBlendMode::premultiplied ? 1 : 0);
        glBindTexture(GL_TEXTURE_2D, batch.texture);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(batch.vertices.size() * sizeof(ParticleVertex)),
                     batch.vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(0);
}

} // namespace strategy
