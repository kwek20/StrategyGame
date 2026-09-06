#include "assets/ModelAsset.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <functional>
#include <stdexcept>

namespace strategy {
namespace {
glm::mat4 matrix(const aiMatrix4x4& value) {
    return {value.a1, value.b1, value.c1, value.d1, value.a2, value.b2, value.c2, value.d2,
            value.a3, value.b3, value.c3, value.d3, value.a4, value.b4, value.c4, value.d4};
}
glm::vec3 vector(const aiVector3D& value) { return {value.x, value.y, value.z}; }
glm::quat quaternion(const aiQuaternion& value) {
    return {value.w, value.x, value.y, value.z};
}

std::shared_ptr<ModelTextureAsset> decodeTexture(const aiScene& scene,
                                                 const std::filesystem::path& modelPath,
                                                 const aiString& reference) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = nullptr;
    std::vector<std::uint8_t> raw;
    if (const aiTexture* embedded = scene.GetEmbeddedTexture(reference.C_Str())) {
        if (embedded->mHeight == 0) {
            decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(embedded->pcData),
                                            static_cast<int>(embedded->mWidth),
                                            &width,
                                            &height,
                                            &channels,
                                            4);
        } else {
            width = static_cast<int>(embedded->mWidth);
            height = static_cast<int>(embedded->mHeight);
            raw.resize(static_cast<std::size_t>(width * height * 4));
            for (int index = 0; index < width * height; ++index) {
                raw[index * 4] = embedded->pcData[index].r;
                raw[index * 4 + 1] = embedded->pcData[index].g;
                raw[index * 4 + 2] = embedded->pcData[index].b;
                raw[index * 4 + 3] = embedded->pcData[index].a;
            }
        }
    } else {
        decoded = stbi_load(
            (modelPath.parent_path() / reference.C_Str()).string().c_str(),
            &width,
            &height,
            &channels,
            4);
    }
    if (decoded) {
        raw.assign(decoded, decoded + static_cast<std::size_t>(width * height * 4));
        stbi_image_free(decoded);
    }
    if (raw.empty())
        return {};
    return std::make_shared<ModelTextureAsset>(ModelTextureAsset{width, height, std::move(raw)});
}
} // namespace

