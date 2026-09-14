#pragma once

#include "assets/ParticleEffectDefinitions.hpp"
#include "assets/ResourceHandle.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace strategy {

class ParticleSystem;
class ShaderManager;
class ResourceManager;
struct CameraView;

class ParticleRenderer final {
  public:
    ParticleRenderer(ShaderManager& shaders, ResourceManager& resources);
    ~ParticleRenderer();
    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    void draw(const ParticleSystem& particles,
              const ParticleEffectCatalogue& catalogue,
              const CameraView& camera) const;

  private:
    ShaderManager* shaders_{nullptr};
    ResourceManager* resources_{nullptr};
    ShaderHandle program_;
    std::uint32_t vao_{0};
    std::uint32_t vbo_{0};
    mutable std::unordered_map<std::string, TextureHandle> textures_;
};

} // namespace strategy
