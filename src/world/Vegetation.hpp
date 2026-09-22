#pragma once

#include "gameplay/DefinitionRegistry.hpp"
#include "world/Entity.hpp"
#include "world/SpatialShape.hpp"

#include <cstddef>
#include <glm/vec2.hpp>
#include <vector>

namespace strategy {

// Presentation-only deterministic scatter. These instances deliberately do not
// participate in simulation, saves, checksums, selection, collision, or pathfinding.
struct VegetationInstance {
    EntityArchetypeId archetype;
    PresentationId presentation;
    Transform transform;
};

struct VegetationChunk {
    glm::ivec2 coordinate{0};
    glm::vec2 center{0.0F};
    float radius{0.0F};
    std::vector<VegetationInstance> instances;
};

class VegetationField final {
  public:
    [[nodiscard]] const std::vector<VegetationChunk>& chunks() const { return chunks_; }
    [[nodiscard]] std::vector<VegetationChunk>& chunks() { return chunks_; }
    [[nodiscard]] std::size_t instanceCount() const;
    std::size_t clearWithin(const SpatialShape& shape);

  private:
    std::vector<VegetationChunk> chunks_;
};

} // namespace strategy
