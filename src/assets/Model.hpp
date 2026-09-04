#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
namespace strategy {
struct Material { glm::vec3 diffuse{0.75F}; float opacity{1.0F}; std::uint32_t baseColorTexture{0}; };
class Model final {
public:
 explicit Model(const std::filesystem::path& path); ~Model();
 Model(const Model&)=delete; Model& operator=(const Model&)=delete;
 void draw(std::uint32_t program,const glm::mat4& viewProjection,const glm::mat4& worldTransform,const std::string& animation={},double animationSeconds=0.0) const;
 [[nodiscard]] std::size_t meshCount() const{return meshes_.size();}
 [[nodiscard]] bool hasAnimation(const std::string& name) const;
private:
 static constexpr std::size_t maxBones=100;
 struct Mesh {std::uint32_t vao{0},vbo{0},ebo{0},indexCount{0};glm::mat4 nodeTransform{1};Material material;bool skinned{false};};
public:
 struct KeyVec {double time{0};glm::vec3 value{0};}; struct KeyQuat {double time{0};glm::quat value{};};
 struct Channel {std::vector<KeyVec> positions,scales;std::vector<KeyQuat> rotations;};
 struct Animation {double duration{0},ticksPerSecond{25};std::unordered_map<std::string,Channel> channels;};
 struct Node {std::string name;glm::mat4 transform{1};std::vector<Node> children;};
private:
 std::vector<Mesh> meshes_; std::vector<std::uint32_t> textures_;
 std::unordered_map<std::string,std::size_t> boneIndices_; std::vector<glm::mat4> boneOffsets_;
 std::unordered_map<std::string,Animation> animations_; Node root_; glm::mat4 globalInverse_{1};
 void evaluateAnimation(const Animation&,double,std::array<glm::mat4,maxBones>&) const;
};
}
