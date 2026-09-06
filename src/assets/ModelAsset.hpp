#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace strategy {

struct ModelVertex {
    glm::vec3 position{0}, normal{0, 1, 0};
    glm::vec2 uv{0};
    std::array<std::int32_t, 4> bones{-1, -1, -1, -1};
    glm::vec4 weights{0};
};

struct ModelTextureAsset {
    int width{0}, height{0};
    std::vector<std::uint8_t> rgba;
};

struct ModelMaterialAsset {
    glm::vec3 diffuse{0.75F};
    float opacity{1.0F};
    std::shared_ptr<ModelTextureAsset> baseColorTexture;
};

struct ModelMeshAsset {
    std::vector<ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    glm::mat4 nodeTransform{1};
    ModelMaterialAsset material;
    bool skinned{false};
};

struct ModelKeyVec {
    double time{0};
    glm::vec3 value{0};
};
struct ModelKeyQuat {
    double time{0};
    glm::quat value{};
};
struct ModelChannel {
    std::vector<ModelKeyVec> positions, scales;
    std::vector<ModelKeyQuat> rotations;
};
struct ModelAnimation {
    double duration{0}, ticksPerSecond{25};
    std::unordered_map<std::string, ModelChannel> channels;
};
struct ModelNode {
    std::string name;
    glm::mat4 transform{1};
    std::vector<ModelNode> children;
};

// Immutable CPU-side result of importing a model. It owns no OpenGL objects and
// can therefore be created safely by an asset worker.
struct ModelAsset {
    static constexpr std::size_t maxBones = 100;
    std::vector<ModelMeshAsset> meshes;
    std::unordered_map<std::string, std::size_t> boneIndices;
    std::vector<glm::mat4> boneOffsets;
    std::unordered_map<std::string, ModelAnimation> animations;
    ModelNode root;
    glm::mat4 globalInverse{1};
};

[[nodiscard]] std::shared_ptr<ModelAsset> importModelAsset(const std::filesystem::path& path);
[[nodiscard]] std::shared_ptr<ModelAsset> makeMarkerModelAsset(bool failed);
[[nodiscard]] std::shared_ptr<ModelTextureAsset>
importTextureAsset(const std::filesystem::path& path);

} // namespace strategy
