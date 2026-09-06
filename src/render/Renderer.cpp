#include "render/Renderer.hpp"

#include "game/RtsCamera.hpp"
#include "localization/Text.hpp"
#include "diagnostics/Logger.hpp"
#include "render/RenderPass.hpp"
#include "render/UiRenderer.hpp"
#include "ui/UiDocument.hpp"
#include "world/World.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <limits>
#include <unordered_map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace strategy {
namespace {

struct TerrainVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

void GLAPIENTRY openGlDebugMessage(GLenum,
                                   GLenum type,
                                   GLuint id,
                                   GLenum severity,
                                   GLsizei,
                                   const GLchar* message,
                                   const void* userData) {
    if (id == 131185) // Common informational NVIDIA buffer message.
        return;
    auto* logger = static_cast<Logger*>(const_cast<void*>(userData));
    if (!logger)
        return;
    static std::unordered_map<std::string, std::uint64_t> occurrences;
    const std::string key = std::to_string(id) + ":" + message;
    const std::uint64_t count = ++occurrences[key];
    if (count > 3 && count != 10 && count != 100 && count % 1000 != 0)
        return;
    const LogLevel level = severity == GL_DEBUG_SEVERITY_HIGH
                               ? LogLevel::error
                               : severity == GL_DEBUG_SEVERITY_MEDIUM ? LogLevel::warning
                                                                      : LogLevel::debug;
    std::string state;
    if (level == LogLevel::error) {
        GLint program = 0;
        GLint vertexArray = 0;
        GLint arrayBuffer = 0;
        GLint elementBuffer = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        // In a core profile the element-buffer binding belongs to a VAO. Querying it
        // while VAO 0 is active can itself generate GL_INVALID_OPERATION on NVIDIA.
        if (vertexArray != 0)
            glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
        state = " state(program=" + std::to_string(program) +
                " vao=" + std::to_string(vertexArray) +
                " arrayBuffer=" + std::to_string(arrayBuffer) +
                " elementBuffer=" + std::to_string(elementBuffer) + ")";
    }
    logger->log(level,
                "opengl",
                std::string("id=") + std::to_string(id) + " type=" + std::to_string(type) +
                    " occurrences=" + std::to_string(count) + " " + message + state);
}

ShaderHandle createTerrainProgram(ShaderManager& shaders) {
    return shaders.loadFiles(
        "terrain", "assets/shaders/terrain.vert", "assets/shaders/terrain.frag");
}

ShaderHandle createModelProgram(ShaderManager& shaders) {
    return shaders.loadFiles(
        "model", "assets/shaders/model.vert", "assets/shaders/model.frag");
}

ShaderHandle createOutlineProgram(ShaderManager& shaders) {
    return shaders.loadFiles(
        "outline", "assets/shaders/outline.vert", "assets/shaders/outline.frag");
}

ShaderHandle createHudProgram(ShaderManager& shaders) {
    return shaders.loadFiles(
        "hud", "assets/shaders/hud.vert", "assets/shaders/hud.frag");
}

void appendHudRectangle(std::vector<glm::vec2>& vertices,
                        float left,
                        float top,
                        float right,
                        float bottom,
                        int width,
                        int height) {
    const auto ndc = [width, height](float x, float y) {
        return glm::vec2{x / static_cast<float>(width) * 2.0F - 1.0F,
                         1.0F - y / static_cast<float>(height) * 2.0F};
    };
    const glm::vec2 topLeft = ndc(left, top);
    const glm::vec2 topRight = ndc(right, top);
    const glm::vec2 bottomLeft = ndc(left, bottom);
    const glm::vec2 bottomRight = ndc(right, bottom);
    vertices.insert(vertices.end(),
                    {topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight});
}

void appendHudText(std::vector<glm::vec2>& vertices,
                   const std::string& text,
                   float pixelX,
                   float pixelY,
                   float scale,
                   int width,
                   int height) {
    (void)vertices;
    (void)text;
    (void)pixelX;
    (void)pixelY;
    (void)scale;
    (void)width;
    (void)height;
}

float presentationGroundOffset(const EntityDefinition* definition, EntityId entityId) {
    if (!definition)
        return 0.0F;
    float offset = definition->groundOffset;
    if (definition->sinkVariance > 0.0F) {
        const std::uint64_t mixed = entityId * 0x9E3779B97F4A7C15ULL;
        offset -= definition->sinkVariance *
                  static_cast<float>((mixed >> 40U) & 0xFFFFU) / 65535.0F;
    }
    return offset;
}

glm::mat4 groundedEntityTransform(const Terrain& terrain,
                                  const Transform& shown,
                                  const EntityDefinition* definition,
                                  EntityId entityId,
                                  float scaleMultiplier = 1.0F) {
    glm::mat4 transform{1.0F};
    transform = glm::translate(
        transform,
        {shown.position.x,
         terrain.heightAt(shown.position.x, shown.position.z) + shown.position.y +
             presentationGroundOffset(definition, entityId),
         shown.position.z});
    if (definition && definition->alignToTerrain && definition->maximumTilt > 0.0F) {
        const float sample = Terrain::spacing * 2.0F;
        const float pitch = glm::clamp(
            glm::degrees(std::atan2(terrain.heightAt(shown.position.x, shown.position.z - sample) -
                                        terrain.heightAt(shown.position.x, shown.position.z + sample),
                                    sample * 2.0F)),
            -definition->maximumTilt,
            definition->maximumTilt);
        const float roll = glm::clamp(
            glm::degrees(std::atan2(terrain.heightAt(shown.position.x + sample, shown.position.z) -
                                        terrain.heightAt(shown.position.x - sample, shown.position.z),
                                    sample * 2.0F)),
            -definition->maximumTilt,
            definition->maximumTilt);
        transform = glm::rotate(transform, glm::radians(pitch), {1.0F, 0.0F, 0.0F});
        transform = glm::rotate(transform, glm::radians(roll), {0.0F, 0.0F, 1.0F});
    }
    transform = glm::rotate(transform, glm::radians(shown.rotationDegrees.x), {1, 0, 0});
    transform = glm::rotate(transform, glm::radians(shown.rotationDegrees.y), {0, 1, 0});
    transform = glm::rotate(transform, glm::radians(shown.rotationDegrees.z), {0, 0, 1});
    const float catalogueScale = definition ? definition->scale : 1.0F;
    return glm::scale(transform, shown.scale * catalogueScale * scaleMultiplier);
}

} // namespace

Renderer::Renderer(Logger* logger)
    : font_(shaders_), logger_(logger) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glCullFace(GL_BACK);
    if (logger_ && GLAD_GL_VERSION_4_3) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(openGlDebugMessage, logger_);
        logger_->info("renderer", "OpenGL debug output enabled");
    }
    uiRenderer_ = std::make_unique<UiRenderer>(shaders_);

    program_ = createTerrainProgram(shaders_);
    modelProgram_ = createModelProgram(shaders_);
    outlineProgram_ = createOutlineProgram(shaders_);
    hudProgram_ = createHudProgram(shaders_);
    worldMaterial_ = materials_.create({"world", modelProgram_});
    RenderMaterial remembered{"remembered", modelProgram_};
    remembered.blending = true;
    remembered.rememberedEntity = true;
    rememberedMaterial_ = materials_.create(std::move(remembered));
    RenderMaterial outline{"selection-outline", outlineProgram_};
    outline.pass = MaterialPass::overlay;
    outline.depthWrite = false;
    outlineMaterial_ = materials_.create(std::move(outline));
    refreshModelShaderBindings();
    glGenVertexArrays(1, &hudVao_);
    glGenBuffers(1, &hudVbo_);
    glGenTextures(1, &explorationTexture_);
    glBindTexture(GL_TEXTURE_2D, explorationTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    constexpr int chunkSide = Terrain::chunkCellCount + 1;
    constexpr float halfExtent = static_cast<float>(Terrain::cellCount) * Terrain::spacing * 0.5F;
    terrainChunks_.reserve(Terrain::chunksPerSide * Terrain::chunksPerSide);

    for (int chunkZ = 0; chunkZ < Terrain::chunksPerSide; ++chunkZ) {
        for (int chunkX = 0; chunkX < Terrain::chunksPerSide; ++chunkX) {
            std::vector<TerrainVertex> vertices;
            std::array<std::vector<std::uint32_t>, 3> lodIndices;
            vertices.reserve(chunkSide * chunkSide);
            lodIndices[0].reserve(Terrain::chunkCellCount * Terrain::chunkCellCount * 6);
            const int startX = chunkX * Terrain::chunkCellCount;
            const int startZ = chunkZ * Terrain::chunkCellCount;

            for (int localZ = 0; localZ < chunkSide; ++localZ) {
                for (int localX = 0; localX < chunkSide; ++localX) {
                    const int gridX = startX + localX;
                    const int gridZ = startZ + localZ;
                    vertices.push_back({{static_cast<float>(gridX) * Terrain::spacing - halfExtent,
                                         terrain_.vertexHeight(gridX, gridZ),
                                         static_cast<float>(gridZ) * Terrain::spacing - halfExtent},
                                        terrain_.normalAt(gridX, gridZ),
                                        terrain_.colorAt(terrain_.normalizedHeight(gridX, gridZ))});
                }
            }

            constexpr std::array<int, 3> lodSteps{1, 2, 4};
            for (std::size_t level = 0; level < lodSteps.size(); ++level) {
                const int step = lodSteps[level];
                for (int z = 0; z < Terrain::chunkCellCount; z += step) {
                    for (int x = 0; x < Terrain::chunkCellCount; x += step) {
                        const auto topLeft = static_cast<std::uint32_t>(z * chunkSide + x);
                        const auto bottomLeft =
                            topLeft + static_cast<std::uint32_t>(step * chunkSide);
                        lodIndices[level].insert(lodIndices[level].end(),
                                                 {topLeft,
                                                  bottomLeft,
                                                  topLeft + static_cast<std::uint32_t>(step),
                                                  topLeft + static_cast<std::uint32_t>(step),
                                                  bottomLeft,
                                                  bottomLeft + static_cast<std::uint32_t>(step)});
                    }
                }
            }

            TerrainChunk chunk;
            for (std::size_t level = 0; level < lodIndices.size(); ++level) {
                chunk.indexCounts[level] = static_cast<std::uint32_t>(lodIndices[level].size());
            }
            const float chunkWorldSize =
                static_cast<float>(Terrain::chunkCellCount) * Terrain::spacing;
            chunk.center = {
                static_cast<float>(startX) * Terrain::spacing - halfExtent + chunkWorldSize * 0.5F,
                Terrain::heightScale * 0.5F,
                static_cast<float>(startZ) * Terrain::spacing - halfExtent + chunkWorldSize * 0.5F};
            chunk.radius = std::sqrt(2.0F * std::pow(chunkWorldSize * 0.5F, 2.0F) +
                                     std::pow(Terrain::heightScale * 0.5F, 2.0F));

            glGenVertexArrays(1, &chunk.vao);
            glGenBuffers(1, &chunk.vbo);
            glGenBuffers(static_cast<GLsizei>(chunk.ebos.size()), chunk.ebos.data());
            glBindVertexArray(chunk.vao);
            glBindBuffer(GL_ARRAY_BUFFER, chunk.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(TerrainVertex)),
                         vertices.data(),
                         GL_STATIC_DRAW);
            for (std::size_t level = 0; level < lodIndices.size(); ++level) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebos[level]);
                glBufferData(
                    GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(lodIndices[level].size() * sizeof(std::uint32_t)),
                    lodIndices[level].data(),
                    GL_STATIC_DRAW);
            }
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0,
                                  3,
                                  GL_FLOAT,
                                  GL_FALSE,
                                  sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex, position)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1,
                                  3,
                                  GL_FLOAT,
                                  GL_FALSE,
                                  sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex, normal)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2,
                                  3,
                                  GL_FLOAT,
                                  GL_FALSE,
                                  sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex, color)));
            glBindVertexArray(0);
            terrainChunks_.push_back(chunk);
        }
    }
}

