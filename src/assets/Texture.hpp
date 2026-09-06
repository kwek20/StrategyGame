#pragma once

#include "assets/ModelAsset.hpp"

#include <cstdint>
#include <memory>

namespace strategy {

class Texture final {
  public:
    explicit Texture(std::shared_ptr<ModelTextureAsset> asset);
    Texture(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    void bind(std::uint32_t unit = 0) const;
    [[nodiscard]] std::uint32_t id() const { return id_; }

  private:
    std::uint32_t id_{0};
    void upload(const ModelTextureAsset& asset);
};

} // namespace strategy
