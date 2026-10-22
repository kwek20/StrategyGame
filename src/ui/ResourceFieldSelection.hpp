#pragma once

#include "world/World.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace strategy {
// UI identity survives depletion/removal of the node that was originally clicked.
struct ResourceFieldSelection {
    std::uint64_t fieldId{};
    std::string family;

    explicit operator bool() const {
        return fieldId != 0;
    }

    static ResourceFieldSelection fromClick(const World& world, EntityId id, bool singleNode) {
        const auto* node = world.findEntity(id);
        if (singleNode || !node || !node->resource || node->resource.remaining <= 0 ||
            !node->resource.fieldId)
            return {};
        return {node->resource.fieldId, node->resource.type};
    }

    std::vector<EntityId> members(const World& world) const {
        std::vector<EntityId> result;
        if (!fieldId)
            return result;
        for (const auto& node : world.entities())
            if (node.resource && node.resource.fieldId == fieldId && node.resource.type == family &&
                node.resource.remaining > 0)
                result.push_back(node.id);
        std::sort(result.begin(), result.end());
        return result;
    }
};
} // namespace strategy