void Renderer::drawUi(const UiDocument& document) const {
    renderGraph_.enter(RenderPassKind::userInterface);
    ProfileScope profile(profiler_, "render.ui");
    uiRenderer_->draw(document, viewportWidth_, viewportHeight_);
}

Renderer::~Renderer() {
    for (const TerrainChunk& chunk : terrainChunks_) {
        glDeleteBuffers(static_cast<GLsizei>(chunk.ebos.size()), chunk.ebos.data());
        glDeleteBuffers(1, &chunk.vbo);
        glDeleteVertexArrays(1, &chunk.vao);
    }
    glDeleteBuffers(1, &hudVbo_);
    glDeleteVertexArrays(1, &hudVao_);
    glDeleteTextures(1, &explorationTexture_);
}

void Renderer::refreshModelShaderBindings() {
    const auto bindings = [this](ShaderHandle handle) {
        return ModelShaderBindings{shaders_.program(handle),
                                   shaders_.uniform(handle, "viewProjection"),
                                   shaders_.uniform(handle, "model"),
                                   shaders_.uniform(handle, "useSkinning"),
                                   shaders_.uniform(handle, "bones[0]"),
                                   shaders_.uniform(handle, "baseColorTexture"),
                                   shaders_.uniform(handle, "materialDiffuse"),
                                   shaders_.uniform(handle, "materialOpacity"),
                                   shaders_.uniform(handle, "hasBaseColorTexture")};
    };
    modelBindings_ = bindings(modelProgram_);
    outlineBindings_ = bindings(outlineProgram_);
}

void Renderer::beginFrame(int width, int height) {
    ProfileScope profile(profiler_, "render.begin");
    renderGraph_.beginFrame();
    commandQueue_.clear();
    const std::vector<ShaderReloadResult> reloads = shaders_.reloadChanged();
    if (!reloads.empty()) {
        refreshModelShaderBindings();
        if (logger_)
            for (const ShaderReloadResult& reload : reloads)
                logger_->log(reload.succeeded ? LogLevel::info : LogLevel::error,
                             "shader",
                             reload.name + ": " + reload.message);
    }
    {
        ProfileScope uploads(profiler_, "assets.upload");
        resources_.update();
    }
    viewportWidth_ = width;
    viewportHeight_ = height;
    pendingText_.clear();
    glViewport(0, 0, width, height);
    glClearColor(0.42F, 0.66F, 0.88F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::beginProfileFrame() {
    profiler_.beginFrame();
}

void Renderer::recordProfile(const std::string& name, double milliseconds) {
    profiler_.record(name, milliseconds);
}

ModelHandle Renderer::modelHandle(const std::string& archetype) const {
    if (const auto found = modelHandles_.find(archetype); found != modelHandles_.end())
        return found->second;
    const ModelHandle handle = resources_.requestModel(archetype);
    modelHandles_.emplace(archetype, handle);
    return handle;
}

void Renderer::preloadAssetGroup(const std::string& group) {
    preloadGroups_.insert_or_assign(group, resources_.preloadGroup(group));
}

AssetLoadProgress Renderer::assetProgress(const std::string& group) {
    resources_.update();
    const auto found = preloadGroups_.find(group);
    return found == preloadGroups_.end() ? AssetLoadProgress{} : resources_.progress(found->second);
}

TextureHandle Renderer::requestTexture(const std::string& key) const {
    return resources_.requestTexture(key);
}

void Renderer::bindTexture(TextureHandle handle, std::uint32_t unit) const {
    resources_.textureOrMarker(handle)->bind(unit);
}

ResourceState Renderer::textureState(TextureHandle handle) const {
    return resources_.state(handle);
}

void Renderer::bindTerrainTextures() const {
    static constexpr std::array<const char*, 4> keys{
        "terrain/grass", "terrain/dirt", "terrain/rock", "terrain/dry_ground"};
    for (std::size_t index = 0; index < terrainTextures_.size(); ++index) {
        if (!terrainTextures_[index])
            terrainTextures_[index] = resources_.requestTexture(keys[index]);
        resources_.textureOrMarker(terrainTextures_[index])
            ->bind(static_cast<std::uint32_t>(index), true);
    }
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::endFrame() {
    {
        ProfileScope profile(profiler_, "render.text");
        font_.drawBatch(pendingText_, viewportWidth_, viewportHeight_);
    }
    pendingText_.clear();
    profiler_.endFrame();
    const double frameMilliseconds = profiler_.last("frame.total");
    const auto now = std::chrono::steady_clock::now();
    if (logger_ && frameMilliseconds > 50.0 && now - lastSlowFrameLog_ > std::chrono::seconds(2)) {
        logger_->warning("performance",
                         "Slow frame: " + std::to_string(frameMilliseconds) + " ms");
        lastSlowFrameLog_ = now;
    }
}

void Renderer::drawLoadingScreen(float progress, const std::string& status) const {
    renderGraph_.enter(RenderPassKind::userInterface);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float centerX = viewportWidth_ * 0.5F, centerY = viewportHeight_ * 0.5F;
    std::vector<glm::vec2> track, fill;
    appendHudRectangle(track,
                       centerX - 210.0F,
                       centerY + 38.0F,
                       centerX + 210.0F,
                       centerY + 58.0F,
                       viewportWidth_,
                       viewportHeight_);
    appendHudRectangle(fill,
                       centerX - 207.0F,
                       centerY + 41.0F,
                       centerX - 207.0F + 414.0F * std::clamp(progress, 0.0F, 1.0F),
                       centerY + 55.0F,
                       viewportWidth_,
                       viewportHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    const auto draw = [this](const std::vector<glm::vec2>& vertices, const glm::vec3& color) {
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(color));
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    };
    draw(track, {0.10F, 0.13F, 0.16F});
    draw(fill, {0.18F, 0.76F, 0.88F});
    const std::string title = Text::get("loading.title");
    drawText(title, centerX - title.size() * 9.0F, centerY - 35.0F, 3.0F);
    drawText(status, centerX - status.size() * 4.8F, centerY + 5.0F, 1.4F, {0.65F, 0.74F, 0.78F});
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawText(
    const std::string& text, float x, float y, float scale, const glm::vec3& color) const {
    pendingText_.push_back(
        {text, x, y, std::max(scale * 8.0F, 13.0F), color});
}

void Renderer::drawTerrain(const CameraView& camera, const Player* player) const {
    renderGraph_.enter(RenderPassKind::terrain);
    ProfileScope profile(profiler_, "render.terrain");
    RenderPass pass(RenderPassKind::terrain);
    const glm::vec3 focus = camera.target;
    const glm::mat4 viewProjection = camera.viewProjection();

    shaders_.use(program_);
    bindTerrainTextures();
    glUniform1i(shaders_.uniform(program_, "grassTexture"), 0);
    glUniform1i(shaders_.uniform(program_, "dirtTexture"), 1);
    glUniform1i(shaders_.uniform(program_, "rockTexture"), 2);
    glUniform1i(shaders_.uniform(program_, "dryGroundTexture"), 3);
    const GLint location = shaders_.uniform(program_, "viewProjection");
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform3fv(
        shaders_.uniform(program_, "cameraPosition"), 1, glm::value_ptr(camera.position));
    const bool closeView = camera.detailDistance < 20.0F;
    glUniform2f(shaders_.uniform(program_, "fogRange"), 140.0F, 280.0F);
    glUniform1i(shaders_.uniform(program_, "useExploration"), player ? 1 : 0);
    if (player) {
        std::vector<std::uint8_t> map(player->discovered.size());
        for (std::size_t i = 0; i < map.size(); ++i)
            map[i] = player->visible[i] ? 255 : (player->discovered[i] ? 90 : 0);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, explorationTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_R8,
                     Player::explorationCells,
                     Player::explorationCells,
                     0,
                     GL_RED,
                     GL_UNSIGNED_BYTE,
                     map.data());
        glUniform1i(shaders_.uniform(program_, "explorationMap"), 7);
        glActiveTexture(GL_TEXTURE0);
    }
    constexpr float renderDistance = 190.0F;
    for (const TerrainChunk& chunk : terrainChunks_) {
        const float dx = chunk.center.x - focus.x;
        const float dz = chunk.center.z - focus.z;
        const float maximumDistance = renderDistance + chunk.radius;
        if (dx * dx + dz * dz > maximumDistance * maximumDistance) {
            continue;
        }
        std::size_t lodLevel;
        if (closeView) {
            const float cameraDx = chunk.center.x - camera.position.x;
            const float cameraDz = chunk.center.z - camera.position.z;
            const float distanceSquared = cameraDx * cameraDx + cameraDz * cameraDz;
            lodLevel = distanceSquared < 55.0F * 55.0F     ? 0U
                       : distanceSquared < 105.0F * 105.0F ? 1U
                                                           : 2U;
        } else {
            lodLevel = camera.detailDistance <= 36.0F   ? 0U
                       : camera.detailDistance <= 76.0F ? 1U
                                                        : 2U;
        }
        glBindVertexArray(chunk.vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebos[lodLevel]);
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(chunk.indexCounts[lodLevel]),
                       GL_UNSIGNED_INT,
                       nullptr);
    }
    glBindVertexArray(0);
}

void Renderer::drawWorld(const World& world, const CameraView& camera, const Player* player) const {
    renderGraph_.enter(RenderPassKind::world);
    ProfileScope profile(profiler_, "render.world");
    RenderPass pass(RenderPassKind::world);
    const glm::mat4 viewProjection = camera.viewProjection();
    shaders_.use(modelProgram_);
    glUniform3fv(
        shaders_.uniform(modelProgram_, "cameraPosition"), 1, glm::value_ptr(camera.position));
    glUniform2f(shaders_.uniform(modelProgram_, "fogRange"), 140.0F, 280.0F);
    glUniform1i(shaders_.uniform(modelProgram_, "rememberedEntity"), 0);

    for (const Entity& entity : world.entities()) {
        if (entity.resource && entity.resource.remaining <= 0.0F)
            continue;
        if (player && entity.authority.owner != player->id) {
            constexpr float extent = Terrain::cellCount * Terrain::spacing;
            const int x =
                std::clamp(static_cast<int>((entity.transform.position.x / extent + 0.5F) *
                                            Player::explorationCells),
                           0,
                           Player::explorationCells - 1);
            const int z =
                std::clamp(static_cast<int>((entity.transform.position.z / extent + 0.5F) *
                                            Player::explorationCells),
                           0,
                           Player::explorationCells - 1);
            if (!player->visible[static_cast<std::size_t>(z * Player::explorationCells + x)])
                continue;
        }
        const ModelHandle handle = modelHandle(entity.renderId());
        const Model* model = resources_.modelOrMarker(handle);
        if (model == nullptr) {
            continue;
        }
        const EntityDefinition* definition = resources_.entityDefinition(entity.renderId());
        const glm::mat4 transform =
            groundedEntityTransform(terrain_, entity.transform, definition, entity.id);
        std::string animation;
        if (definition) {
            const bool moving = entity.authority.directController != 0
                                    ? glm::length(entity.unitControl.directInput) > 0.01F
                                    : entity.unitControl.hasStrategicDestination;
            const std::string animationState =
                moving ? (entity.unitControl.running ? "run" : "walk") : "idle";
            const auto selected = definition->animations.find(animationState);
            if (selected != definition->animations.end())
                animation = selected->second;
        }
        const double animationSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        commandQueue_.submit(
            {handle,
             worldMaterial_,
             transform,
             std::move(animation),
             animationSeconds,
             entity.construction && !entity.construction.complete
                 ? (entity.construction.placementValid ? glm::vec3{0.45F} : glm::vec3{0.85F, 0.12F, 0.10F})
                 : glm::vec3{1.0F}});
    }
    if (player)
        for (const LastKnownEntity& known : player->intelligence) {
            constexpr float extent = Terrain::cellCount * Terrain::spacing;
            const int x = std::clamp(static_cast<int>((known.position.x / extent + 0.5F) *
                                                      Player::explorationCells),
                                     0,
                                     Player::explorationCells - 1),
                      z = std::clamp(static_cast<int>((known.position.z / extent + 0.5F) *
                                                      Player::explorationCells),
                                     0,
                                     Player::explorationCells - 1);
            if (player->visible[static_cast<std::size_t>(z * Player::explorationCells + x)])
                continue;
            const Model* model = resources_.modelOrMarker(modelHandle(known.modelKey));
            if (!model)
                continue;
            const EntityDefinition* definition = resources_.entityDefinition(known.modelKey);
            const Transform shown{known.position, known.rotationDegrees, known.scale};
            const glm::mat4 transform =
                groundedEntityTransform(terrain_, shown, definition, known.id);
            const glm::vec3 tint =
                known.building ? glm::vec3{0.22F, 0.34F, 0.40F} : glm::vec3{0.27F, 0.29F, 0.31F};
            commandQueue_.submit(
                {modelHandle(known.modelKey), rememberedMaterial_, transform, {}, 0.0, tint});
        }
    commandQueue_.sort();
    for (const ModelRenderCommand& command : commandQueue_.commands()) {
        const RenderMaterial& material = materials_.get(command.material);
        material.depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        material.cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        material.blending ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        glDepthMask(material.depthWrite ? GL_TRUE : GL_FALSE);
        shaders_.use(material.shader);
        glUniform4fv(shaders_.uniform(material.shader, "materialTint"),
                     1,
                     glm::value_ptr(material.tint));
        glUniform1f(shaders_.uniform(material.shader, "materialRoughness"), material.roughness);
        const bool tinted = material.rememberedEntity || command.tint != glm::vec3{1.0F};
        glUniform1i(shaders_.uniform(material.shader, "rememberedEntity"), tinted ? 1 : 0);
        if (tinted)
            glUniform3fv(shaders_.uniform(material.shader, "rememberedTint"),
                         1,
                         glm::value_ptr(command.tint));
        const Texture* albedo = material.albedo ? resources_.textureOrMarker(material.albedo)
                                                 : nullptr;
        if (const Model* model = resources_.modelOrMarker(command.model))
            model->draw(modelBindings_,
                        viewProjection,
                        command.transform,
                        command.animation,
                        command.animationSeconds,
                        albedo ? albedo->id() : 0);
    }
    glDepthMask(GL_TRUE);
    glUniform1i(shaders_.uniform(modelProgram_, "rememberedEntity"), 0);
    commandQueue_.clear();
    std::vector<glm::vec2> healthBack, healthDamage, healthRemaining, constructionProgress;
    for (const Entity& entity : world.entities()) {
        const bool constructing = entity.construction && !entity.construction.complete &&
                                  entity.construction.powerRequired > 0.0F;
        const bool damaged = entity.health && entity.health.current > 0.0F &&
                             entity.health.maximum > 0.0F &&
                             entity.health.current < entity.health.maximum - 0.001F;
        if (!constructing && !damaged)
            continue;
        if (player && entity.authority.owner != player->id) {
            constexpr float extent = Terrain::cellCount * Terrain::spacing;
            const int x = std::clamp(
                          static_cast<int>((entity.transform.position.x / extent + 0.5F) *
                                           Player::explorationCells),
                          0,
                          Player::explorationCells - 1),
                      z = std::clamp(
                          static_cast<int>((entity.transform.position.z / extent + 0.5F) *
                                           Player::explorationCells),
                          0,
                          Player::explorationCells - 1);
            if (!player->visible[static_cast<std::size_t>(z * Player::explorationCells + x)])
                continue;
        }
    const EntityDefinition* definition = resources_.entityDefinition(entity.renderId());
        const float height = definition ? definition->selectionHeight : 2.0F;
        const float ground = terrain_.heightAt(entity.transform.position.x,
                                               entity.transform.position.z) +
                             (entity.flight ? entity.transform.position.y : 0.0F);
        const glm::vec4 clip =
            viewProjection * glm::vec4{entity.transform.position.x,
                                       ground + entity.transform.position.y + height + 0.65F,
                                       entity.transform.position.z,
                                       1.0F};
        if (clip.w <= 0.0F)
            continue;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0F || ndc.z > 1.0F)
            continue;
        const float screenX = (ndc.x + 1.0F) * 0.5F * viewportWidth_,
                    screenY = (1.0F - ndc.y) * 0.5F * viewportHeight_;
        if (screenX < 0.0F || screenX > viewportWidth_ || screenY < 0.0F ||
            screenY > viewportHeight_)
            continue;
        const float width = entity.unitControl ? 48.0F : 72.0F, left = screenX - width * 0.5F,
                    top = screenY - 4.0F;
        const float ratio = constructing
                                ? std::clamp(entity.construction.powerProgress /
                                                 entity.construction.powerRequired,
                                             0.0F,
                                             1.0F)
                                : std::clamp(entity.health.current / entity.health.maximum,
                                             0.0F,
                                             1.0F);
        appendHudRectangle(healthBack,
                           left - 2.0F,
                           top - 2.0F,
                           left + width + 2.0F,
                           top + 8.0F,
                           viewportWidth_,
                           viewportHeight_);
        if (!constructing)
            appendHudRectangle(
                healthDamage, left, top, left + width, top + 6.0F, viewportWidth_, viewportHeight_);
        auto& progressVertices = constructing ? constructionProgress : healthRemaining;
        appendHudRectangle(progressVertices,
                           left,
                           top,
                           left + width * ratio,
                           top + 6.0F,
                           viewportWidth_,
                           viewportHeight_);
    }
    if (!healthBack.empty()) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        shaders_.use(hudProgram_);
        glBindVertexArray(hudVao_);
        glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
        const auto draw = [this](const std::vector<glm::vec2>& vertices, const glm::vec3& color) {
            glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(color));
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                         vertices.data(),
                         GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        };
        draw(healthBack, {0.01F, 0.015F, 0.02F});
        draw(healthDamage, {0.55F, 0.07F, 0.05F});
        draw(healthRemaining, {0.16F, 0.78F, 0.20F});
        draw(constructionProgress, {0.95F, 0.72F, 0.12F});
        glBindVertexArray(0);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }
}

