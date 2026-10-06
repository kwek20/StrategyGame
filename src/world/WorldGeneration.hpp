#pragma once
#include "gameplay/DefinitionRegistry.hpp"
#include <cstdint>
#include <glm/vec2.hpp>
#include <vector>
namespace strategy {
class DefinitionRegistry;
class Terrain;
class World;
class VegetationField;
class WorldGenerationProgress;

struct GeneratedResourceField {
    ResourceFieldId definition;
    glm::vec2 center{0.0F};
    float radius{0.0F};
    std::uint32_t desiredNodes{0};
    // Diagnostic allocation only: resources remain neutral, and this is not simulation ownership.
    std::uint32_t startingPlayer{0};
    float capacity{0.0F};
    float accessCost{0.0F};
};

struct GeneratedResourceNode {
    ResourceFieldId field;
    ResourceArchetypeId archetype;
    glm::vec2 position{0.0F};
    float rotationDegrees{0.0F};
    std::size_t fieldIndex{0};
};

struct ResourceLayout {
    std::vector<GeneratedResourceField> fields;
    std::vector<GeneratedResourceNode> nodes;
};

[[nodiscard]] ResourceLayout populateResources(
    World& world,
    const Terrain& terrain,
    const DefinitionRegistry& definitions,
    std::uint32_t terrainSeed,
    std::uint32_t mapChunksPerSide = 20,
    float abundanceScale = 1.0F,
    const std::vector<glm::vec2>& startingAnchors = {},
    WorldGenerationProgress* progress = nullptr);
[[nodiscard]] VegetationField generateVegetation(const World& world,
                                                 const Terrain& terrain,
                                                 const DefinitionRegistry& definitions,
                                                 std::uint32_t terrainSeed,
                                                 std::uint32_t mapChunksPerSide = 20,
                                                 WorldGenerationProgress* progress = nullptr);
} // namespace strategy
