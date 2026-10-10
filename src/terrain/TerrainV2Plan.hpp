#pragma once

#include "terrain/TerrainV2Recipe.hpp"

#include <glm/vec2.hpp>
#include <vector>

namespace strategy {
enum class TerrainCellRole { ordinary, opening, route, hill, peak, basin, shore };
struct TerrainRoute {
    glm::vec2 start{};
    glm::vec2 end{};
};

// Regional reservations have no material/road representation.
class TerrainV2Plan final {
  public:
    static TerrainV2Plan
    create(std::uint32_t seed, TerrainV2Recipe recipe, float extent, std::size_t players);
    const TerrainV2Recipe& recipe() const {
        return recipe_;
    }
    const std::vector<glm::vec2>& openings() const {
        return openings_;
    }
    float extent() const {
        return extent_;
    }
    float relief() const {
        return relief_;
    }
    float openingInfluence(glm::vec2 position) const;
    bool reservedRoute(glm::vec2 position) const;
    float routeInfluence(glm::vec2 position) const;
    // Negative inside the active basin; dry plans return +infinity.
    float waterDistance(glm::vec2 position) const;
    float peakInfluence(glm::vec2 position) const;
    float isletInfluence(glm::vec2 position) const;
    [[nodiscard]] TerrainV2Plan constructiveFallback() const;
    bool hasWater() const {
        return lakeRadius_ > 0.0F;
    }

  private:
    TerrainV2Recipe recipe_;
    float extent_{};
    float relief_{};
    float lakeRadius_{};
    float lakePhase_{};
    std::vector<glm::vec2> openings_;
    std::vector<TerrainRoute> routes_;
    std::vector<glm::vec2> peaks_;
    std::vector<glm::vec2> islets_;
};
} // namespace strategy
