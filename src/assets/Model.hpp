#pragma once
#include "assets/ModelAsset.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <unordered_map>
#include <vector>
namespace strategy {
struct Material {
    glm::vec3 diffuse{0.75F};
    float opacity{1.0F};
    std::uint32_t baseColorTexture{0};
};
struct ModelShaderBindings {
    std::uint32_t program{0};
    std::int32_t viewProjection{-1}, model{-1}, useSkinning{-1}, bones{-1};
    std::int32_t baseColorTexture{-1}, materialDiffuse{-1}, materialOpacity{-1};
    std::int32_t hasBaseColorTexture{-1};
};
class Model final {
  public:
    explicit Model(const std::filesystem::path& path);
    explicit Model(std::shared_ptr<ModelAsset> asset);
    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    void draw(const ModelShaderBindings& shader,
              const glm::mat4& viewProjection,
              const glm::mat4& worldTransform,
              const std::string& animation = {},
              double animationSeconds = 0.0,
              std::uint32_t overrideTexture = 0) const;
    [[nodiscard]] std::size_t meshCount() const {
        return meshes_.size();
    }
    [[nodiscard]] bool hasAnimation(const std::string& name) const;

  private:
    static constexpr std::size_t maxBones = ModelAsset::maxBones;
    struct Mesh {
        std::uint32_t vao{0}, vbo{0}, ebo{0}, indexCount{0};
        glm::mat4 nodeTransform{1};
        Material material;
        bool skinned{false};
    };

  public:
    using KeyVec = ModelKeyVec;
    using KeyQuat = ModelKeyQuat;
    using Channel = ModelChannel;
    using Animation = ModelAnimation;
    using Node = ModelNode;

  private:
    std::vector<Mesh> meshes_;
    std::vector<std::uint32_t> textures_;
    std::unordered_map<std::string, std::size_t> boneIndices_;
    std::vector<glm::mat4> boneOffsets_;
    std::unordered_map<std::string, Animation> animations_;
    Node root_;
    glm::mat4 globalInverse_{1};
    void evaluateAnimation(const Animation&, double, std::array<glm::mat4, maxBones>&) const;
};
} // namespace strategy