void Renderer::drawDebugHud(const RtsCamera& camera, std::size_t entityCount) const {
    renderGraph_.enter(RenderPassKind::overlay);
    std::vector<glm::vec2> vertices;
    std::ostringstream fps;
    fps << std::fixed << std::setprecision(1) << framesPerSecond_;
    std::ostringstream cameraLine;
    cameraLine << std::fixed << std::setprecision(1) << camera.focus().x;
    std::ostringstream cameraZ;
    cameraZ << std::fixed << std::setprecision(1) << camera.focus().z;
    std::ostringstream counts;
    counts << entityCount;
    appendHudText(vertices,
                  Text::format("hud.fps", {fps.str()}),
                  14.0F,
                  14.0F,
                  2.0F,
                  viewportWidth_,
                  viewportHeight_);
    appendHudText(vertices,
                  Text::format("hud.camera", {cameraLine.str(), cameraZ.str()}),
                  14.0F,
                  34.0F,
                  2.0F,
                  viewportWidth_,
                  viewportHeight_);
    appendHudText(
        vertices,
        Text::format("hud.counts", {counts.str(), std::to_string(resources_.modelCount())}),
        14.0F,
        54.0F,
        2.0F,
        viewportWidth_,
        viewportHeight_);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glUniform3f(shaders_.uniform(hudProgram_, "hudColor"), 0.95F, 0.98F, 0.82F);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                 vertices.data(),
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);
    drawText(Text::format("hud.fps", {fps.str()}), 14.0F, 14.0F, 2.0F);
    drawText(Text::format("hud.camera", {cameraLine.str(), cameraZ.str()}), 14.0F, 34.0F, 2.0F);
    drawText(Text::format("hud.counts", {counts.str(), std::to_string(resources_.modelCount())}),
             14.0F,
             54.0F,
             2.0F);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawVisionRanges(const CameraView& camera, const Entity* entity) const {
    renderGraph_.enter(RenderPassKind::overlay);
    if (!entity || !entity->unitControl || !entity->vision)
        return;
    const float elevation =
        terrain_.heightAt(entity->transform.position.x, entity->transform.position.z);
    const float base = entity->vision.sightRange;
    const float effective = effectiveSightRange(base, elevation);
    constexpr int segments = 128;
    const auto drawCircle = [&](float radius, glm::vec3 color, float heightOffset) {
        std::vector<TerrainVertex> vertices;
        vertices.reserve(segments);
        for (int index = 0; index < segments; ++index) {
            const float angle = glm::two_pi<float>() * static_cast<float>(index) / segments;
            const float x = entity->transform.position.x + std::cos(angle) * radius,
                        z = entity->transform.position.z + std::sin(angle) * radius;
            vertices.push_back({{x, terrain_.heightAt(x, z) + heightOffset, z}, {0, 1, 0}, color});
        }
        shaders_.use(program_);
        const glm::mat4 viewProjection = camera.viewProjection();
        glUniformMatrix4fv(shaders_.uniform(program_, "viewProjection"),
                           1,
                           GL_FALSE,
                           glm::value_ptr(viewProjection));
        glUniform3fv(
            shaders_.uniform(program_, "cameraPosition"), 1, glm::value_ptr(camera.position));
        glUniform2f(shaders_.uniform(program_, "fogRange"), 10000.0F, 10001.0F);
        glUniform1i(shaders_.uniform(program_, "useExploration"), 0);
        glBindVertexArray(hudVao_);
        glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(TerrainVertex)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(TerrainVertex),
                              reinterpret_cast<void*>(offsetof(TerrainVertex, position)));
        glVertexAttribPointer(1,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(TerrainVertex),
                              reinterpret_cast<void*>(offsetof(TerrainVertex, normal)));
        glVertexAttribPointer(2,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(TerrainVertex),
                              reinterpret_cast<void*>(offsetof(TerrainVertex, color)));
        glLineWidth(3.0F);
        glDrawArrays(GL_LINE_LOOP, 0, segments);
    };
    drawCircle(base, {0.10F, 0.78F, 1.0F}, 0.18F);
    drawCircle(effective, {1.0F, 0.72F, 0.08F}, 0.24F);
    glLineWidth(1.0F);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindVertexArray(0);
}

