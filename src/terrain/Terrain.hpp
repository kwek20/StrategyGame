#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace strategy {

class Terrain final {
public:
    static constexpr int cellCount = 512;
    static constexpr int vertexCount = cellCount + 1;
    static constexpr int chunkCellCount = 64;
    static constexpr int chunksPerSide = cellCount / chunkCellCount;
    static constexpr float spacing = 0.375F;
    static constexpr float heightScale = 16.0F;

    explicit Terrain(std::uint32_t seed = 0x5EED1234U);

    [[nodiscard]] float normalizedHeight(int x, int z) const;
    [[nodiscard]] float vertexHeight(int x, int z) const;
    [[nodiscard]] float heightAt(float worldX, float worldZ) const;
    [[nodiscard]] glm::vec3 normalAt(int x, int z) const;
    [[nodiscard]] glm::vec3 colorAt(float normalizedHeight) const;
    [[nodiscard]] float worldExtent() const;

private:
    std::vector<float> heights_;

    void generate(std::uint32_t seed);
    [[nodiscard]] static float valueNoise(float x, float z, std::uint32_t seed);
    [[nodiscard]] static float fractalNoise(float x, float z, std::uint32_t seed,
                                            int octaves, float persistence);
};

} // namespace strategy