std::shared_ptr<ModelAsset> importModelAsset(const std::filesystem::path& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals |
            aiProcess_LimitBoneWeights | aiProcess_ImproveCacheLocality | aiProcess_SortByPType |
            aiProcess_FlipUVs);
    if (!scene || !scene->mRootNode)
        throw std::runtime_error("Could not import model '" + path.string() +
                                 "': " + importer.GetErrorString());

    auto asset = std::make_shared<ModelAsset>();
    asset->globalInverse = glm::inverse(matrix(scene->mRootNode->mTransformation));
    std::function<ModelNode(const aiNode*)> copyNode = [&](const aiNode* node) {
        ModelNode result;
        result.name = node->mName.C_Str();
        result.transform = matrix(node->mTransformation);
        for (unsigned index = 0; index < node->mNumChildren; ++index)
            result.children.push_back(copyNode(node->mChildren[index]));
        return result;
    };
    asset->root = copyNode(scene->mRootNode);

    for (unsigned index = 0; index < scene->mNumAnimations; ++index) {
        const aiAnimation* source = scene->mAnimations[index];
        ModelAnimation animation;
        animation.duration = source->mDuration;
        animation.ticksPerSecond = source->mTicksPerSecond > 0 ? source->mTicksPerSecond : 25;
        for (unsigned channelIndex = 0; channelIndex < source->mNumChannels; ++channelIndex) {
            const aiNodeAnim* sourceChannel = source->mChannels[channelIndex];
            ModelChannel channel;
            for (unsigned key = 0; key < sourceChannel->mNumPositionKeys; ++key)
                channel.positions.push_back({sourceChannel->mPositionKeys[key].mTime,
                                             vector(sourceChannel->mPositionKeys[key].mValue)});
            for (unsigned key = 0; key < sourceChannel->mNumScalingKeys; ++key)
                channel.scales.push_back({sourceChannel->mScalingKeys[key].mTime,
                                          vector(sourceChannel->mScalingKeys[key].mValue)});
            for (unsigned key = 0; key < sourceChannel->mNumRotationKeys; ++key)
                channel.rotations.push_back({sourceChannel->mRotationKeys[key].mTime,
                                             quaternion(sourceChannel->mRotationKeys[key].mValue)});
            animation.channels.emplace(sourceChannel->mNodeName.C_Str(), std::move(channel));
        }
        asset->animations.emplace(source->mName.C_Str(), std::move(animation));
    }

    std::unordered_map<unsigned, std::shared_ptr<ModelTextureAsset>> textures;
    const auto materialFor = [&](unsigned materialIndex) {
        ModelMaterialAsset result;
        if (materialIndex >= scene->mNumMaterials)
            return result;
        const aiMaterial* material = scene->mMaterials[materialIndex];
        aiColor3D color;
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            result.diffuse = {color.r, color.g, color.b};
        material->Get(AI_MATKEY_OPACITY, result.opacity);
        if (const auto found = textures.find(materialIndex); found != textures.end()) {
            result.baseColorTexture = found->second;
        } else {
            aiString reference;
            if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &reference) != AI_SUCCESS)
                material->GetTexture(aiTextureType_DIFFUSE, 0, &reference);
            if (reference.length)
                result.baseColorTexture = decodeTexture(*scene, path, reference);
            textures.emplace(materialIndex, result.baseColorTexture);
        }
        return result;
    };

    std::function<void(const aiNode*, const glm::mat4&)> visit;
    visit = [&](const aiNode* node, const glm::mat4& parent) {
        const glm::mat4 nodeTransform = parent * matrix(node->mTransformation);
        for (unsigned meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex) {
            const aiMesh* source = scene->mMeshes[node->mMeshes[meshIndex]];
            if (!(source->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                continue;
            ModelMeshAsset mesh;
            mesh.vertices.resize(source->mNumVertices);
            mesh.nodeTransform = nodeTransform;
            mesh.skinned = source->HasBones();
            mesh.material = materialFor(source->mMaterialIndex);
            for (unsigned vertex = 0; vertex < source->mNumVertices; ++vertex) {
                mesh.vertices[vertex].position = vector(source->mVertices[vertex]);
                mesh.vertices[vertex].normal =
                    source->HasNormals() ? vector(source->mNormals[vertex]) : glm::vec3(0, 1, 0);
                if (source->HasTextureCoords(0))
                    mesh.vertices[vertex].uv = {source->mTextureCoords[0][vertex].x,
                                                source->mTextureCoords[0][vertex].y};
            }
            for (unsigned boneIndex = 0; boneIndex < source->mNumBones; ++boneIndex) {
                const aiBone* bone = source->mBones[boneIndex];
                const std::string name = bone->mName.C_Str();
                std::size_t targetIndex = 0;
                if (const auto found = asset->boneIndices.find(name);
                    found != asset->boneIndices.end()) {
                    targetIndex = found->second;
                } else {
                    targetIndex = asset->boneIndices.size();
                    if (targetIndex >= ModelAsset::maxBones)
                        continue;
                    asset->boneIndices.emplace(name, targetIndex);
                    asset->boneOffsets.push_back(matrix(bone->mOffsetMatrix));
                }
                for (unsigned weight = 0; weight < bone->mNumWeights; ++weight) {
                    ModelVertex& vertex = mesh.vertices[bone->mWeights[weight].mVertexId];
                    for (int slot = 0; slot < 4; ++slot)
                        if (vertex.bones[slot] < 0) {
                            vertex.bones[slot] = static_cast<std::int32_t>(targetIndex);
                            vertex.weights[slot] = bone->mWeights[weight].mWeight;
                            break;
                        }
                }
            }
            for (unsigned face = 0; face < source->mNumFaces; ++face)
                if (source->mFaces[face].mNumIndices == 3)
                    for (unsigned index = 0; index < 3; ++index)
                        mesh.indices.push_back(source->mFaces[face].mIndices[index]);
            asset->meshes.push_back(std::move(mesh));
        }
        for (unsigned index = 0; index < node->mNumChildren; ++index)
            visit(node->mChildren[index], nodeTransform);
    };
    visit(scene->mRootNode, glm::mat4(1));
    return asset;
}

std::shared_ptr<ModelAsset> makeMarkerModelAsset(bool failed) {
    auto asset = std::make_shared<ModelAsset>();
    ModelMeshAsset mesh;
    mesh.material.diffuse = failed ? glm::vec3{1.0F, 0.05F, 0.45F}
                                   : glm::vec3{1.0F, 0.72F, 0.08F};
    constexpr glm::vec3 positions[] = {{-0.5F, 0.0F, -0.5F}, {0.5F, 0.0F, -0.5F},
                                       {0.5F, 1.0F, -0.5F},  {-0.5F, 1.0F, -0.5F},
                                       {-0.5F, 0.0F, 0.5F},  {0.5F, 0.0F, 0.5F},
                                       {0.5F, 1.0F, 0.5F},   {-0.5F, 1.0F, 0.5F}};
    for (const glm::vec3 position : positions)
        mesh.vertices.push_back({position, glm::normalize(position + glm::vec3{0, -0.5F, 0})});
    mesh.indices = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
                    1, 2, 6, 1, 6, 5, 3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4};
    asset->meshes.push_back(std::move(mesh));
    return asset;
}

} // namespace strategy