void Renderer::drawDetailedDebugHud(const CameraView& camera,
                                    const Entity* entity,
                                    const Player* player,
                                    std::uint32_t seed,
                                    std::uint64_t tick,
                                    std::size_t entityCount) const {
    renderGraph_.enter(RenderPassKind::overlay);
    (void)player;
    const auto number = [](float value) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << value;
        return stream.str();
    };
    const auto vector = [&](glm::vec3 value) {
        return "[" + number(value.x) + ", " + number(value.y) + ", " + number(value.z) + "]";
    };
    std::vector<std::string> lines{
        Text::get("debug.title"),
        Text::format(
            "debug.camera",
            {number(camera.position.x), number(camera.position.y), number(camera.position.z)}),
        Text::format("debug.target",
                     {number(camera.target.x), number(camera.target.y), number(camera.target.z)}),
        Text::format("debug.world", {std::to_string(seed), std::to_string(tick)}),
        Text::format("debug.performance",
                     {number(framesPerSecond_),
                      std::to_string(entityCount),
                      std::to_string(resources_.modelCount())})};
    lines.push_back("TIMINGS (last / average / peak ms)");
    for (const ProfileMetric& metric : profiler_.snapshot()) {
        std::ostringstream timing;
        timing << "  " << metric.name << ": " << std::fixed << std::setprecision(2)
               << metric.lastMilliseconds << " / " << metric.averageMilliseconds << " / "
               << metric.peakMilliseconds;
        lines.push_back(timing.str());
    }
    if (entity) {
        const char* kinds[] = {"decoration", "unit", "building", "resource"};
        lines.push_back("ENTITY (saved component state)");
        lines.push_back("Identity");
        lines.push_back("  id: " + std::to_string(entity->id));
        lines.push_back("  archetype: " + entity->archetype.value);
        lines.push_back("  presentation: " + entity->presentation.value);
        lines.push_back("  kind: " + std::string(kinds[static_cast<unsigned>(entity->kind)]));
        lines.push_back("  owner: " + std::to_string(entity->authority.owner));
        lines.push_back("Transform");
        lines.push_back("  position: " + vector(entity->transform.position));
        lines.push_back("  rotation: " + vector(entity->transform.rotationDegrees));
        lines.push_back("  scale: " + vector(entity->transform.scale));
        if (entity->health) {
            lines.push_back("Health");
            lines.push_back("  current [saved]: " + number(entity->health.current));
            lines.push_back("  maximum [saved]: " + number(entity->health.maximum));
        }
        if (entity->vision) {
            lines.push_back("Vision");
            lines.push_back("  range [resolved]: " + number(entity->vision.sightRange));
        }
        if (entity->unitControl) {
            lines.push_back("Unit");
            lines.push_back("  controller [saved]: " +
                            std::to_string(entity->authority.directController));
            lines.push_back("  order [saved]: " +
                            std::to_string(static_cast<unsigned>(entity->unitControl.order)));
            lines.push_back("  orderTarget [saved]: " +
                            std::to_string(entity->unitControl.orderTarget));
            lines.push_back("  movementSpeed [resolved]: " +
                            number(entity->unitControl.movementSpeed));
        }
        if (entity->gatherer) {
            lines.push_back("Gatherer");
            lines.push_back("  carriedResource [saved]: " + entity->gatherer.carriedResource);
            lines.push_back("  carriedAmount [saved]: " +
                            number(entity->gatherer.carriedAmount));
            lines.push_back("  capacity [resolved]: " + number(entity->gatherer.carryCapacity));
            lines.push_back("  rate [resolved]: " + number(entity->gatherer.gatherPerSecond));
        }
        if (entity->combat) {
            lines.push_back("Combat");
            lines.push_back("  damage [resolved]: " + number(entity->combat.damage));
            lines.push_back("  range [resolved]: " + number(entity->combat.range));
        }
        if (entity->resource) {
            lines.push_back("Resource");
            lines.push_back("  type [saved]: " + entity->resource.type);
            lines.push_back("  remaining [saved]: " + number(entity->resource.remaining));
        }
        if (entity->production) {
            lines.push_back("Production");
            lines.push_back("  speedMultiplier [saved]: " +
                            number(entity->production.productionSpeedMultiplier));
            lines.push_back("  speedUpgrades [saved]: " +
                            std::to_string(entity->production.productionSpeedUpgrades));
            lines.push_back("  queue [saved]: " + std::to_string(entity->production.queue.size()));
        }
        if (entity->buildingUpgrades) {
            lines.push_back("Building upgrades");
            lines.push_back("  level [saved]: " + std::to_string(entity->buildingUpgrades.level));
        }
        if (entity->upgrades) {
            lines.push_back("Selectable upgrades");
            if (entity->upgrades.levels.empty())
                lines.push_back("  none configured");
            for (const auto& [id, level] : entity->upgrades.levels)
                lines.push_back("  " + id + ": " + std::to_string(level));
        }
    } else
        lines.push_back(Text::get("debug.no_entity"));
    if (entity && entity->vision) {
        const float elevation =
            terrain_.heightAt(entity->transform.position.x, entity->transform.position.z);
        lines.push_back("  terrainHeight: " + number(elevation));
        lines.push_back("  effectiveRange [height modified]: " +
                        number(effectiveSightRange(entity->vision.sightRange, elevation)));
        lines.push_back("  circles: cyan=unit range, yellow=height modified");
    }
    const float left = 10.0F, top = 50.0F,
                right = std::min(static_cast<float>(viewportWidth_) - 12.0F, 790.0F),
                bottom = std::min(static_cast<float>(viewportHeight_) - 12.0F,
                                  top + 18.0F + static_cast<float>(lines.size()) * 15.0F);
    std::vector<glm::vec2> panel;
    appendHudRectangle(panel, left, top, right, bottom, viewportWidth_, viewportHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glUniform3f(shaders_.uniform(hudProgram_, "hudColor"), 0.018F, 0.028F, 0.038F);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(panel.size() * sizeof(glm::vec2)),
                 panel.data(),
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(panel.size()));
    for (std::size_t index = 0; index < lines.size(); ++index)
        if (top + 8.0F + index * 15.0F < bottom - 10.0F)
            drawText(lines[index],
                     left + 10.0F,
                     top + 6.0F + index * 15.0F,
                     1.15F,
                     index == 0 ? glm::vec3{0.20F, 0.82F, 0.94F} : glm::vec3{0.88F, 0.94F, 0.86F});
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawResourceHud(const Player& player) const {
    renderGraph_.enter(RenderPassKind::overlay);
    std::vector<glm::vec2> panel;
    appendHudRectangle(panel, 10.0F, 10.0F, 560.0F, 44.0F, viewportWidth_, viewportHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glUniform3f(shaders_.uniform(hudProgram_, "hudColor"), 0.018F, 0.028F, 0.038F);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(panel.size() * sizeof(glm::vec2)),
                 panel.data(),
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(panel.size()));
    const auto amount = [&player](const char* id) {
        const auto found = player.resources.find(id);
        return found == player.resources.end() ? 0.0F : found->second;
    };
    drawText(Text::format("strategy.resources",
                          {std::to_string(static_cast<unsigned>(amount("wood"))),
                           std::to_string(static_cast<unsigned>(amount("stone"))),
                           std::to_string(static_cast<unsigned>(amount("gold"))),
                           std::to_string(static_cast<unsigned>(amount("materials")))}),
             20.0F,
             17.0F,
             1.45F);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawStartMenu(bool startHovered,
                             bool buildHovered,
                             bool loadHovered,
                             bool settingsHovered,
                             bool exitHovered,
                             bool seedFocused,
                             const std::string& seedText,
                             const std::string& playerOneCountry,
                             const std::string& playerTwoCountry) const {
    renderGraph_.enter(RenderPassKind::userInterface);
    const auto drawVertices = [this](const std::vector<glm::vec2>& vertices,
                                     const glm::vec3& color) {
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(color));
        glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    };

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);

    std::vector<glm::vec2> panel;
    appendHudRectangle(panel, 30.0F, 70.0F, 800.0F, 625.0F, viewportWidth_, viewportHeight_);
    drawVertices(panel, {0.035F, 0.055F, 0.075F});

    std::vector<glm::vec2> seedField;
    appendHudRectangle(seedField, 60.0F, 185.0F, 300.0F, 230.0F, viewportWidth_, viewportHeight_);
    drawVertices(seedField,
                 seedFocused ? glm::vec3{0.22F, 0.34F, 0.46F} : glm::vec3{0.10F, 0.16F, 0.22F});

    std::vector<glm::vec2> startButton;
    appendHudRectangle(startButton, 60.0F, 250.0F, 300.0F, 310.0F, viewportWidth_, viewportHeight_);
    drawVertices(startButton,
                 startHovered ? glm::vec3{0.28F, 0.62F, 0.24F} : glm::vec3{0.16F, 0.36F, 0.18F});

    std::vector<glm::vec2> buildButton;
    appendHudRectangle(buildButton, 60.0F, 320.0F, 300.0F, 380.0F, viewportWidth_, viewportHeight_);
    drawVertices(buildButton,
                 buildHovered ? glm::vec3{0.25F, 0.48F, 0.70F} : glm::vec3{0.14F, 0.27F, 0.40F});

    std::vector<glm::vec2> loadButton;
    appendHudRectangle(loadButton, 60.0F, 390.0F, 300.0F, 450.0F, viewportWidth_, viewportHeight_);
    drawVertices(loadButton,
                 loadHovered ? glm::vec3{0.54F, 0.46F, 0.20F} : glm::vec3{0.31F, 0.27F, 0.13F});

    std::vector<glm::vec2> settingsButton;
    appendHudRectangle(settingsButton, 60, 460, 300, 520, viewportWidth_, viewportHeight_);
    drawVertices(settingsButton,
                 settingsHovered ? glm::vec3{0.30F, 0.48F, 0.65F} : glm::vec3{0.14F, 0.25F, 0.36F});
    std::vector<glm::vec2> exitButton;
    appendHudRectangle(exitButton, 60.0F, 530.0F, 300.0F, 590.0F, viewportWidth_, viewportHeight_);
    drawVertices(exitButton,
                 exitHovered ? glm::vec3{0.72F, 0.25F, 0.20F} : glm::vec3{0.40F, 0.16F, 0.14F});
    std::vector<glm::vec2> countryFields;
    appendHudRectangle(
        countryFields, 380.0F, 185.0F, 760.0F, 230.0F, viewportWidth_, viewportHeight_);
    appendHudRectangle(
        countryFields, 380.0F, 250.0F, 760.0F, 295.0F, viewportWidth_, viewportHeight_);
    drawVertices(countryFields, {0.10F, 0.16F, 0.22F});

    std::vector<glm::vec2> text;
    appendHudText(
        text, Text::get("menu.title"), 60.0F, 115.0F, 3.0F, viewportWidth_, viewportHeight_);
    appendHudText(
        text, Text::get("menu.seed"), 60.0F, 163.0F, 2.0F, viewportWidth_, viewportHeight_);
    appendHudText(text,
                  seedText.empty() ? "0" : seedText,
                  72.0F,
                  198.0F,
                  2.0F,
                  viewportWidth_,
                  viewportHeight_);
    appendHudText(
        text, Text::get("menu.start"), 125.0F, 269.0F, 3.0F, viewportWidth_, viewportHeight_);
    appendHudText(
        text, Text::get("menu.build"), 125.0F, 339.0F, 3.0F, viewportWidth_, viewportHeight_);
    appendHudText(
        text, Text::get("menu.load"), 134.0F, 409.0F, 3.0F, viewportWidth_, viewportHeight_);
    appendHudText(
        text, Text::get("menu.settings"), 100.0F, 479.0F, 3.0F, viewportWidth_, viewportHeight_);
    appendHudText(
        text, Text::get("menu.exit"), 134.0F, 549.0F, 3.0F, viewportWidth_, viewportHeight_);
    drawVertices(text, {0.95F, 0.98F, 0.82F});
    drawText(Text::get("menu.title"), 60.0F, 108.0F, 3.0F);
    drawText(Text::get("menu.seed"), 60.0F, 157.0F, 2.0F);
    drawText(seedText.empty() ? "0" : seedText, 72.0F, 192.0F, 2.0F);
    drawText(Text::get("menu.start"), 125.0F, 261.0F, 3.0F);
    drawText(Text::get("menu.build"), 125.0F, 331.0F, 3.0F);
    drawText(Text::get("menu.load"), 134.0F, 401.0F, 3.0F);
    drawText(Text::get("menu.settings"), 100.0F, 471.0F, 3.0F);
    drawText(Text::get("menu.exit"), 134.0F, 541.0F, 3.0F);
    drawText(Text::get("menu.team_a_country"), 380.0F, 157.0F, 2.0F);
    drawText("<  " + playerOneCountry + "  >", 400.0F, 192.0F, 2.0F);
    drawText(Text::get("menu.team_b_country"), 380.0F, 222.0F, 2.0F);
    drawText("<  " + playerTwoCountry + "  >", 400.0F, 257.0F, 2.0F);
    drawText(Text::get("menu.country_controls"), 380.0F, 312.0F, 1.5F);

    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawSettings(const GameConfig& config,
                            std::size_t resolution,
                            int hovered,
                            int binding) const {
    renderGraph_.enter(RenderPassKind::userInterface);
    static constexpr std::array<std::pair<int, int>, 4> sizes{
        {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}}};
    static constexpr const char* actions[] = {"settings.action.forward",
                                              "settings.action.backward",
                                              "settings.action.left",
                                              "settings.action.right",
                                              "settings.action.debug",
                                              "settings.action.pause"};
    static constexpr const char* ids[] = {"forward", "backward", "left", "right", "debug", "pause"};
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_); glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    const auto box = [&](UiRect rect, int id) {
        std::vector<glm::vec2> vertices;
        appendHudRectangle(vertices, rect.left, rect.top, rect.right, rect.bottom,
                           viewportWidth_, viewportHeight_);
        const glm::vec3 color = id == hovered ? glm::vec3{0.22F, 0.38F, 0.52F}
                                               : glm::vec3{0.10F, 0.16F, 0.22F};
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(color));
        glBufferData(
            GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec2), vertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    };
    std::vector<glm::vec2> panel;
    appendHudRectangle(panel, 30, 30, 880, 620, viewportWidth_, viewportHeight_);
    glUniform3f(shaders_.uniform(hudProgram_, "hudColor"), 0.035F, 0.055F, 0.075F);
    glBufferData(GL_ARRAY_BUFFER, panel.size() * sizeof(glm::vec2), panel.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(panel.size()));
    box({60,130,430,168},0); box({60,175,430,213},1);
    for(int row=0;row<4;++row) box({60.0F,285.0F+row*45,430.0F,323.0F+row*45},2+row);
    for(int row=0;row<6;++row) box({470.0F,130.0F+row*45,850.0F,168.0F+row*45},6+row);
    box({470,500,850,538},12); box({470,545,850,583},13);
    const auto percent=[](float value){return std::to_string(static_cast<int>(std::round(value*100.0F)))+"%";};
    drawText(Text::get("settings.title"), 60, 52, 3.0F);
    const std::string enabled = Text::get("settings.on"), disabled = Text::get("settings.off");
    drawText(Text::get("settings.video"),60,100,2.0F,{0.35F,0.72F,0.92F});
    drawText(Text::format("settings.resolution", {std::to_string(sizes[resolution].first),
                                                   std::to_string(sizes[resolution].second)}),75,140,1.5F);
    drawText(Text::format("settings.fullscreen", {config.fullscreen ? enabled : disabled}),75,185,1.5F);
    drawText(Text::get("settings.audio"),60,250,2.0F,{0.35F,0.72F,0.92F});
    drawText(Text::format("settings.master_volume", {percent(config.masterVolume)}),75,295,1.4F);
    drawText(Text::format("settings.music_volume", {percent(config.musicVolume)}),75,340,1.4F);
    drawText(Text::format("settings.effects_volume", {percent(config.effectsVolume)}),75,385,1.4F);
    drawText(Text::format("settings.mute", {config.muted ? enabled : disabled}),75,430,1.4F);
    drawText(Text::get("settings.controls"),470,100,2.0F,{0.35F,0.72F,0.92F});
    for (int i = 0; i < 6; ++i) {
        const auto found = config.keybinds.find(ids[i]);
        const SDL_Keycode key = found == config.keybinds.end() ? 0 : found->second;
        drawText(Text::get(actions[i]),485,140+i*45,1.35F,{0.72F,0.78F,0.82F});
        drawText(binding == i ? Text::get("settings.press_key")
                              : Text::format("settings.bound_to", {SDL_GetKeyName(key)}),
                 625,140+i*45,1.35F,binding==i?glm::vec3{1.0F,0.72F,0.18F}:glm::vec3{0.95F,0.98F,0.82F});
    }
    drawText(Text::get("settings.apply"),485,510,1.5F);
    drawText(Text::get("settings.cancel"),485,555,1.5F);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::regenerateTerrain(std::uint32_t seed) {
    terrainSeed_ = seed;
    terrain_ = Terrain(seed);
    for (const TerrainFoundation& foundation : terrainFoundations_)
        terrain_.applyFoundation(foundation);
    constexpr int chunkSide = Terrain::chunkCellCount + 1;
    constexpr float halfExtent = static_cast<float>(Terrain::cellCount) * Terrain::spacing * 0.5F;

    for (int chunkZ = 0; chunkZ < Terrain::chunksPerSide; ++chunkZ) {
        for (int chunkX = 0; chunkX < Terrain::chunksPerSide; ++chunkX) {
            std::vector<TerrainVertex> vertices;
            vertices.reserve(chunkSide * chunkSide);
            const int startX = chunkX * Terrain::chunkCellCount;
            const int startZ = chunkZ * Terrain::chunkCellCount;
            for (int localZ = 0; localZ < chunkSide; ++localZ) {
                for (int localX = 0; localX < chunkSide; ++localX) {
                    const int gridX = startX + localX;
                    const int gridZ = startZ + localZ;
                    vertices.push_back({{static_cast<float>(gridX) * Terrain::spacing - halfExtent,
                                         terrain_.vertexHeight(gridX, gridZ),
                                         static_cast<float>(gridZ) * Terrain::spacing - halfExtent},
                                        terrain_.normalAt(gridX, gridZ),
                                        terrain_.colorAt(terrain_.normalizedHeight(gridX, gridZ))});
                }
            }
            const std::size_t chunkIndex =
                static_cast<std::size_t>(chunkZ * Terrain::chunksPerSide + chunkX);
            glBindBuffer(GL_ARRAY_BUFFER, terrainChunks_[chunkIndex].vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(TerrainVertex)),
                         vertices.data(),
                         GL_STATIC_DRAW);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Renderer::setTerrainFoundations(const std::vector<TerrainFoundation>& foundations) {
    const auto equal = [](const TerrainFoundation& a, const TerrainFoundation& b) {
        return a.center == b.center && a.innerRadius == b.innerRadius &&
               a.outerRadius == b.outerRadius;
    };
    if (terrainFoundations_.size() == foundations.size() &&
        std::equal(terrainFoundations_.begin(), terrainFoundations_.end(), foundations.begin(), equal))
        return;
    terrainFoundations_ = foundations;
    regenerateTerrain(terrainSeed_);
}

void Renderer::drawPauseMenu(bool resumeHovered, bool settingsHovered, bool exitHovered) const {
    renderGraph_.enter(RenderPassKind::overlay);
    const auto drawVertices = [this](const std::vector<glm::vec2>& vertices,
                                     const glm::vec3& color) {
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(color));
        glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    };

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);

    std::vector<glm::vec2> panel;
    appendHudRectangle(panel, 30.0F, 70.0F, 335.0F, 505.0F, viewportWidth_, viewportHeight_);
    drawVertices(panel, {0.035F, 0.055F, 0.075F});

    std::vector<glm::vec2> resumeButton;
    appendHudRectangle(
        resumeButton, 60.0F, 250.0F, 300.0F, 310.0F, viewportWidth_, viewportHeight_);
    drawVertices(resumeButton,
                 resumeHovered ? glm::vec3{0.28F, 0.62F, 0.24F} : glm::vec3{0.16F, 0.36F, 0.18F});

    std::vector<glm::vec2> settingsButton;
    appendHudRectangle(
        settingsButton, 60.0F, 330.0F, 300.0F, 390.0F, viewportWidth_, viewportHeight_);
    drawVertices(settingsButton,
                 settingsHovered ? glm::vec3{0.30F, 0.48F, 0.65F}
                                 : glm::vec3{0.14F, 0.25F, 0.36F});
    std::vector<glm::vec2> exitButton;
    appendHudRectangle(exitButton, 60.0F, 410.0F, 300.0F, 470.0F, viewportWidth_, viewportHeight_);
    drawVertices(exitButton,
                 exitHovered ? glm::vec3{0.72F, 0.25F, 0.20F} : glm::vec3{0.40F, 0.16F, 0.14F});

    std::vector<glm::vec2> text;
    appendHudText(
        text, Text::get("pause.title"), 91.0F, 115.0F, 4.0F, viewportWidth_, viewportHeight_);
    appendHudText(
        text, Text::get("pause.resume"), 107.0F, 269.0F, 3.0F, viewportWidth_, viewportHeight_);
    appendHudText(
        text, Text::get("menu.exit"), 134.0F, 429.0F, 3.0F, viewportWidth_, viewportHeight_);
    drawVertices(text, {0.95F, 0.98F, 0.82F});
    drawText(Text::get("pause.title"), 91.0F, 106.0F, 4.0F);
    drawText(Text::get("pause.resume"), 107.0F, 261.0F, 3.0F);
    drawText(Text::get("menu.settings"), 100.0F, 341.0F, 3.0F);
    drawText(Text::get("menu.exit"), 134.0F, 421.0F, 3.0F);

    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawBuildHud(PlayerId team,
                            const std::string& entityType,
                            std::size_t entityCount,
                            const std::string& status,
                            bool hovered,
                            bool active) const {
    renderGraph_.enter(RenderPassKind::overlay);
    std::vector<glm::vec2> panel, button, icon;
    const float panelTop = static_cast<float>(viewportHeight_) - 235.0F;
    appendHudRectangle(panel, 18.0F, panelTop, 640.0F, static_cast<float>(viewportHeight_) - 18.0F, viewportWidth_, viewportHeight_);
    appendHudRectangle(button, 30.0F, static_cast<float>(viewportHeight_) - 75.0F, 220.0F, static_cast<float>(viewportHeight_) - 30.0F, viewportWidth_, viewportHeight_);
    appendHudRectangle(icon, 42.0F, static_cast<float>(viewportHeight_) - 67.0F, 70.0F, static_cast<float>(viewportHeight_) - 38.0F, viewportWidth_, viewportHeight_);
    (void)team;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    auto draw = [this](const std::vector<glm::vec2>& vertices, const glm::vec3& color) {
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(color));
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    };
    draw(panel, {0.025F, 0.04F, 0.06F});
    draw(button, active ? glm::vec3{0.38F, 0.58F, 0.24F} : hovered ? glm::vec3{0.32F, 0.56F, 0.22F} : glm::vec3{0.14F, 0.27F, 0.20F});
    draw(icon, {0.18F, 0.76F, 0.88F});
    // Keep the build palette icon-first, while exposing a readable tooltip on hover.
    drawText(entityType, 122.0F, static_cast<float>(viewportHeight_) - 63.0F, 1.25F);
    if (hovered)
        drawText(status, 30.0F, panelTop + 18.0F, 1.15F);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawStrategyHud(const World& world, EntityId selected, const Player* player) const {
    renderGraph_.enter(RenderPassKind::overlay);
    (void)selected;
    std::vector<glm::vec2> map, dots, rememberedFog, visibleFog, rememberedDots;
    const float mapLeft = static_cast<float>(viewportWidth_) - 210.0F, mapTop = 20.0F,
                mapRight = static_cast<float>(viewportWidth_) - 20.0F, mapBottom = 210.0F;
    appendHudRectangle(map, mapLeft, mapTop, mapRight, mapBottom, viewportWidth_, viewportHeight_);
    const float extent = terrain_.worldExtent(), half = extent * 0.5F;
    if (player)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                const int sourceX =
                              x * Player::explorationCells / 16 + Player::explorationCells / 32,
                          sourceZ =
                              z * Player::explorationCells / 16 + Player::explorationCells / 32;
                const auto index =
                    static_cast<std::size_t>(sourceZ * Player::explorationCells + sourceX);
                auto& layer = player->visible[index] ? visibleFog : rememberedFog;
                if (player->visible[index] || player->discovered[index]) {
                    const float l = mapLeft + 8.0F + x * (mapRight - mapLeft - 16.0F) / 16.0F,
                                t = mapTop + 28.0F + z * (mapBottom - mapTop - 36.0F) / 16.0F;
                    appendHudRectangle(layer,
                                       l,
                                       t,
                                       l + (mapRight - mapLeft - 16.0F) / 16.0F + 0.5F,
                                       t + (mapBottom - mapTop - 36.0F) / 16.0F + 0.5F,
                                       viewportWidth_,
                                       viewportHeight_);
                }
            }
    for (const Entity& entity : world.entities()) {
        if (player && entity.authority.owner != player->id) {
            const int gx = std::clamp(
                          static_cast<int>((entity.transform.position.x / extent + 0.5F) *
                                           Player::explorationCells),
                          0,
                          Player::explorationCells - 1),
                      gz = std::clamp(
                          static_cast<int>((entity.transform.position.z / extent + 0.5F) *
                                           Player::explorationCells),
                          0,
                          Player::explorationCells - 1);
            if (!player->visible[static_cast<std::size_t>(gz * Player::explorationCells + gx)])
                continue;
        }
        const float x =
            mapLeft + 8.0F +
            (entity.transform.position.x + half) / extent * (mapRight - mapLeft - 16.0F);
        const float y =
            mapTop + 28.0F +
            (entity.transform.position.z + half) / extent * (mapBottom - mapTop - 36.0F);
        appendHudRectangle(
            dots, x - 2.5F, y - 2.5F, x + 2.5F, y + 2.5F, viewportWidth_, viewportHeight_);
    }
    if (player)
        for (const LastKnownEntity& known : player->intelligence) {
            const int gx = std::clamp(static_cast<int>((known.position.x / extent + 0.5F) *
                                                       Player::explorationCells),
                                      0,
                                      Player::explorationCells - 1),
                      gz = std::clamp(static_cast<int>((known.position.z / extent + 0.5F) *
                                                       Player::explorationCells),
                                      0,
                                      Player::explorationCells - 1);
            if (player->visible[static_cast<std::size_t>(gz * Player::explorationCells + gx)])
                continue;
            const float x = mapLeft + 8.0F +
                            (known.position.x + half) / extent * (mapRight - mapLeft - 16.0F),
                        y = mapTop + 28.0F +
                            (known.position.z + half) / extent * (mapBottom - mapTop - 36.0F);
            appendHudRectangle(rememberedDots,
                               x - 2.0F,
                               y - 2.0F,
                               x + 2.0F,
                               y + 2.0F,
                               viewportWidth_,
                               viewportHeight_);
        }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    auto draw = [this](const std::vector<glm::vec2>& v, const glm::vec3& c) {
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(c));
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(v.size() * sizeof(glm::vec2)),
                     v.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size()));
    };
    draw(map, {0.008F, 0.012F, 0.016F});
    draw(rememberedFog, {0.055F, 0.085F, 0.085F});
    draw(visibleFog, {0.10F, 0.22F, 0.18F});
    draw(rememberedDots, {0.32F, 0.38F, 0.40F});
    draw(dots, {0.92F, 0.82F, 0.25F});
    drawText(Text::get("strategy.minimap"), mapLeft + 8.0F, mapTop + 4.0F, 1.5F);
    // Overlay team markers in their faction colours.
    for (PlayerId team : {PlayerId{1}, PlayerId{2}}) {
        std::vector<glm::vec2> teamDots;
        for (const Entity& e : world.entities())
            if (e.authority.owner == team) {
                if (player && team != player->id) {
                    const int gx = std::clamp(
                                  static_cast<int>((e.transform.position.x / extent + 0.5F) *
                                                   Player::explorationCells),
                                  0,
                                  Player::explorationCells - 1),
                              gz = std::clamp(
                                  static_cast<int>((e.transform.position.z / extent + 0.5F) *
                                                   Player::explorationCells),
                                  0,
                                  Player::explorationCells - 1);
                    if (!player->visible[static_cast<std::size_t>(gz * Player::explorationCells +
                                                                  gx)])
                        continue;
                }
                float x = mapLeft + 8.0F +
                          (e.transform.position.x + half) / extent * (mapRight - mapLeft - 16.0F);
                float y = mapTop + 28.0F +
                          (e.transform.position.z + half) / extent * (mapBottom - mapTop - 36.0F);
                appendHudRectangle(teamDots,
                                   x - 3.5F,
                                   y - 3.5F,
                                   x + 3.5F,
                                   y + 3.5F,
                                   viewportWidth_,
                                   viewportHeight_);
            }
        draw(teamDots, team == 1 ? glm::vec3{0.2F, 0.55F, 1.0F} : glm::vec3{0.95F, 0.22F, 0.18F});
    }
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawUnitHud(const Entity& controlled) const {
    renderGraph_.enter(RenderPassKind::overlay);
    std::vector<glm::vec2> panel, text, healthBack, healthFill, crosshair;
    const float top = static_cast<float>(viewportHeight_) - 125.0F;
    appendHudRectangle(panel,
                       18.0F,
                       top,
                       390.0F,
                       static_cast<float>(viewportHeight_) - 18.0F,
                       viewportWidth_,
                       viewportHeight_);
    appendHudText(text,
                  Text::format("unit.name", {controlled.name}),
                  30.0F,
                  top + 12.0F,
                  2.0F,
                  viewportWidth_,
                  viewportHeight_);
    appendHudText(text,
                  Text::format("unit.team",
                               {Text::get(controlled.authority.owner == 1 ? "build.team.a"
                                                                          : "build.team.b")}),
                  30.0F,
                  top + 38.0F,
                  1.5F,
                  viewportWidth_,
                  viewportHeight_);
    const int current = static_cast<int>(std::max(0.0F, controlled.health.current)),
              maximum = static_cast<int>(std::max(1.0F, controlled.health.maximum));
    appendHudText(text,
                  Text::format("unit.health", {std::to_string(current), std::to_string(maximum)}),
                  30.0F,
                  top + 58.0F,
                  1.5F,
                  viewportWidth_,
                  viewportHeight_);
    appendHudText(
        text, Text::get("unit.escape"), 30.0F, top + 84.0F, 1.25F, viewportWidth_, viewportHeight_);
    appendHudRectangle(
        healthBack, 205.0F, top + 55.0F, 365.0F, top + 70.0F, viewportWidth_, viewportHeight_);
    const float ratio =
        glm::clamp(controlled.health.current / controlled.health.maximum, 0.0F, 1.0F);
    appendHudRectangle(healthFill,
                       205.0F,
                       top + 55.0F,
                       205.0F + 160.0F * ratio,
                       top + 70.0F,
                       viewportWidth_,
                       viewportHeight_);
    const float cx = viewportWidth_ * 0.5F, cy = viewportHeight_ * 0.5F;
    appendHudRectangle(
        crosshair, cx - 10.0F, cy - 1.0F, cx + 10.0F, cy + 1.0F, viewportWidth_, viewportHeight_);
    appendHudRectangle(
        crosshair, cx - 1.0F, cy - 10.0F, cx + 1.0F, cy + 10.0F, viewportWidth_, viewportHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    auto draw = [this](const std::vector<glm::vec2>& v, const glm::vec3& c) {
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(c));
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(v.size() * sizeof(glm::vec2)),
                     v.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size()));
    };
    draw(panel, {0.025F, 0.04F, 0.06F});
    draw(healthBack, {0.18F, 0.06F, 0.05F});
    draw(healthFill, {0.18F, 0.72F, 0.22F});
    draw(crosshair, {0.95F, 0.95F, 0.84F});
    draw(text, {0.93F, 0.97F, 0.84F});
    drawText(Text::format("unit.name", {controlled.name}), 30.0F, top + 8.0F, 2.0F);
    drawText(Text::format(
                 "unit.team",
                 {Text::get(controlled.authority.owner == 1 ? "build.team.a" : "build.team.b")}),
             30.0F,
             top + 34.0F,
             1.5F);
    drawText(Text::format("unit.health", {std::to_string(current), std::to_string(maximum)}),
             30.0F,
             top + 54.0F,
             1.5F);
    drawText(Text::get("unit.escape"), 30.0F, top + 80.0F, 1.25F);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawTownHallHud(const Entity& hall, const std::array<bool, 3>& hovered) const {
    renderGraph_.enter(RenderPassKind::overlay);
    const float height = static_cast<float>(viewportHeight_), top = height - 235.0F;
    std::vector<glm::vec2> panel, buttons[3], queueSlots, queueIcons, progressBack, progressFill;
    appendHudRectangle(panel, 18.0F, top, 640.0F, height - 18.0F, viewportWidth_, viewportHeight_);
    constexpr std::array<float, 3> lefts{30.0F, 230.0F, 430.0F};
    constexpr std::array<float, 3> rights{220.0F, 420.0F, 620.0F};
    for (std::size_t i = 0; i < 3; ++i)
        appendHudRectangle(buttons[i],
                           lefts[i],
                           height - 75.0F,
                           rights[i],
                           height - 30.0F,
                           viewportWidth_,
                           viewportHeight_);
    const std::size_t visible = std::min<std::size_t>(hall.production.queue.size(), 9);
    for (std::size_t i = 0; i < visible; ++i) {
        const float left = 30.0F + static_cast<float>(i) * 54.0F;
        appendHudRectangle(queueSlots,
                           left,
                           top + 82.0F,
                           left + 44.0F,
                           top + 126.0F,
                           viewportWidth_,
                           viewportHeight_);
        // Temporary modern pictogram: a compact equipment tile with a bright core.
        appendHudRectangle(queueIcons,
                           left + 10.0F,
                           top + 92.0F,
                           left + 34.0F,
                           top + 116.0F,
                           viewportWidth_,
                           viewportHeight_);
        appendHudRectangle(queueIcons,
                           left + 16.0F,
                           top + 86.0F,
                           left + 28.0F,
                           top + 122.0F,
                           viewportWidth_,
                           viewportHeight_);
    }
    if (!hall.production.queue.empty()) {
        const ProductionOrder& active = hall.production.queue.front();
        const float ratio = active.durationTicks > 0
                                ? std::clamp(1.0F - static_cast<float>(active.remainingTicks) /
                                                           static_cast<float>(active.durationTicks),
                                             0.0F,
                                             1.0F)
                                : 1.0F;
        appendHudRectangle(progressBack,
                           30.0F,
                           top + 137.0F,
                           620.0F,
                           top + 147.0F,
                           viewportWidth_,
                           viewportHeight_);
        appendHudRectangle(progressFill,
                           30.0F,
                           top + 137.0F,
                           30.0F + 590.0F * ratio,
                           top + 147.0F,
                           viewportWidth_,
                           viewportHeight_);
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    auto draw = [this](const std::vector<glm::vec2>& v, const glm::vec3& c) {
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(c));
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(v.size() * sizeof(glm::vec2)),
                     v.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size()));
    };
    draw(panel, {0.025F, 0.04F, 0.06F});
    for (std::size_t i = 0; i < 3; ++i)
        draw(buttons[i],
             hovered[i] ? glm::vec3{0.32F, 0.56F, 0.22F} : glm::vec3{0.14F, 0.27F, 0.20F});
    draw(queueSlots, {0.08F, 0.12F, 0.15F});
    draw(queueIcons, {0.18F, 0.76F, 0.88F});
    draw(progressBack, {0.06F, 0.08F, 0.10F});
    draw(progressFill, {0.18F, 0.76F, 0.88F});
    drawText(hall.name + " - " + std::to_string(hall.buildingUpgrades.level),
             30.0F,
             top + 14.0F,
             2.4F);
    std::string current = Text::get("town_hall.queue.empty");
    std::ostringstream remaining;
    remaining << std::fixed << std::setprecision(1) << 0.0F;
    if (!hall.production.queue.empty()) {
        const ProductionOrder& active = hall.production.queue.front();
        current = Text::get(active.kind == ProductionKind::trainCharacter ? "town_hall.queue.train"
                            : active.kind == ProductionKind::upgradeBuilding
                                ? "town_hall.queue.upgrade"
                                : "town_hall.queue.research");
        remaining.str("");
        remaining << std::fixed << std::setprecision(1)
                  << static_cast<float>(active.remainingTicks) / 30.0F;
    }
    if (hall.production.queue.empty())
        drawText(Text::get("town_hall.production.empty"), 30.0F, top + 48.0F, 1.7F);
    else
        drawText(
            Text::format("town_hall.production",
                         {std::to_string(hall.production.queue.size()), current, remaining.str()}),
            30.0F,
            top + 48.0F,
            1.7F);
    if (hall.production.queue.size() > visible)
        drawText("+ " + std::to_string(hall.production.queue.size() - visible),
                 526.0F,
                 top + 96.0F,
                 1.35F);
    drawText(hall.buildingUpgrades.level >= 3 ? Text::get("town_hall.max_level")
                                        : Text::get("town_hall.upgrade"),
             42.0F,
             height - 64.0F,
             1.35F);
    drawText(Text::get("town_hall.faster"),
             242.0F,
             height - 64.0F,
             1.35F);
    drawText(Text::get("town_hall.train"), 442.0F, height - 64.0F, 1.35F);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawEntityActionHud(const Entity& entity,
                                   const std::vector<std::string>& labels,
                                   const std::vector<std::string>& costs,
                                   const std::vector<bool>& enabled,
                                   int hovered,
                                   const std::vector<std::string>& queueLabels,
                                   int queueHovered) const {
    if (labels.empty()) return;
    renderGraph_.enter(RenderPassKind::overlay);
    const float height = static_cast<float>(viewportHeight_), top = height - 235.0F;
    std::vector<glm::vec2> panel, buttons, hot, disabled, actionIcons, queueSlots, queueIcons,
        progressBack, progressFill;
    appendHudRectangle(panel, 18, top, 640, height - 18, viewportWidth_, viewportHeight_);
    const std::size_t count = std::min<std::size_t>(labels.size(), 6);
    for (std::size_t i=0;i<count;++i) {
        const float left=30.0F+static_cast<float>(i)*96.0F;
        auto& buttonLayer = i >= enabled.size() || !enabled[i]
                                ? disabled
                                : (i == static_cast<std::size_t>(hovered) ? hot : buttons);
        appendHudRectangle(buttonLayer,left,height-86,left+82,height-30,viewportWidth_,viewportHeight_);
        appendHudRectangle(actionIcons,left+28,height-76,left+54,height-40,viewportWidth_,viewportHeight_);
        appendHudRectangle(actionIcons,left+20,height-68,left+62,height-48,viewportWidth_,viewportHeight_);
    }
    if (entity.production) {
        const std::size_t visible = std::min<std::size_t>(entity.production.queue.size(), 9);
        for (std::size_t i=0;i<visible;++i) {
            const float left=30.0F+static_cast<float>(i)*54.0F;
            appendHudRectangle(queueSlots,left,top+82,left+44,top+126,viewportWidth_,viewportHeight_);
            appendHudRectangle(queueIcons,left+10,top+92,left+34,top+116,viewportWidth_,viewportHeight_);
            appendHudRectangle(queueIcons,left+16,top+86,left+28,top+122,viewportWidth_,viewportHeight_);
        }
        if (!entity.production.queue.empty()) {
            const ProductionOrder& active=entity.production.queue.front();
            const float ratio=active.durationTicks ? std::clamp(1.0F-static_cast<float>(active.remainingTicks)/static_cast<float>(active.durationTicks),0.0F,1.0F):1.0F;
            appendHudRectangle(progressBack,30,top+137,620,top+147,viewportWidth_,viewportHeight_);
            appendHudRectangle(progressFill,30,top+137,30+590*ratio,top+147,viewportWidth_,viewportHeight_);
        }
    }
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); shaders_.use(hudProgram_); glBindVertexArray(hudVao_); glBindBuffer(GL_ARRAY_BUFFER,hudVbo_); glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);
    auto draw=[this](const std::vector<glm::vec2>& v,const glm::vec3& c){ glUniform3fv(shaders_.uniform(hudProgram_,"hudColor"),1,glm::value_ptr(c)); glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(v.size()*sizeof(glm::vec2)),v.data(),GL_DYNAMIC_DRAW); glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size())); };
    draw(panel,{0.025F,0.04F,0.06F}); draw(buttons,{0.14F,0.27F,0.20F}); draw(hot,{0.32F,0.56F,0.22F});
    draw(disabled,{0.16F,0.17F,0.18F});
    draw(actionIcons,{0.18F,0.76F,0.88F}); draw(queueSlots,{0.08F,0.12F,0.15F});
    draw(queueIcons,{0.18F,0.76F,0.88F}); draw(progressBack,{0.06F,0.08F,0.10F}); draw(progressFill,{0.18F,0.76F,0.88F});
    drawText(entity.name,30,top+14,2.0F);
    if(hovered>=0 && static_cast<std::size_t>(hovered)<count) drawText(labels[hovered]+"  "+costs[hovered],30,top+48,1.45F);
    if (queueHovered >= 0 && static_cast<std::size_t>(queueHovered) < queueLabels.size())
        drawText(queueLabels[queueHovered] + "  (click to cancel and refund)",
                 30, top + 48, 1.45F, {1.0F, 0.82F, 0.24F});
    if (entity.production && entity.production.queue.size() > 9)
        drawText("+ " + std::to_string(entity.production.queue.size()-9),526,top+96,1.35F);
    glBindVertexArray(0); glEnable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST);
}

