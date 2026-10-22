#pragma once

#include "terrain/TerrainV2Recipe.hpp"

#include <glm/vec2.hpp>
#include <vector>

namespace strategy {
enum class TerrainCellRole { ordinary, opening, route, hill, peak, basin, shore };
struct TerrainRidge {
    glm::vec2 center{}, direction{};
    float length{}, width{}, heightScale{}, bend{}, phase{}, scarpSide{1};
    [[nodiscard]] float influence(glm::vec2 position) const;
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
    const std::vector<TerrainRidge>& ridges() const {
        return ridges_;
    }
    float isletInfluence(glm::vec2 position) const;
    [[nodiscard]] glm::vec2 fieldPosition(glm::vec2 position) const;
    void setAccessMask(int side, std::vector<std::uint8_t> mask);
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
    float lakeAspect_{1.0F};
    float lakeRotation_{};
    glm::vec2 lakeCenter_{0.0F};
    glm::vec2 fieldOffset_{0.0F};
    float fieldAngle_{};
    float fieldStretch_{1.0F};
    int accessSide_{};
    std::vector<std::uint8_t> accessMask_;
    std::vector<glm::vec2> openings_;
    std::vector<TerrainRidge> ridges_;
    std::vector<glm::vec2> islets_;
};
} // namespace strategy
