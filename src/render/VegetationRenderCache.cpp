#include "render/VegetationRenderCache.hpp"

#include <glad/glad.h>
#include <glm/geometric.hpp>
#include <stdexcept>

namespace strategy {

VegetationRenderCache::Storage::~Storage() {
    if (buffer)
        glDeleteBuffers(1, &buffer);
}

void VegetationRenderCache::clear() {
    chunks_.clear();
    storage_.clear();
    fieldIdentity_ = 0;
    stats_ = {};
}

void VegetationRenderCache::beginFrame(const VegetationField& field, bool resetStats) {
    if (resetStats)
        resetFrameStats();
    if (fieldIdentity_ == field.identity())
        return;
    const auto previousStats = stats_;
    clear();
    stats_ = previousStats;
    chunks_.resize(field.chunks().size());
    for (std::size_t i = 0; i < chunks_.size(); ++i) {
        chunks_[i].center = field.chunks()[i].center;
        chunks_[i].radius = field.chunks()[i].radius;
        for (const auto& instance : field.chunks()[i].instances)
            ++chunks_[i].batches[instance.presentation.value].capacity;
        for (auto& [key, batch] : chunks_[i].batches) {
            auto& storage = storage_[key];
            batch.first = static_cast<std::uint32_t>(storage.capacity);
            storage.capacity += batch.capacity;
        }
    }
    for (auto& [key, storage] : storage_) {
        glGenBuffers(1, &storage.buffer);
        glBindBuffer(GL_ARRAY_BUFFER, storage.buffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(storage.capacity * sizeof(glm::mat4)),
                     nullptr,
                     GL_STATIC_DRAW);
    }
    for (auto& chunk : chunks_)
        for (auto& [key, batch] : chunk.batches)
            batch.buffer = storage_.at(key).buffer;
    fieldIdentity_ = field.identity();
}

const std::map<std::string, VegetationRenderCache::Batch>&
VegetationRenderCache::prepareChunk(std::size_t index,
                                    const VegetationChunk& source,
                                    const ResourceManager& resources,
                                    const PrepareTransform& prepare) {
    Chunk& cached = chunks_.at(index);
    bool changed = cached.dirty || cached.revision != source.revision;
    for (const auto& [key, batch] : cached.batches) {
        const Model* model = resources.modelOrMarker(batch.handle);
        changed |= model != batch.model || (model && model->baseY() != batch.baseY);
    }
    if (!changed)
        return cached.batches;

    cached.dirty = true; // Retry a failed rebuild instead of reusing partial batches.
    for (auto& [key, batch] : cached.batches) {
        batch.count = 0;
        batch.handle = resources.requestModel(key);
        batch.model = resources.modelOrMarker(batch.handle);
        batch.baseY = batch.model ? batch.model->baseY() : 0;
    }
    std::map<std::string, std::vector<glm::mat4>> transforms;
    for (const auto& instance : source.instances) {
        const std::string& key = instance.presentation.value;
        Batch& batch = cached.batches.at(key);
        if (batch.model)
            transforms[key].push_back(
                prepare(instance, resources.entityDefinition(key), batch.baseY));
    }
    for (auto& [key, batch] : cached.batches) {
        const auto found = transforms.find(key);
        if (found == transforms.end() || found->second.empty())
            continue;
        batch.count = found->second.size();
        if (batch.count > batch.capacity)
            throw std::logic_error("Vegetation additions require a new field cache identity");
        glBindBuffer(GL_ARRAY_BUFFER, batch.buffer);
        glBufferSubData(GL_ARRAY_BUFFER,
                        static_cast<GLintptr>(batch.first * sizeof(glm::mat4)),
                        static_cast<GLsizeiptr>(batch.count * sizeof(glm::mat4)),
                        found->second.data());
        ++stats_.uploadedBatches;
    }
    cached.revision = source.revision;
    cached.dirty = false;
    ++stats_.rebuiltChunks;
    return cached.batches;
}

void VegetationRenderCache::invalidateTerrainRegion(glm::vec2 minimum, glm::vec2 maximum) {
    for (auto& chunk : chunks_) {
        const glm::vec2 nearest = glm::clamp(chunk.center, minimum, maximum);
        const glm::vec2 delta = chunk.center - nearest;
        if (glm::dot(delta, delta) <= chunk.radius * chunk.radius)
            chunk.dirty = true;
    }
}

} // namespace strategy
