#include "gameplay/DefinitionRegistry.hpp"
#include "terrain/Terrain.hpp"
#include "world/StartingPlacement.hpp"
#include "world/MapArea.hpp"
#include "world/World.hpp"
#include "world/WorldGeneration.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
bool sameLayout(const strategy::ResourceLayout& left,
                const strategy::ResourceLayout& right) {
    if (left.fields.size() != right.fields.size() || left.nodes.size() != right.nodes.size())
        return false;
    for (std::size_t index = 0; index < left.fields.size(); ++index) {
        const auto& a = left.fields[index];
        const auto& b = right.fields[index];
        if (a.definition != b.definition || a.center != b.center || a.radius != b.radius ||
            a.desiredNodes != b.desiredNodes)
            return false;
    }
    for (std::size_t index = 0; index < left.nodes.size(); ++index) {
        const auto& a = left.nodes[index];
        const auto& b = right.nodes[index];
        if (a.field != b.field || a.archetype != b.archetype || a.position != b.position ||
            a.rotationDegrees != b.rotationDegrees ||
            a.capacityMultiplier != b.capacityMultiplier)
            return false;
    }
    return true;
}

std::vector<glm::vec2> anchors(const strategy::Terrain& terrain,
                               const strategy::DefinitionRegistry& definitions,
                               std::uint32_t mapChunksPerSide = 10,
                               std::uint32_t selectionSeed = 0x5EED1234U) {
    std::vector<glm::vec2> result;
    for (const strategy::StartingRegion& region :
         strategy::selectStartingRegions(terrain, definitions, mapChunksPerSide, 2,
                                         selectionSeed))
        result.push_back(region.anchor);
    return result;
}
}

int strategyTestMain() {
    const strategy::DefinitionRegistry definitions;
    const std::vector<std::uint32_t> seeds{123U, 0x13572468U, 0x24681357U, 0x51A7F00DU};
    bool valid = true;
    std::set<std::string> observedVariants;
    std::set<std::string> signatures;

    for (const std::uint32_t seed : seeds) {
        const strategy::Terrain terrain{seed};
        const std::vector<glm::vec2> starts = anchors(terrain, definitions, 10, seed);
        const strategy::MapArea startMap{10};
        valid = valid && starts.size() == 2 &&
                glm::distance(starts[0], starts[1]) + 0.001F >=
                    startMap.extent() *
                        definitions.matchRules().minimumOpponentSeparationNormalized;
        strategy::World firstWorld;
        strategy::World secondWorld;
        const strategy::ResourceLayout first = strategy::populateResources(
            firstWorld, terrain, definitions, seed, 10, 1.0F, starts);
        const strategy::ResourceLayout second = strategy::populateResources(
            secondWorld, terrain, definitions, seed, 10, 1.0F, starts);

        valid = valid && sameLayout(first, second) && !first.fields.empty() &&
                !first.nodes.empty() && firstWorld.size() == first.nodes.size() &&
                secondWorld.size() == second.nodes.size();

        std::string signature;
        for (const strategy::GeneratedResourceNode& node : first.nodes) {
            const auto* nodeDefinition = definitions.resource(node.archetype);
            const auto* fieldDefinition = definitions.resourceField(node.field);
            valid = valid && nodeDefinition && fieldDefinition;
            if (!nodeDefinition || !fieldDefinition) continue;
            observedVariants.insert(node.archetype.value);
            signature += node.field.value + ':' + node.archetype.value + ':' +
                         std::to_string(node.position.x) + ':' +
                         std::to_string(node.position.y) + ';';
            const bool belongsToField = std::any_of(
                first.fields.begin(), first.fields.end(), [&](const auto& field) {
                    return field.definition == node.field &&
                           glm::distance(field.center, node.position) <= field.radius + 0.001F;
                });
            valid = valid && belongsToField &&
                    node.capacityMultiplier >=
                        fieldDefinition->generation.minimumCapacityMultiplier &&
                    node.capacityMultiplier <=
                        fieldDefinition->generation.maximumCapacityMultiplier;
        }
        signatures.insert(std::move(signature));

        for (std::size_t left = 0; left < first.nodes.size(); ++left)
            for (std::size_t right = left + 1; right < first.nodes.size(); ++right) {
                const auto* leftType = definitions.resource(first.nodes[left].archetype);
                const auto* rightType = definitions.resource(first.nodes[right].archetype);
                if (!leftType || !rightType) continue;
                const float minimumDistance = leftType->collisionRadius +
                                              rightType->collisionRadius;
                valid = valid && glm::distance(first.nodes[left].position,
                                               first.nodes[right].position) + 0.0001F >=
                                     minimumDistance;
            }
    }

    valid = valid && signatures.size() == seeds.size();
    for (const std::string& fieldId : definitions.matchRules().generatedResourceFields) {
        const auto* field = definitions.resourceField(strategy::ResourceFieldId{fieldId});
        bool observed = false;
        if (field)
            for (const auto& variant : field->variants)
                observed = observed || observedVariants.contains(variant.node.value);
        valid = valid && observed;
    }

    // Regression seed covering the default 15x15 match: landform shaping must not deny either
    // player the opening Scrap capacity guaranteed by the fairness policy.
    {
        constexpr std::uint32_t regressionSeed = 123U;
        const strategy::Terrain terrain{regressionSeed};
        const std::vector<glm::vec2> starts = anchors(terrain, definitions, 15);
        strategy::World world;
        const strategy::ResourceLayout layout = strategy::populateResources(
            world, terrain, definitions, regressionSeed, 15, 1.0F, starts);
        valid = valid && !layout.nodes.empty();
    }

    if (!valid)
        std::cerr << "Multi-seed resource field generation test failed\n";
    return valid ? 0 : 1;
}
