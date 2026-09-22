#include "world/Vegetation.hpp"

#include "world/SpatialShape.hpp"

#include <algorithm>

namespace strategy {

std::size_t VegetationField::instanceCount() const {
    std::size_t count = 0;
    for (const VegetationChunk& chunk : chunks_) count += chunk.instances.size();
    return count;
}

std::size_t VegetationField::clearWithin(const SpatialShape& shape) {
    std::size_t removed = 0;
    for (VegetationChunk& chunk : chunks_) {
        const auto firstRemoved = std::remove_if(
            chunk.instances.begin(), chunk.instances.end(), [&](const VegetationInstance& item) {
                const glm::vec2 position{item.transform.position.x, item.transform.position.z};
                if (signedDistance(shape, position) > 0.0F) return false;
                ++removed;
                return true;
            });
        chunk.instances.erase(firstRemoved, chunk.instances.end());
    }
    return removed;
}

} // namespace strategy
