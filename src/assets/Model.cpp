#include "assets/Model.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
namespace strategy {
namespace {
template <class K> std::size_t before(const std::vector<K>& k, double t) {
    if (k.size() < 2)
        return 0;
    for (std::size_t i = 0; i + 1 < k.size(); ++i)
        if (t < k[i + 1].time)
            return i;
    return k.size() - 2;
}
glm::vec3 sample(const std::vector<Model::KeyVec>& k, double t, glm::vec3 f) {
    if (k.empty())
        return f;
    if (k.size() == 1)
        return k[0].value;
    auto i = before(k, t);
    double d = k[i + 1].time - k[i].time;
    float a = d > 0 ? float((t - k[i].time) / d) : 0;
    return glm::mix(k[i].value, k[i + 1].value, std::clamp(a, 0.F, 1.F));
}
glm::quat sample(const std::vector<Model::KeyQuat>& k, double t, glm::quat f) {
    if (k.empty())
        return f;
    if (k.size() == 1)
        return k[0].value;
    auto i = before(k, t);
    double d = k[i + 1].time - k[i].time;
    float a = d > 0 ? float((t - k[i].time) / d) : 0;
    return glm::normalize(glm::slerp(k[i].value, k[i + 1].value, std::clamp(a, 0.F, 1.F)));
}
std::uint32_t upload(const ModelTextureAsset& texture) {
    if (texture.rgba.empty() || texture.width <= 0 || texture.height <= 0)
        return 0;
    std::uint32_t id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_SRGB8_ALPHA8,
                 texture.width,
                 texture.height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 texture.rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return id;
}
} // namespace
Model::Model(const std::filesystem::path& path)
    : Model(importModelAsset(path)) {}

Model::Model(std::shared_ptr<ModelAsset> asset) {
    if (!asset)
        return;
    boneIndices_ = std::move(asset->boneIndices);
    boneOffsets_ = std::move(asset->boneOffsets);
    animations_ = std::move(asset->animations);
    root_ = std::move(asset->root);
    globalInverse_ = asset->globalInverse;

    std::unordered_map<const ModelTextureAsset*, std::uint32_t> uploadedTextures;
    for (ModelMeshAsset& source : asset->meshes) {
        Mesh mesh;
        mesh.indexCount = static_cast<std::uint32_t>(source.indices.size());
        mesh.nodeTransform = source.nodeTransform;
        mesh.skinned = source.skinned;
        mesh.material.diffuse = source.material.diffuse;
        mesh.material.opacity = source.material.opacity;
        if (source.material.baseColorTexture) {
            const ModelTextureAsset* key = source.material.baseColorTexture.get();
            if (const auto found = uploadedTextures.find(key); found != uploadedTextures.end()) {
                mesh.material.baseColorTexture = found->second;
            } else {
                mesh.material.baseColorTexture = upload(*source.material.baseColorTexture);
                uploadedTextures.emplace(key, mesh.material.baseColorTexture);
                if (mesh.material.baseColorTexture)
                    textures_.push_back(mesh.material.baseColorTexture);
            }
        }
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ebo);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(source.vertices.size() * sizeof(ModelVertex)),
                     source.vertices.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(source.indices.size() * sizeof(std::uint32_t)),
                     source.indices.data(),
                     GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(ModelVertex),
                              (void*)offsetof(ModelVertex, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(ModelVertex),
                              (void*)offsetof(ModelVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, uv));
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(
            3, 4, GL_INT, sizeof(ModelVertex), (void*)offsetof(ModelVertex, bones));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(
            4, 4, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, weights));
        glBindVertexArray(0);
        meshes_.push_back(mesh);
    }
}

Model::~Model() {
    for (const auto& m : meshes_) {
        glDeleteBuffers(1, &m.ebo);
        glDeleteBuffers(1, &m.vbo);
        glDeleteVertexArrays(1, &m.vao);
    }
    for (auto t : textures_)
        glDeleteTextures(1, &t);
}
bool Model::hasAnimation(const std::string& n) const {
    return animations_.contains(n);
}
void Model::evaluateAnimation(const Animation& a,
                              double seconds,
                              std::array<glm::mat4, maxBones>& out) const {
    double time = a.duration > 0 ? std::fmod(seconds * a.ticksPerSecond, a.duration) : 0;
    std::function<void(const Node&, const glm::mat4&)> visit = [&](const Node& n,
                                                                   const glm::mat4& p) {
        glm::mat4 local = n.transform;
        if (auto i = a.channels.find(n.name); i != a.channels.end()) {
            glm::vec3 tr{local[3]}, sc{glm::length(glm::vec3(local[0])),
                                       glm::length(glm::vec3(local[1])),
                                       glm::length(glm::vec3(local[2]))};
            glm::mat3 rm(local);
            rm[0] /= sc.x;
            rm[1] /= sc.y;
            rm[2] /= sc.z;
            local = glm::translate(glm::mat4(1), sample(i->second.positions, time, tr)) *
                    glm::mat4_cast(sample(i->second.rotations, time, glm::quat_cast(rm))) *
                    glm::scale(glm::mat4(1), sample(i->second.scales, time, sc));
        }
        glm::mat4 global = p * local;
        if (auto b = boneIndices_.find(n.name); b != boneIndices_.end())
            out[b->second] = globalInverse_ * global * boneOffsets_[b->second];
        for (const auto& c : n.children)
            visit(c, global);
    };
    visit(root_, glm::mat4(1));
}
void Model::draw(const ModelShaderBindings& shader,
                 const glm::mat4& vp,
                 const glm::mat4& world,
                 const std::string& animation,
                 double seconds,
                 std::uint32_t overrideTexture) const {
    std::array<glm::mat4, maxBones> bones;
    bones.fill(glm::mat4(1));
    bool animated = false;
    if (auto a = animations_.find(animation); a != animations_.end()) {
        evaluateAnimation(a->second, seconds, bones);
        animated = true;
    }
    glUseProgram(shader.program);
    glUniformMatrix4fv(shader.viewProjection, 1, GL_FALSE, glm::value_ptr(vp));
    // Static meshes do not use the bone array. Uploading the full declared array for
    // those draws is invalid on drivers that optimize the inactive array away. For an
    // animated model, upload only the matrices that can actually be referenced by its
    // vertex data.
    if (animated && !boneOffsets_.empty()) {
        if (shader.bones >= 0) {
            const auto boneCount = static_cast<GLsizei>(std::min(boneOffsets_.size(), maxBones));
            glUniformMatrix4fv(shader.bones, boneCount, GL_FALSE, glm::value_ptr(bones[0]));
        }
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(shader.baseColorTexture, 0);
    for (const auto& m : meshes_) {
        glm::mat4 model = world * (m.skinned ? glm::mat4(1) : m.nodeTransform);
        glUniformMatrix4fv(shader.model, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(shader.useSkinning, animated && m.skinned);
        glUniform3fv(shader.materialDiffuse, 1, glm::value_ptr(m.material.diffuse));
        glUniform1f(shader.materialOpacity, m.material.opacity);
        const std::uint32_t texture = overrideTexture != 0 ? overrideTexture
                                                           : m.material.baseColorTexture;
        glUniform1i(shader.hasBaseColorTexture, texture != 0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindVertexArray(m.vao);
        glDrawElements(GL_TRIANGLES, GLsizei(m.indexCount), GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
} // namespace strategy
