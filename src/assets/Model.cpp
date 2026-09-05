#include "assets/Model.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <stb_image.h>
#include <stdexcept>
namespace strategy {
namespace {
struct Vertex {
    glm::vec3 position, normal;
    glm::vec2 uv{0};
    std::array<std::int32_t, 4> bones{-1, -1, -1, -1};
    glm::vec4 weights{0};
};
glm::mat4 matrix(const aiMatrix4x4& v) {
    return {v.a1,
            v.b1,
            v.c1,
            v.d1,
            v.a2,
            v.b2,
            v.c2,
            v.d2,
            v.a3,
            v.b3,
            v.c3,
            v.d3,
            v.a4,
            v.b4,
            v.c4,
            v.d4};
}
glm::vec3 vector(const aiVector3D& v) {
    return {v.x, v.y, v.z};
}
glm::quat quaternion(const aiQuaternion& q) {
    return {q.w, q.x, q.y, q.z};
}
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
std::uint32_t upload(const unsigned char* p, int w, int h) {
    if (!p || w <= 0 || h <= 0)
        return 0;
    std::uint32_t id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return id;
}
} // namespace
Model::Model(const std::filesystem::path& path) {
    Assimp::Importer importer;
    const aiScene* s = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals |
            aiProcess_LimitBoneWeights | aiProcess_ImproveCacheLocality | aiProcess_SortByPType |
            aiProcess_FlipUVs);
    if (!s || !s->mRootNode)
        throw std::runtime_error("Could not import model '" + path.string() +
                                 "': " + importer.GetErrorString());
    globalInverse_ = glm::inverse(matrix(s->mRootNode->mTransformation));
    const aiTexture* e = nullptr;
    std::function<Node(const aiNode*)> copy = [&](const aiNode* n) {
        Node r;
        r.name = n->mName.C_Str();
        r.transform = matrix(n->mTransformation);
        for (unsigned i = 0; i < n->mNumChildren; ++i)
            r.children.push_back(copy(n->mChildren[i]));
        return r;
    };
    root_ = copy(s->mRootNode);
    for (unsigned a = 0; a < s->mNumAnimations; ++a) {
        const aiAnimation* sa = s->mAnimations[a];
        Animation an;
        an.duration = sa->mDuration;
        an.ticksPerSecond = sa->mTicksPerSecond > 0 ? sa->mTicksPerSecond : 25;
        for (unsigned c = 0; c < sa->mNumChannels; ++c) {
            const aiNodeAnim* sc = sa->mChannels[c];
            Channel ch;
            for (unsigned k = 0; k < sc->mNumPositionKeys; ++k)
                ch.positions.push_back(
                    {sc->mPositionKeys[k].mTime, vector(sc->mPositionKeys[k].mValue)});
            for (unsigned k = 0; k < sc->mNumScalingKeys; ++k)
                ch.scales.push_back(
                    {sc->mScalingKeys[k].mTime, vector(sc->mScalingKeys[k].mValue)});
            for (unsigned k = 0; k < sc->mNumRotationKeys; ++k)
                ch.rotations.push_back(
                    {sc->mRotationKeys[k].mTime, quaternion(sc->mRotationKeys[k].mValue)});
            an.channels.emplace(sc->mNodeName.C_Str(), std::move(ch));
        }
        animations_.emplace(sa->mName.C_Str(), std::move(an));
    }
    std::unordered_map<unsigned, std::uint32_t> materialTextures;
    auto textureFor = [&](unsigned index) {
        if (auto i = materialTextures.find(index); i != materialTextures.end())
            return i->second;
        aiString ref;
        const aiMaterial* m = s->mMaterials[index];
        if (m->GetTexture(aiTextureType_BASE_COLOR, 0, &ref) != AI_SUCCESS)
            m->GetTexture(aiTextureType_DIFFUSE, 0, &ref);
        std::uint32_t id = 0;
        if (ref.length) {
            int w = 0, h = 0, n = 0;
            unsigned char* p = nullptr;
            if (const aiTexture* e = s->GetEmbeddedTexture(ref.C_Str())) {
                if (e->mHeight == 0)
                    p = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(e->pcData), int(e->mWidth), &w, &h, &n, 4);
                else {
                    w = int(e->mWidth);
                    h = int(e->mHeight);
                    p = new unsigned char[std::size_t(w * h * 4)];
                    for (int q = 0; q < w * h; ++q) {
                        p[q * 4] = e->pcData[q].r;
                        p[q * 4 + 1] = e->pcData[q].g;
                        p[q * 4 + 2] = e->pcData[q].b;
                        p[q * 4 + 3] = e->pcData[q].a;
                    }
                }
            } else
                p = stbi_load((path.parent_path() / ref.C_Str()).string().c_str(), &w, &h, &n, 4);
            id = upload(p, w, h);
            if (e = s->GetEmbeddedTexture(ref.C_Str()); e && e->mHeight)
                delete[] p;
            else
                stbi_image_free(p);
            if (id)
                textures_.push_back(id);
        }
        materialTextures[index] = id;
        return id;
    };
    std::function<void(const aiNode*, const glm::mat4&)> visit;
    visit = [&](const aiNode* n, const glm::mat4& parent) {
        glm::mat4 nt = parent * matrix(n->mTransformation);
        for (unsigned mi = 0; mi < n->mNumMeshes; ++mi) {
            const aiMesh* src = s->mMeshes[n->mMeshes[mi]];
            if (!(src->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                continue;
            std::vector<Vertex> v(src->mNumVertices);
            std::vector<std::uint32_t> ix;
            for (unsigned q = 0; q < src->mNumVertices; ++q) {
                v[q].position = vector(src->mVertices[q]);
                v[q].normal = src->HasNormals() ? vector(src->mNormals[q]) : glm::vec3(0, 1, 0);
                if (src->HasTextureCoords(0))
                    v[q].uv = {src->mTextureCoords[0][q].x, src->mTextureCoords[0][q].y};
            }
            for (unsigned b = 0; b < src->mNumBones; ++b) {
                const aiBone* bone = src->mBones[b];
                std::string name = bone->mName.C_Str();
                std::size_t bi;
                if (auto f = boneIndices_.find(name); f != boneIndices_.end())
                    bi = f->second;
                else {
                    bi = boneIndices_.size();
                    if (bi >= maxBones)
                        continue;
                    boneIndices_[name] = bi;
                    boneOffsets_.push_back(matrix(bone->mOffsetMatrix));
                }
                for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                    Vertex& vert = v[bone->mWeights[w].mVertexId];
                    for (int slot = 0; slot < 4; ++slot)
                        if (vert.bones[slot] < 0) {
                            vert.bones[slot] = int(bi);
                            vert.weights[slot] = bone->mWeights[w].mWeight;
                            break;
                        }
                }
            }
            for (unsigned f = 0; f < src->mNumFaces; ++f)
                if (src->mFaces[f].mNumIndices == 3)
                    for (unsigned j = 0; j < 3; ++j)
                        ix.push_back(src->mFaces[f].mIndices[j]);
            Mesh mesh;
            mesh.indexCount = std::uint32_t(ix.size());
            mesh.nodeTransform = nt;
            mesh.skinned = src->HasBones();
            if (src->mMaterialIndex < s->mNumMaterials) {
                const aiMaterial* m = s->mMaterials[src->mMaterialIndex];
                aiColor3D c;
                if (m->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS)
                    mesh.material.diffuse = {c.r, c.g, c.b};
                m->Get(AI_MATKEY_OPACITY, mesh.material.opacity);
                mesh.material.baseColorTexture = textureFor(src->mMaterialIndex);
            }
            glGenVertexArrays(1, &mesh.vao);
            glGenBuffers(1, &mesh.vbo);
            glGenBuffers(1, &mesh.ebo);
            glBindVertexArray(mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
            glBufferData(
                GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(Vertex)), v.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         GLsizeiptr(ix.size() * sizeof(std::uint32_t)),
                         ix.data(),
                         GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(
                0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(
                1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(
                2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
            glEnableVertexAttribArray(3);
            glVertexAttribIPointer(3, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, bones));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(
                4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, weights));
            glBindVertexArray(0);
            meshes_.push_back(mesh);
        }
        for (unsigned c = 0; c < n->mNumChildren; ++c)
            visit(n->mChildren[c], nt);
    };
    visit(s->mRootNode, glm::mat4(1));
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
void Model::draw(std::uint32_t prog,
                 const glm::mat4& vp,
                 const glm::mat4& world,
                 const std::string& animation,
                 double seconds) const {
    std::array<glm::mat4, maxBones> bones;
    bones.fill(glm::mat4(1));
    bool animated = false;
    if (auto a = animations_.find(animation); a != animations_.end()) {
        evaluateAnimation(a->second, seconds, bones);
        animated = true;
    }
    glUseProgram(prog);
    glUniformMatrix4fv(
        glGetUniformLocation(prog, "viewProjection"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniformMatrix4fv(
        glGetUniformLocation(prog, "bones"), GLsizei(maxBones), GL_FALSE, glm::value_ptr(bones[0]));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(prog, "baseColorTexture"), 0);
    for (const auto& m : meshes_) {
        glm::mat4 model = world * (m.skinned ? glm::mat4(1) : m.nodeTransform);
        glUniformMatrix4fv(glGetUniformLocation(prog, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(glGetUniformLocation(prog, "useSkinning"), animated && m.skinned);
        glUniform3fv(
            glGetUniformLocation(prog, "materialDiffuse"), 1, glm::value_ptr(m.material.diffuse));
        glUniform1f(glGetUniformLocation(prog, "materialOpacity"), m.material.opacity);
        glUniform1i(glGetUniformLocation(prog, "hasBaseColorTexture"),
                    m.material.baseColorTexture != 0);
        glBindTexture(GL_TEXTURE_2D, m.material.baseColorTexture);
        glBindVertexArray(m.vao);
        glDrawElements(GL_TRIANGLES, GLsizei(m.indexCount), GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
} // namespace strategy
