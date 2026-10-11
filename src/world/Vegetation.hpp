#pragma once

#include "gameplay/DefinitionRegistry.hpp"
#include "world/Entity.hpp"
#include "world/SpatialShape.hpp"

#include <cstddef>
#include <cstdint>
#include <glm/vec2.hpp>
#include <vector>

namespace strategy {

using VegetationInstanceId = std::uint64_t;

// Presentation-only deterministic scatter. These instances deliberately do not
// participate in simulation, saves, checksums, selection, collision, or pathfinding.
struct VegetationInstance {
    EntityArchetypeId archetype;
    PresentationId presentation;
    Transform transform;
    VegetationInstanceId id{0};
};

struct VegetationChunk {
    glm::ivec2 coordinate{0};
    glm::vec2 center{0.0F};
    float radius{0.0F};
    std::vector<VegetationInstance> instances;
    std::uint64_t revision{0};
};

class VegetationField final {
  public:
    VegetationField();
    explicit VegetationField(std::vector<VegetationChunk> chunks);
    VegetationField(const VegetationField& other);
    VegetationField& operator=(const VegetationField& other);
    VegetationField(VegetationField&& other) noexcept;
    VegetationField& operator=(VegetationField&& other) noexcept;
    [[nodiscard]] const std::vector<VegetationChunk>& chunks() const {
        return chunks_;
    }
    // Transient cache identity, independent of deterministic instance IDs and simulation state.
    [[nodiscard]] std::uint64_t identity() const {
        return identity_;
    }
    [[nodiscard]] std::size_t instanceCount() const;
    std::size_t clearWithin(const SpatialShape& shape);
    bool removeInstance(VegetationInstanceId id);

  private:
    std::vector<VegetationChunk> chunks_;
    std::uint64_t identity_;
};

} // namespace strategy
