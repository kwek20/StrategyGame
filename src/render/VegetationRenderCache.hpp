#pragma once

#include "assets/ResourceManager.hpp"
#include "world/Vegetation.hpp"

#include <deque>
#include <functional>
#include <map>

namespace strategy {

struct VegetationCacheStats {
    std::size_t rebuiltChunks{0};
    std::size_t uploadedBatches{0};
    std::size_t submittedInstances{0};
};

// Render-thread-owned buffers. Changes to source data are tracked by field identity and
// chunk revision; no pointers into the source instance vectors survive a rebuild.
// Each presentation owns one buffer with reserved chunk ranges. Removal changes only the
// affected ranges; adding/replacing source data requires a new field identity.
class VegetationRenderCache final {
  public:
    VegetationRenderCache() = default;
    VegetationRenderCache(const VegetationRenderCache&) = delete;
    VegetationRenderCache& operator=(const VegetationRenderCache&) = delete;
    struct Batch {
        ModelHandle handle;
        const Model* model{nullptr};
        float baseY{0};
        std::uint32_t buffer{0};
        std::size_t count{0};
        std::uint32_t first{0};
        std::size_t capacity{0};
    };
    using PrepareTransform =
        std::function<glm::mat4(const VegetationInstance&, const EntityDefinition*, float)>;

    void beginFrame(const VegetationField& field);
    const std::map<std::string, Batch>& prepareChunk(std::size_t index,
                                                     const VegetationChunk& source,
                                                     const ResourceManager& resources,
                                                     const PrepareTransform& prepare);
    void invalidateTerrainRegion(glm::vec2 minimum, glm::vec2 maximum);
    void clear();
    void recordSubmitted(std::size_t count) {
        stats_.submittedInstances += count;
    }
    [[nodiscard]] const VegetationCacheStats& stats() const {
        return stats_;
    }

  private:
    struct Storage {
        std::uint32_t buffer{0};
        std::size_t capacity{0};
        Storage() = default;
        ~Storage();
        Storage(const Storage&) = delete;
        Storage& operator=(const Storage&) = delete;
    };
    struct Chunk {
        glm::vec2 center{0};
        float radius{0};
        std::uint64_t revision{0};
        bool dirty{true};
        std::map<std::string, Batch> batches;
    };
    std::uint64_t fieldIdentity_{0};
    std::deque<Chunk> chunks_;
    std::map<std::string, Storage> storage_;
    VegetationCacheStats stats_;
};

} // namespace strategy
