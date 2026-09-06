#pragma once

#include "assets/ResourceHandle.hpp"

#include <glm/vec4.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace strategy {

enum class MaterialPass { opaque, transparent, overlay };

struct RenderMaterial {
    std::string name;
    ShaderHandle shader;
    TextureHandle albedo;
    glm::vec4 tint{1.0F};
    float roughness{0.0F};
    MaterialPass pass{MaterialPass::opaque};
    bool depthTest{true};
    bool depthWrite{true};
    bool cullFace{true};
    bool blending{false};
    bool rememberedEntity{false};
};

class MaterialManager final {
  public:
    [[nodiscard]] MaterialHandle create(RenderMaterial material);
    [[nodiscard]] MaterialHandle find(const std::string& name) const;
    [[nodiscard]] const RenderMaterial& get(MaterialHandle handle) const;

  private:
    struct Slot {
        std::uint32_t generation{1};
        RenderMaterial material;
    };
    std::vector<Slot> slots_;
    std::unordered_map<std::string, MaterialHandle> handles_;
};

} // namespace strategy