void Renderer::drawSelectionBox(const glm::vec2& start, const glm::vec2& end) const {
    renderGraph_.enter(RenderPassKind::overlay);
    const float left = std::min(start.x, end.x), right = std::max(start.x, end.x);
    const float top = std::min(start.y, end.y), bottom = std::max(start.y, end.y);
    std::vector<glm::vec2> border;
    appendHudRectangle(border, left, top, right, top + 2.0F, viewportWidth_, viewportHeight_);
    appendHudRectangle(border, left, bottom - 2.0F, right, bottom, viewportWidth_, viewportHeight_);
    appendHudRectangle(border, left, top, left + 2.0F, bottom, viewportWidth_, viewportHeight_);
    appendHudRectangle(border, right - 2.0F, top, right, bottom, viewportWidth_, viewportHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glUniform3f(shaders_.uniform(hudProgram_, "hudColor"), 1.0F, 0.82F, 0.05F);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(border.size() * sizeof(glm::vec2)),
                 border.data(),
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(border.size()));
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

std::vector<EntityId> Renderer::unitsInScreenRectangle(const glm::vec2& start,
                                                       const glm::vec2& end,
                                                       const World& world,
                                                       const CameraView& camera,
                                                       PlayerId owner) const {
    const float left = std::min(start.x, end.x), right = std::max(start.x, end.x);
    const float top = std::min(start.y, end.y), bottom = std::max(start.y, end.y);
    const glm::mat4 viewProjection = camera.viewProjection();
    std::vector<EntityId> result;
    for (const Entity& entity : world.entities()) {
        if (entity.authority.owner != owner || entity.resource)
            continue;
        const EntityDefinition* definition = resources_.entityDefinition(entity.renderId());
        const float height = definition ? definition->selectionHeight : 1.8F;
        const float radius = definition ? definition->selectionRadius : 0.6F;
        const float ground = terrain_.heightAt(entity.transform.position.x,
                                               entity.transform.position.z) +
                             (entity.flight ? entity.transform.position.y : 0.0F);
        const glm::vec3 center{
            entity.transform.position.x, ground + height * 0.5F, entity.transform.position.z};
        const std::array<glm::vec3, 6> points{center + glm::vec3{-radius, 0, 0},
                                              center + glm::vec3{radius, 0, 0},
                                              center + glm::vec3{0, -height * 0.5F, 0},
                                              center + glm::vec3{0, height * 0.5F, 0},
                                              center + glm::vec3{0, 0, -radius},
                                              center + glm::vec3{0, 0, radius}};
        float entityLeft = std::numeric_limits<float>::max(), entityRight = -entityLeft,
              entityTop = entityLeft, entityBottom = -entityLeft;
        bool projected = false;
        for (const glm::vec3& point : points) {
            const glm::vec4 clip = viewProjection * glm::vec4{point, 1.0F};
            if (clip.w <= 0.0F)
                continue;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float screenX = (ndc.x + 1.0F) * 0.5F * viewportWidth_,
                        screenY = (1.0F - ndc.y) * 0.5F * viewportHeight_;
            entityLeft = std::min(entityLeft, screenX);
            entityRight = std::max(entityRight, screenX);
            entityTop = std::min(entityTop, screenY);
            entityBottom = std::max(entityBottom, screenY);
            projected = true;
        }
        if (projected && entityRight >= left && entityLeft <= right && entityBottom >= top &&
            entityTop <= bottom)
            result.push_back(entity.id);
    }
    return result;
}

void Renderer::drawUnitSelectionHud(const World& world,
                                    const std::vector<EntityId>& selected) const {
    renderGraph_.enter(RenderPassKind::overlay);
    struct Group {
        std::string model, name;
        std::size_t count;
    };
    std::vector<Group> groups;
    for (EntityId id : selected)
        if (const Entity* entity = world.findEntity(id)) {
            auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& group) {
                return group.model == entity->archetype.value;
            });
            if (found == groups.end())
                groups.push_back({entity->archetype.value, entity->name, 1});
            else
                ++found->count;
        }
    const std::size_t visible = std::min<std::size_t>(groups.size(), 6);
    const float panelHeight = 58.0F + static_cast<float>(visible) * 24.0F;
    const float top = static_cast<float>(viewportHeight_) - panelHeight - 18.0F;
    std::vector<glm::vec2> panel;
    appendHudRectangle(panel,
                       18.0F,
                       top,
                       410.0F,
                       static_cast<float>(viewportHeight_) - 18.0F,
                       viewportWidth_,
                       viewportHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glUniform3f(shaders_.uniform(hudProgram_, "hudColor"), 0.025F, 0.04F, 0.06F);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(panel.size() * sizeof(glm::vec2)),
                 panel.data(),
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(panel.size()));
    glBindVertexArray(0);
    drawText(Text::format("selection.title", {std::to_string(selected.size())}),
             30.0F,
             top + 12.0F,
             2.0F);
    for (std::size_t index = 0; index < visible; ++index)
        drawText(Text::format("selection.group",
                              {groups[index].name, std::to_string(groups[index].count)}),
                 30.0F,
                 top + 40.0F + index * 24.0F,
                 1.35F);
    if (groups.size() > visible)
        drawText(Text::format("selection.more", {std::to_string(groups.size() - visible)}),
                 280.0F,
                 top + 12.0F,
                 1.35F);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawOrderMarkers(const World& world,
                                EntityId selected,
                                const std::vector<EntityId>& selection,
                                const CameraView& camera) const {
    renderGraph_.enter(RenderPassKind::overlay);
    std::vector<glm::vec2> destinations;
    const auto add = [&](EntityId id) {
        const Entity* entity = world.findEntity(id);
        if (!entity || !entity->unitControl || !entity->unitControl.hasStrategicDestination)
            return;
        const glm::vec2 destination{entity->unitControl.strategicDestination.x,
                                    entity->unitControl.strategicDestination.z};
        for (const glm::vec2& existing : destinations)
            if (glm::dot(existing - destination, existing - destination) < 0.25F)
                return;
        destinations.push_back(destination);
    };
    if (selection.empty())
        add(selected);
    else
        for (EntityId id : selection)
            add(id);
    if (destinations.empty())
        return;
    std::vector<glm::vec2> poles, flags;
    const auto screen = [this](glm::vec4 clip) {
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec2{(ndc.x + 1.0F) * 0.5F * viewportWidth_,
                         (1.0F - ndc.y) * 0.5F * viewportHeight_};
    };
    const auto ndc = [this](glm::vec2 pixel) {
        return glm::vec2{pixel.x / static_cast<float>(viewportWidth_) * 2.0F - 1.0F,
                         1.0F - pixel.y / static_cast<float>(viewportHeight_) * 2.0F};
    };
    for (const glm::vec2& destination : destinations) {
        const float ground = terrain_.heightAt(destination.x, destination.y);
        const glm::vec4 clip =
            camera.viewProjection() * glm::vec4{destination.x, ground + 0.15F, destination.y, 1.0F};
        if (clip.w <= 0.0F)
            continue;
        const glm::vec2 base = screen(clip);
        if (base.x < 0 || base.x > viewportWidth_ || base.y < 0 || base.y > viewportHeight_)
            continue;
        appendHudRectangle(poles,
                           base.x - 1.5F,
                           base.y - 30.0F,
                           base.x + 1.5F,
                           base.y + 2.0F,
                           viewportWidth_,
                           viewportHeight_);
        flags.insert(flags.end(),
                     {ndc({base.x + 1.5F, base.y - 30.0F}),
                      ndc({base.x + 20.0F, base.y - 24.0F}),
                      ndc({base.x + 1.5F, base.y - 18.0F})});
    }
    if (poles.empty())
        return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    const auto draw = [this](const std::vector<glm::vec2>& vertices, const glm::vec3& color) {
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(color));
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    };
    draw(poles, {0.92F, 0.92F, 0.82F});
    draw(flags, {1.0F, 0.72F, 0.05F});
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

