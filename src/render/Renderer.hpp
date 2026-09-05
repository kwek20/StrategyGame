#pragma once

#include "assets/ResourceManager.hpp"
#include "persistence/GameConfig.hpp"
#include "players/Player.hpp"
#include "render/CameraView.hpp"
#include "render/FontRenderer.hpp"
#include "terrain/Terrain.hpp"
#include "world/Entity.hpp"

#include <array>
#include <cstdint>
#include <glm/vec3.hpp>
#include <memory>
#include <vector>

namespace strategy {

class RtsCamera;
class UiDocument;
class UiRenderer;
class Logger;
class World;

class Renderer final {
  public:
    explicit Renderer(Logger* logger = nullptr);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void beginFrame(int width, int height);
    void drawUi(const UiDocument& document) const;
    void drawLoadingScreen(float progress, const std::string& status) const;
    void drawTerrain(const CameraView& camera, const Player* player = nullptr) const;
    void
    drawWorld(const World& world, const CameraView& camera, const Player* player = nullptr) const;
    void drawResourceHud(const Player& player) const;
    void drawDebugHud(const RtsCamera& camera, std::size_t entityCount) const;
    void drawDetailedDebugHud(const CameraView& camera,
                              const Entity* entity,
                              const Player* player,
                              std::uint32_t seed,
                              std::uint64_t tick,
                              std::size_t entityCount) const;
    void drawVisionRanges(const CameraView& camera, const Entity* entity) const;
    void drawStartMenu(bool startHovered,
                       bool buildHovered,
                       bool loadHovered,
                       bool settingsHovered,
                       bool exitHovered,
                       bool seedFocused,
                       const std::string& seedText,
                       const std::string& playerOneCountry,
                       const std::string& playerTwoCountry) const;
    void
    drawSettings(const GameConfig& config, std::size_t resolution, int hovered, int binding) const;
    void drawBuildHud(PlayerId team,
                      const std::string& entityType,
                      std::size_t entityCount,
                      const std::string& status) const;
    void
    drawStrategyHud(const World& world, EntityId selected, const Player* player = nullptr) const;
    void drawUnitHud(const Entity& controlled) const;
    void drawTownHallHud(const Entity& townHall, const std::array<bool, 3>& hovered) const;
    void drawSelectionBox(const glm::vec2& start, const glm::vec2& end) const;
    [[nodiscard]] std::vector<EntityId> unitsInScreenRectangle(const glm::vec2& start,
                                                               const glm::vec2& end,
                                                               const World& world,
                                                               const CameraView& camera,
                                                               PlayerId owner) const;
    void drawUnitSelectionHud(const World& world, const std::vector<EntityId>& selected) const;
    void drawOrderMarkers(const World& world,
                          EntityId selected,
                          const std::vector<EntityId>& selection,
                          const CameraView& camera) const;
    [[nodiscard]] glm::vec3
    screenToTerrain(float pixelX, float pixelY, const CameraView& camera) const;
    [[nodiscard]] EntityId pickEntity(float pixelX,
                                      float pixelY,
                                      const World& world,
                                      const CameraView& camera,
                                      const Player* player = nullptr,
                                      bool currentlyVisibleOnly = false) const;
    void drawEntityOutline(const World& world,
                           EntityId entity,
                           const CameraView& camera,
                           const Player* player = nullptr) const;
    [[nodiscard]] CameraView constrainThirdPersonCamera(const CameraView& desired,
                                                        const World& world,
                                                        EntityId followed) const;
    [[nodiscard]] float aspectRatio() const {
        return static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_);
    }
    void drawPauseMenu(bool resumeHovered, bool settingsHovered, bool exitHovered) const;
    void regenerateTerrain(std::uint32_t seed);
    void setFramesPerSecond(float fps) {
        framesPerSecond_ = fps;
    }
    [[nodiscard]] float terrainHeightAt(float worldX, float worldZ) const;
    [[nodiscard]] std::size_t loadedModelCount() const {
        return resources_.modelCount();
    }

  private:
    struct TerrainChunk {
        std::uint32_t vao{0};
        std::uint32_t vbo{0};
        std::array<std::uint32_t, 3> ebos{0, 0, 0};
        std::array<std::uint32_t, 3> indexCounts{0, 0, 0};
        glm::vec3 center{0.0F};
        float radius{0.0F};
    };

    std::uint32_t program_{0};
    std::uint32_t modelProgram_{0};
    std::uint32_t outlineProgram_{0};
    std::uint32_t hudProgram_{0};
    std::uint32_t hudVao_{0};
    std::uint32_t hudVbo_{0};
    std::uint32_t explorationTexture_{0};
    Terrain terrain_;
    ResourceManager resources_{"assets"};
    FontRenderer font_;
    std::vector<TerrainChunk> terrainChunks_;
    int viewportWidth_{1};
    int viewportHeight_{1};
    float framesPerSecond_{0.0F};
    std::unique_ptr<UiRenderer> uiRenderer_;
    Logger* logger_{nullptr};
    void drawText(const std::string& text,
                  float x,
                  float y,
                  float scale,
                  const glm::vec3& color = {0.95F, 0.98F, 0.82F}) const;
};

} // namespace strategy
