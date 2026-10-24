#pragma once

#include <glm/vec2.hpp>
#include <optional>
#include <string>

namespace strategy {
class DefinitionRegistry;
class Terrain;
class World;
struct Entity;

// Deterministic, expanding search over the entire playable map. A crowded perimeter
// never caps deployment distance. Null means no sampled legal space exists anywhere.
[[nodiscard]] std::optional<glm::vec2> findUnitDeployment(const World& world,
                                                          const Terrain& terrain,
                                                          const DefinitionRegistry& definitions,
                                                          unsigned int mapChunksPerSide,
                                                          const Entity& producer,
                                                          const std::string& product);
} // namespace strategy
