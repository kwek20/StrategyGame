#pragma once

#include "assets/ResourceHandle.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace strategy {

class ShaderManager final {
  public:
    ShaderManager() = default;
    ~ShaderManager();
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    [[nodiscard]] ShaderHandle load(std::string name,
                                    std::string_view vertexSource,
                                    std::string_view fragmentSource);
    [[nodiscard]] std::uint32_t program(ShaderHandle handle) const;
    [[nodiscard]] std::int32_t uniform(ShaderHandle handle, std::string_view name) const;
    void use(ShaderHandle handle) const;
    [[nodiscard]] ResourceState state(ShaderHandle handle) const;
    [[nodiscard]] const std::string& error(ShaderHandle handle) const;

  private:
    struct Slot {
        std::uint32_t generation{1};
        std::string name;
        std::uint32_t program{0};
        ResourceState state{ResourceState::invalid};
        std::string error;
        mutable std::unordered_map<std::string, std::int32_t> uniforms;
    };
    std::vector<Slot> slots_;
    std::unordered_map<std::string, ShaderHandle> handles_;
};

} // namespace strategy
