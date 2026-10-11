#include "world/Vegetation.hpp"

#include "world/SpatialShape.hpp"

#include <algorithm>
#include <atomic>
#include <utility>

namespace strategy {
namespace {
std::uint64_t nextIdentity() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

VegetationField::VegetationField()
    : identity_(nextIdentity()) {}

VegetationField::VegetationField(std::vector<VegetationChunk> chunks)
    : chunks_(std::move(chunks))
    , identity_(nextIdentity()) {
    VegetationInstanceId next = 1;
    for (auto& chunk : chunks_) {
        chunk.revision = 0;
        for (auto& instance : chunk.instances)
            instance.id = next++;
    }
}

VegetationField::VegetationField(const VegetationField& other)
    : chunks_(other.chunks_)
    , identity_(nextIdentity()) {}

VegetationField& VegetationField::operator=(const VegetationField& other) {
    if (this != &other) {
        chunks_ = other.chunks_;
        identity_ = nextIdentity();
    }
    return *this;
}

VegetationField::VegetationField(VegetationField&& other) noexcept
    : chunks_(std::move(other.chunks_))
    , identity_(nextIdentity()) {
    other.identity_ = nextIdentity();
}

VegetationField& VegetationField::operator=(VegetationField&& other) noexcept {
    if (this != &other) {
        chunks_ = std::move(other.chunks_);
        identity_ = nextIdentity();
        other.identity_ = nextIdentity();
    }
    return *this;
}

std::size_t VegetationField::instanceCount() const {
    std::size_t count = 0;
    for (const VegetationChunk& chunk : chunks_)
        count += chunk.instances.size();
    return count;
}

std::size_t VegetationField::clearWithin(const SpatialShape& shape) {
    std::size_t removed = 0;
    for (VegetationChunk& chunk : chunks_) {
        const auto firstRemoved = std::remove_if(
            chunk.instances.begin(), chunk.instances.end(), [&](const VegetationInstance& item) {
                const glm::vec2 position{item.transform.position.x, item.transform.position.z};
                if (signedDistance(shape, position) > 0.0F)
                    return false;
                ++removed;
                return true;
            });
        if (firstRemoved != chunk.instances.end()) {
            chunk.instances.erase(firstRemoved, chunk.instances.end());
            ++chunk.revision;
        }
    }
    return removed;
}

bool VegetationField::removeInstance(VegetationInstanceId id) {
    for (auto& chunk : chunks_) {
        const auto found = std::find_if(chunk.instances.begin(),
                                        chunk.instances.end(),
                                        [id](const auto& instance) { return instance.id == id; });
        if (found == chunk.instances.end())
            continue;
        chunk.instances.erase(found);
        ++chunk.revision;
        return true;
    }
    return false;
}

} // namespace strategy