glm::vec3 Renderer::screenToTerrain(float pixelX, float pixelY, const CameraView& camera) const {
    const glm::vec3 focus = camera.target;
    const float ground = terrain_.heightAt(focus.x, focus.z);
    const glm::mat4 inverseViewProjection = glm::inverse(camera.viewProjection());
    const float x = 2.0F * pixelX / static_cast<float>(viewportWidth_) - 1.0F;
    const float y = 1.0F - 2.0F * pixelY / static_cast<float>(viewportHeight_);
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4{x, y, -1.0F, 1.0F};
    glm::vec4 farPoint = inverseViewProjection * glm::vec4{x, y, 1.0F, 1.0F};
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    const glm::vec3 origin{nearPoint};
    const glm::vec3 direction = glm::normalize(glm::vec3(farPoint - nearPoint));
    glm::vec3 previous = origin;
    float previousDelta = previous.y - terrain_.heightAt(previous.x, previous.z);
    for (float distance = 1.0F; distance <= 350.0F; distance += 1.0F) {
        const glm::vec3 point = origin + direction * distance;
        const float delta = point.y - terrain_.heightAt(point.x, point.z);
        if (previousDelta >= 0.0F && delta <= 0.0F) {
            glm::vec3 low = previous, high = point;
            for (int i = 0; i < 8; ++i) {
                const glm::vec3 middle = (low + high) * 0.5F;
                if (middle.y > terrain_.heightAt(middle.x, middle.z))
                    low = middle;
                else
                    high = middle;
            }
            glm::vec3 result = (low + high) * 0.5F;
            result.y = terrain_.heightAt(result.x, result.z);
            return result;
        }
        previous = point;
        previousDelta = delta;
    }
    return {focus.x, ground, focus.z};
}

