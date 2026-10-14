#pragma once

#include <cstdint>

namespace strategy {
class Terrain;

// A cached, non-playable continuation of the active map. It owns no simulation data.
class TerrainBackdrop {
  public:
    ~TerrainBackdrop();
    TerrainBackdrop() = default;
    TerrainBackdrop(const TerrainBackdrop&) = delete;
    TerrainBackdrop& operator=(const TerrainBackdrop&) = delete;
    void rebuild(const Terrain& terrain, float extent);
    void draw() const;

  private:
    std::uint32_t vao_{0}, vbo_{0}, ebo_{0}, indexCount_{0};
};
} // namespace strategy
