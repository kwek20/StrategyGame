#pragma once

#include "assets/ResourceManager.hpp"
#include "assets/IconAtlas.hpp"
#include "diagnostics/FrameProfiler.hpp"
#include "persistence/GameConfig.hpp"
#include "players/Player.hpp"
#include "render/CameraView.hpp"
#include "render/FontRenderer.hpp"
#include "render/MaterialManager.hpp"
#include "render/RenderCommandQueue.hpp"
#include "render/RenderGraph.hpp"
#include "render/ShaderManager.hpp"
#include "terrain/Terrain.hpp"
#include "world/Entity.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <unordered_map>
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
    void beginProfileFrame();
    void recordProfile(const std::string& name, double milliseconds);
    void endFrame();
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
                      const std::string& status,
                      bool hovered = false,
                      bool active = false) const;
    void
    drawStrategyHud(const World& world, EntityId selected, const Player* player = nullptr) const;
    void drawUnitHud(const Entity& controlled) const;
    void drawTownHallHud(const Entity& townHall, const std::array<bool, 3>& hovered) const;
    void drawEntityActionHud(const Entity& entity,
                             const std::vector<std::string>& labels,
                             const std::vector<std::string>& icons,
                             const std::vector<std::string>& costs,
                             const std::vector<bool>& enabled,
                             int hovered,
                             const std::vector<std::string>& queueLabels,
                             int queueHovered) const;
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
    void setTerrainFoundations(const std::vector<TerrainFoundation>& foundations);
    void preloadAssetGroup(const std::string& group);
    [[nodiscard]] AssetLoadProgress assetProgress(const std::string& group);
    [[nodiscard]] TextureHandle requestTexture(const std::string& key) const;
    void bindTexture(TextureHandle handle, std::uint32_t unit = 0) const;
    [[nodiscard]] ResourceState textureState(TextureHandle handle) const;
    void setFramesPerSecond(float fps) {
        framesPerSecond_ = fps;
    }
    [[nodiscard]] float terrainHeightAt(float worldX, float worldZ) const;
    [[nodiscard]] FootprintFit fitTerrainFootprint(float worldX, float worldZ, float radius,
                                                   float maximumSlopeDegrees) const {
        return terrain_.fitFootprint(worldX, worldZ, radius, maximumSlopeDegrees);
    }
    [[nodiscard]] FootprintFit fitTerrainFootprint(float worldX, float worldZ,
                                                   const TerrainFootprint& footprint) const {
        return terrain_.fitFootprint(worldX, worldZ, footprint);
    }
    [[nodiscard]] std::size_t loadedModelCount() const {
        return resources_.modelCount();
    }

  private:
    struct FoundationMesh {
        std::uint32_t vao{0};
        std::uint32_t vbo{0};
        std::uint32_t ebo{0};
        std::uint32_t indexCount{0};
        EntityId sourceEntity{0};
        glm::vec3 center{0.0F};
        glm::vec2 halfExtents{0.0F};
        float rotationDegrees{0.0F};
        bool visible{true};
    };

    struct TerrainChunk {
        std::uint32_t vao{0};
        std::uint32_t vbo{0};
        std::array<std::uint32_t, 3> ebos{0, 0, 0};
        std::array<std::uint32_t, 3> indexCounts{0, 0, 0};
        glm::vec3 center{0.0F};
        float radius{0.0F};
    };

    ShaderManager shaders_;
    MaterialManager materials_;
    ShaderHandle program_;
    ShaderHandle modelProgram_;
    ShaderHandle outlineProgram_;
    ShaderHandle hudProgram_;
    ModelShaderBindings modelBindings_;
    ModelShaderBindings outlineBindings_;
    MaterialHandle worldMaterial_;
    MaterialHandle rememberedMaterial_;
    MaterialHandle outlineMaterial_;
    std::uint32_t hudVao_{0};
    std::uint32_t hudVbo_{0};
    std::uint32_t explorationTexture_{0};
    Terrain terrain_;
    std::uint32_t terrainSeed_{0x5EED1234U};
    std::vector<TerrainFoundation> terrainFoundations_;
    ResourceManager resources_{"assets"};
    IconAtlas iconAtlas_;
    FontRenderer font_;
    std::vector<TerrainChunk> terrainChunks_;
    std::vector<FoundationMesh> foundationMeshes_;
    std::vector<std::pair<int, int>> pendingTerrainChunkUploads_;
    int viewportWidth_{1};
    int viewportHeight_{1};
    float framesPerSecond_{0.0F};
    std::unique_ptr<UiRenderer> uiRenderer_;
    Logger* logger_{nullptr};
    mutable std::vector<TextDraw> pendingText_;
    mutable FrameProfiler profiler_;
    mutable RenderCommandQueue commandQueue_;
    mutable RenderGraph renderGraph_;
    std::chrono::steady_clock::time_point lastSlowFrameLog_{};
    mutable std::unordered_map<std::string, ModelHandle> modelHandles_;
    mutable std::array<TextureHandle, 4> terrainTextures_{};
    mutable TextureHandle foundationTexture_{};
    mutable TextureHandle iconAtlasTexture_{};
    std::unordered_map<std::string, AssetPreloadSet> preloadGroups_;
    [[nodiscard]] ModelHandle modelHandle(const std::string& archetype) const;
    void bindTerrainTextures() const;
    void drawIcon(const std::string& id, float left, float top, float right, float bottom,
                  const glm::vec3& tint = {1.0F, 1.0F, 1.0F}) const;
    void refreshModelShaderBindings();
    void uploadTerrainChunk(int chunkX, int chunkZ);
    void syncFoundationMeshes();
    void drawText(const std::string& text,
                  float x,
                  float y,
                  float scale,
                  const glm::vec3& color = {0.95F, 0.98F, 0.82F}) const;
};

} // namespace strategy