EntityId Renderer::pickEntity(float pixelX,
                              float pixelY,
                              const World& world,
                              const CameraView& camera,
                              const Player* player,
                              bool currentlyVisibleOnly) const {
    const float x = 2.0F * pixelX / static_cast<float>(viewportWidth_) - 1.0F;
    const float y = 1.0F - 2.0F * pixelY / static_cast<float>(viewportHeight_);
    const glm::mat4 inverseViewProjection = glm::inverse(camera.viewProjection());
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4{x, y, -1.0F, 1.0F};
    glm::vec4 farPoint = inverseViewProjection * glm::vec4{x, y, 1.0F, 1.0F};
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    const glm::vec3 origin{nearPoint};
    const glm::vec3 direction = glm::normalize(glm::vec3(farPoint - nearPoint));
    EntityId best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (const Entity& entity : world.entities()) {
        if (entity.resource && entity.resource.remaining <= 0.0F)
            continue;
        glm::vec3 pickPosition = entity.transform.position;
        if (player && entity.authority.owner != player->id) {
            constexpr float extent = Terrain::cellCount * Terrain::spacing;
            const int gridX =
                std::clamp(static_cast<int>((entity.transform.position.x / extent + 0.5F) *
                                            Player::explorationCells),
                           0,
                           Player::explorationCells - 1);
            const int gridZ =
                std::clamp(static_cast<int>((entity.transform.position.z / extent + 0.5F) *
                                            Player::explorationCells),
                           0,
                           Player::explorationCells - 1);
            const std::size_t index =
                static_cast<std::size_t>(gridZ * Player::explorationCells + gridX);
            if (!player->discovered[index] || (currentlyVisibleOnly && !player->visible[index]))
                continue;
            if (!player->visible[index]) {
                const auto known =
                    std::find_if(player->intelligence.begin(),
                                 player->intelligence.end(),
                                 [&](const LastKnownEntity& item) { return item.id == entity.id; });
                if (known == player->intelligence.end())
                    continue;
                pickPosition = known->position;
            }
        }
        const EntityDefinition* definition = resources_.entityDefinition(entity.renderId());
        if (!definition)
            continue;
        const bool airborne = entity.flight ||
                              (player && pickPosition.y > 0.5F &&
                               entity.renderId().find("drone") != std::string::npos);
        const float groundHeight = terrain_.heightAt(pickPosition.x, pickPosition.z) +
                                   (airborne ? pickPosition.y : 0.0F);
        const glm::vec3 center{
            pickPosition.x, groundHeight + definition->selectionHeight * 0.5F, pickPosition.z};
        // Ray/ellipsoid intersection keeps tall buildings from acquiring a huge
        // spherical click area over nearby empty terrain.
        const float horizontalRadius = std::max(0.15F, definition->selectionRadius);
        const float verticalRadius = std::max(0.15F, definition->selectionHeight * 0.5F);
        const glm::vec3 inverseRadii{
            1.0F / horizontalRadius, 1.0F / verticalRadius, 1.0F / horizontalRadius};
        const glm::vec3 offset = (origin - center) * inverseRadii;
        const glm::vec3 scaledDirection = direction * inverseRadii;
        const float a = glm::dot(scaledDirection, scaledDirection);
        const float b = glm::dot(offset, scaledDirection);
        const float c = glm::dot(offset, offset) - 1.0F;
        const float discriminant = b * b - a * c;
        if (discriminant < 0.0F)
            continue;
        const float distance = (-b - std::sqrt(discriminant)) / a;
        if (distance >= 0.0F && distance < bestDistance) {
            bestDistance = distance;
            best = entity.id;
        }
    }
    return best;
}

