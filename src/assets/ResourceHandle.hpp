#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>

namespace strategy {

template <class Tag> struct ResourceHandle {
    static constexpr std::uint32_t invalidIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index{invalidIndex};
    std::uint32_t generation{0};
    [[nodiscard]] explicit operator bool() const { return index != invalidIndex; }
    friend bool operator==(ResourceHandle, ResourceHandle) = default;
};

struct ModelResourceTag;
struct TextureResourceTag;
struct AudioResourceTag;
struct ShaderResourceTag;
struct MaterialResourceTag;
using ModelHandle = ResourceHandle<ModelResourceTag>;
using TextureHandle = ResourceHandle<TextureResourceTag>;
using AudioHandle = ResourceHandle<AudioResourceTag>;
using ShaderHandle = ResourceHandle<ShaderResourceTag>;
using MaterialHandle = ResourceHandle<MaterialResourceTag>;

enum class ResourceState : std::uint8_t { invalid, queued, importing, uploading, ready, failed };

struct AssetLoadProgress {
    std::size_t completed{0};
    std::size_t total{0};
    std::size_t failed{0};
    ResourceState activeState{ResourceState::invalid};
    [[nodiscard]] float fraction() const {
        return total == 0 ? 1.0F : static_cast<float>(completed) / static_cast<float>(total);
    }
    [[nodiscard]] bool finished() const { return completed >= total; }
};

} // namespace strategy
