#pragma once

#include "world/Vegetation.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <glm/geometric.hpp>
#include <map>
#include <utility>
#include <vector>

namespace strategy {
// Derive static render instances from live harvest targets. Depletion/removal revises only
// its field's range in the shared instance buffer; new or moved targets rebuild the allocation.
class ResourceNodePresentation final {
  public:
    const VegetationField& update(const World& world) {
        std::map<EntityId, Source> next;
        for (const auto& entity : world.entities())
            if (entity.resource && entity.resource.fieldId && entity.resource.remaining > 0)
                next.emplace(entity.id,
                             Source{entity.resource.fieldId,
                                    entity.archetype,
                                    PresentationId{entity.renderId()},
                                    entity.transform});
        const bool rebuild = std::any_of(next.begin(), next.end(), [&](const auto& entry) {
            const auto old = sources_.find(entry.first);
            return old == sources_.end() || !(old->second == entry.second);
        });
        if (rebuild) {
            std::map<std::uint64_t, VegetationChunk> fields;
            for (const auto& [id, source] : next) {
                auto& chunk = fields[source.field];
                chunk.instances.push_back(
                    {source.archetype, source.presentation, source.transform, id});
                chunk.center += glm::vec2{source.transform.position.x, source.transform.position.z};
            }
            std::vector<VegetationChunk> chunks;
            for (auto& [id, chunk] : fields) {
                chunk.center /= static_cast<float>(chunk.instances.size());
                for (const auto& instance : chunk.instances)
                    chunk.radius = std::max(chunk.radius,
                                            glm::length(glm::vec2{instance.transform.position.x,
                                                                  instance.transform.position.z} -
                                                        chunk.center) +
                                                3);
                chunks.push_back(std::move(chunk));
            }
            field_ = VegetationField{std::move(chunks)};
        } else {
            for (const auto& [id, source] : sources_)
                if (!next.contains(id))
                    field_.removeInstance(id);
        }
        sources_ = std::move(next);
        return field_;
    }
    void clear() {
        sources_.clear();
        field_ = VegetationField{};
    }

  private:
    struct Source {
        std::uint64_t field;
        EntityArchetypeId archetype;
        PresentationId presentation;
        Transform transform;
        bool operator==(const Source& other) const {
            return field == other.field && archetype == other.archetype &&
                   presentation == other.presentation &&
                   transform.position == other.transform.position &&
                   transform.rotationDegrees == other.transform.rotationDegrees &&
                   transform.scale == other.transform.scale;
        }
    };
    std::map<EntityId, Source> sources_;
    VegetationField field_;
};
} // namespace strategy