void Renderer::drawEntityOutline(const World& world,
                                 EntityId id,
                                  const CameraView& camera,
                                  const Player* player) const {
    renderGraph_.enter(RenderPassKind::overlay);
    RenderPass pass(RenderPassKind::overlay);
    const Entity* entity = world.findEntity(id);
    if (!entity)
        return;
    std::string modelKey = entity->renderId();
    Transform shown = entity->transform;
    bool remembered = false;
    if (player && entity->authority.owner != player->id) {
        constexpr float extent = Terrain::cellCount * Terrain::spacing;
        const int x = std::clamp(static_cast<int>((shown.position.x / extent + 0.5F) *
                                                  Player::explorationCells),
                                 0,
                                 Player::explorationCells - 1),
                  z = std::clamp(static_cast<int>((shown.position.z / extent + 0.5F) *
                                                  Player::explorationCells),
                                 0,
                                 Player::explorationCells - 1);
        if (!player->visible[static_cast<std::size_t>(z * Player::explorationCells + x)]) {
            const auto known =
                std::find_if(player->intelligence.begin(),
                             player->intelligence.end(),
                             [&](const LastKnownEntity& item) { return item.id == id; });
            if (known == player->intelligence.end())
                return;
            modelKey = known->modelKey;
            shown.position = known->position;
            shown.rotationDegrees = known->rotationDegrees;
            shown.scale = known->scale;
            remembered = true;
        }
    }
    const Model* model = resources_.modelOrMarker(modelHandle(modelKey));
    if (!model)
        return;
    const EntityDefinition* definition = resources_.entityDefinition(modelKey);
    const glm::mat4 transform =
        groundedEntityTransform(terrain_, shown, definition, id, 1.035F);
    std::string animation;
    if (definition) {
        const bool moving = !remembered && (glm::length(entity->unitControl.directInput) > 0.01F ||
                                            entity->unitControl.hasStrategicDestination);
        const auto found = definition->animations.find(
            moving ? (entity->unitControl.running ? "run" : "walk") : "idle");
        if (found != definition->animations.end())
            animation = found->second;
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const RenderMaterial& outlineMaterial = materials_.get(outlineMaterial_);
    outlineMaterial.depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    outlineMaterial.blending ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    glDepthMask(outlineMaterial.depthWrite ? GL_TRUE : GL_FALSE);
    glCullFace(GL_FRONT);
    model->draw(outlineBindings_, camera.viewProjection(), transform, animation, seconds);
    glDepthMask(GL_TRUE);
    glCullFace(GL_BACK);
}

CameraView Renderer::constrainThirdPersonCamera(const CameraView& desired,
                                                const World& world,
                                                EntityId followed) const {
    const glm::vec3 segment = desired.position - desired.target;
    const float desiredDistance = glm::length(segment);
    if (desiredDistance < 0.001F)
        return desired;
    const glm::vec3 direction = segment / desiredDistance;
    float allowed = desiredDistance;
    const float half = terrain_.worldExtent() * 0.5F - 0.5F;
    for (float distance = 0.5F; distance <= desiredDistance; distance += 0.25F) {
        const glm::vec3 point = desired.target + direction * distance;
        if (std::abs(point.x) > half || std::abs(point.z) > half ||
            point.y < terrain_.heightAt(point.x, point.z) + 0.45F) {
            allowed = std::max(0.5F, distance - 0.35F);
            break;
        }
    }
    for (const Entity& entity : world.entities()) {
        if (entity.id == followed)
            continue;
    const EntityDefinition* definition = resources_.entityDefinition(entity.renderId());
        if (!definition || definition->selectionRadius < 1.0F)
            continue;
        const float ground = terrain_.heightAt(entity.transform.position.x,
                                               entity.transform.position.z) +
                             (entity.flight ? entity.transform.position.y : 0.0F);
        const glm::vec3 center{entity.transform.position.x,
                               ground + definition->selectionHeight * 0.5F,
                               entity.transform.position.z};
        const glm::vec3 offset = desired.target - center;
        const float radius = definition->selectionRadius + 0.3F;
        const float b = glm::dot(offset, direction);
        const float c = glm::dot(offset, offset) - radius * radius;
        const float discriminant = b * b - c;
        if (discriminant < 0.0F)
            continue;
        const float hit = -b - std::sqrt(discriminant);
        if (hit > 0.5F && hit < allowed)
            allowed = std::max(0.5F, hit - 0.3F);
    }
    CameraView result = desired;
    result.position = desired.target + direction * allowed;
    result.view = glm::lookAt(result.position, result.target, {0.0F, 1.0F, 0.0F});
    return result;
}

float Renderer::terrainHeightAt(float worldX, float worldZ) const {
    return terrain_.heightAt(worldX, worldZ);
}

} // namespace strategy
