#include "render/Renderer.hpp"

#include "game/RtsCamera.hpp"
#include "localization/Text.hpp"
#include "diagnostics/Logger.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "render/RenderPass.hpp"
#include "particles/ParticleSystem.hpp"
#include "render/UiRenderer.hpp"
#include "ui/UiDocument.hpp"
#include "ui/EntityHudLayout.hpp"
#include "ui/GameHudLayout.hpp"
#include "ui/EntityHudModel.hpp"
#include "world/World.hpp"
#include "world/WorldGeneration.hpp"
#include "world/Vegetation.hpp"

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
#include <map>
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
    glm::vec4 materialWeights{0.0F};
    float traversalClass{0.0F};
    float waterSurfaceHeight{0.0F};
    float waterCoverage{0.0F};
    glm::vec2 waterFlow{0.0F};
    float generatedWaterSurfaceHeight{0.0F};
    float generatedWaterCoverage{0.0F};
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

ShaderHandle createWaterProgram(ShaderManager& shaders) {
    return shaders.loadFiles(
        "water", "assets/shaders/water.vert", "assets/shaders/water.frag");
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

void appendHudLine(std::vector<glm::vec2>& vertices,
                   glm::vec2 start,
                   glm::vec2 end,
                   float thickness,
                   int width,
                   int height) {
    const glm::vec2 direction = end - start;
    const float length = glm::length(direction);
    if (length <= 0.001F) return;
    const glm::vec2 normal{-direction.y / length * thickness * 0.5F,
                            direction.x / length * thickness * 0.5F};
    const auto ndc = [width, height](glm::vec2 point) {
        return glm::vec2{point.x / static_cast<float>(width) * 2.0F - 1.0F,
                         1.0F - point.y / static_cast<float>(height) * 2.0F};
    };
    const glm::vec2 a = ndc(start + normal), b = ndc(start - normal);
    const glm::vec2 c = ndc(end + normal), d = ndc(end - normal);
    vertices.insert(vertices.end(), {a, b, c, c, b, d});
}

bool clipScreenLine(glm::vec2& start, glm::vec2& end, float width, float height) {
    const glm::vec2 delta = end - start;
    float first = 0.0F, last = 1.0F;
    const auto clip = [&](float p, float q) {
        if (std::abs(p) <= 0.0001F) return q >= 0.0F;
        const float ratio = q / p;
        if (p < 0.0F) { if (ratio > last) return false; first = std::max(first, ratio); }
        else { if (ratio < first) return false; last = std::min(last, ratio); }
        return true;
    };
    if (!clip(-delta.x, start.x) || !clip(delta.x, width - start.x) ||
        !clip(-delta.y, start.y) || !clip(delta.y, height - start.y)) return false;
    const glm::vec2 original = start;
    start = original + delta * first;
    end = original + delta * last;
    return true;
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
    (void)entityId;
    if (!definition)
        return 0.0F;
    return definition->groundOffset;
}

glm::mat4 groundedEntityTransform(const Terrain& terrain,
                                  const Transform& shown,
                                  const EntityDefinition* definition,
                                  EntityId entityId,
                                  float modelBaseY,
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
    transform = glm::scale(transform, shown.scale * catalogueScale * scaleMultiplier);
    return glm::translate(transform, {0.0F, -modelBaseY, 0.0F});
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
    iconAtlas_ = IconAtlas::load("assets/icons_atlas.json");
    particleEffects_ = ParticleEffectCatalogue::load();
    particleSystem_ = std::make_unique<ParticleSystem>(particleEffects_);

    program_ = createTerrainProgram(shaders_);
    waterProgram_ = createWaterProgram(shaders_);
    modelProgram_ = createModelProgram(shaders_);
    outlineProgram_ = createOutlineProgram(shaders_);
    hudProgram_ = createHudProgram(shaders_);
    particleRenderer_ = std::make_unique<ParticleRenderer>(shaders_, resources_);
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
                    const float worldX =
                        static_cast<float>(gridX) * Terrain::spacing - halfExtent;
                    const float worldZ =
                        static_cast<float>(gridZ) * Terrain::spacing - halfExtent;
                    vertices.push_back({{worldX,
                                         terrain_.vertexHeight(gridX, gridZ),
                                         worldZ},
                                        terrain_.normalAt(gridX, gridZ),
                                        terrain_.colorAt(worldX, worldZ),
                                        terrain_.materialWeightsAt(worldX, worldZ),
                                        terrain_.traversalAt(worldX, worldZ) ==
                                                TerrainTraversalClass::impassable
                                            ? 1.0F
                                            : (terrain_.traversalAt(worldX, worldZ) ==
                                                       TerrainTraversalClass::difficult
                                                   ? 0.5F
                                                   : 0.0F),
                                        terrain_.waterSurfaceAt(worldX, worldZ),
                                        terrain_.waterCoverageAt(worldX, worldZ),
                                        terrain_.riverFlowAt(worldX, worldZ),
                                        terrain_.generatedWaterSurfaceAt(worldX, worldZ),
                                        terrain_.generatedWaterCoverageAt(worldX, worldZ)});
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
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3,
                                  4,
                                  GL_FLOAT,
                                  GL_FALSE,
                                  sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex,
                                                                   materialWeights)));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex,
                                                                   traversalClass)));
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex,
                                                                   waterSurfaceHeight)));
            glEnableVertexAttribArray(6);
            glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex,
                                                                  waterCoverage)));
            glEnableVertexAttribArray(7);
            glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex, waterFlow)));
            glEnableVertexAttribArray(8);
            glVertexAttribPointer(
                8, 1, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                reinterpret_cast<void*>(offsetof(TerrainVertex, generatedWaterSurfaceHeight)));
            glEnableVertexAttribArray(9);
            glVertexAttribPointer(
                9, 1, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                reinterpret_cast<void*>(offsetof(TerrainVertex, generatedWaterCoverage)));
            glBindVertexArray(0);
            terrainChunks_.push_back(chunk);
        }
    }
}

ParticleEmitterHandle Renderer::emitParticle(const ParticleEmitterDesc& description) {
    return particleSystem_->emit(description);
}

void Renderer::stopParticle(ParticleEmitterHandle emitter, bool removeParticles) {
    particleSystem_->stop(emitter, removeParticles);
}

void Renderer::setParticleEmitterTransform(ParticleEmitterHandle emitter,
                                           glm::vec3 position,
                                           glm::vec3 direction) {
    particleSystem_->setTransform(emitter, position, direction);
}

void Renderer::updateParticles(float deltaSeconds) {
    particleSystem_->update(deltaSeconds);
}

void Renderer::drawParticles(const CameraView& camera) const {
    particleRenderer_->draw(*particleSystem_, particleEffects_, camera);
}

void Renderer::clearParticles() {
    particleSystem_->clear();
}

void Renderer::drawUi(const UiDocument& document) const {
    renderGraph_.enter(RenderPassKind::userInterface);
    ProfileScope profile(profiler_, "render.ui");
    uiRenderer_->draw(document, viewportWidth_, viewportHeight_);
}

void Renderer::drawIcon(const std::string& id, float left, float top, float right, float bottom,
                        const glm::vec3& tint) const {
    const IconRegion* region = iconAtlas_.region(id);
    if (!region)
        return;
    if (!iconAtlasTexture_)
        iconAtlasTexture_ = resources_.requestTexture(iconAtlas_.texture());
    const Texture* texture = resources_.textureOrMarker(iconAtlasTexture_);
    if (!texture)
        return;
    const bool atlasReady = resources_.state(iconAtlasTexture_) == ResourceState::ready;
    if (!atlasReady) {
        uiRenderer_->image(texture->id(), left, top, right, bottom,
                           0.0F, 0.0F, 1.0F, 1.0F, tint,
                           viewportWidth_, viewportHeight_);
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture->id());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    constexpr float texelInset = 0.5F;
    const float u0 = (static_cast<float>(region->x) + texelInset) / iconAtlas_.width();
    const float u1 = (static_cast<float>(region->x + region->width) - texelInset) /
                     iconAtlas_.width();
    // stb_image uploads the source's first (top) scanline as texture row zero. Mapping
    // screen-top to the lower V value both selects top-origin atlas regions and displays
    // their source pixels upright.
    const float v0 = (static_cast<float>(region->y) + texelInset) / iconAtlas_.height();
    const float v1 = (static_cast<float>(region->y + region->height) - texelInset) /
                     iconAtlas_.height();
    uiRenderer_->image(texture->id(), left, top, right, bottom, u0, v0, u1, v1, tint,
                       viewportWidth_, viewportHeight_);
}

Renderer::~Renderer() {
    for (const FoundationMesh& mesh : foundationMeshes_) {
        if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
        if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
    }
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
                                   shaders_.uniform(handle, "useInstancing"),
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
    // Initial terrain is intentionally uploaded in small batches so the loading screen and OS
    // event queue remain responsive. OpenGL ownership never leaves this thread.
    constexpr std::size_t initialChunksPerFrame = 4;
    for (std::size_t uploaded = 0;
         uploaded < initialChunksPerFrame && !pendingInitialTerrainUploads_.empty(); ++uploaded) {
        const auto [chunkX, chunkZ] = pendingInitialTerrainUploads_.back();
        pendingInitialTerrainUploads_.pop_back();
        uploadTerrainChunk(chunkX, chunkZ);
    }
    // Upload a complete connected deformation region in one frame. Splitting adjacent
    // chunks across frames exposes stale shared-edge vertices and produces a visible seam.
    if (!pendingTerrainChunkUploads_.empty()) {
        std::vector<std::pair<int, int>> connected{pendingTerrainChunkUploads_.front()};
        pendingTerrainChunkUploads_.erase(pendingTerrainChunkUploads_.begin());
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (auto pending = pendingTerrainChunkUploads_.begin();
                 pending != pendingTerrainChunkUploads_.end();) {
                const bool adjacent = std::any_of(
                    connected.begin(), connected.end(), [&](const auto& included) {
                        return std::abs(included.first - pending->first) <= 1 &&
                               std::abs(included.second - pending->second) <= 1;
                    });
                if (adjacent) {
                    connected.push_back(*pending);
                    pending = pendingTerrainChunkUploads_.erase(pending);
                    expanded = true;
                } else {
                    ++pending;
                }
            }
        }
        for (const auto [chunkX, chunkZ] : connected)
            uploadTerrainChunk(chunkX, chunkZ);
    }
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
    UiDocument ui = GameHudLayout::loading(
        progress, status, viewportWidth_, viewportHeight_);
    uiRenderer_->draw(ui, viewportWidth_, viewportHeight_);
}

void Renderer::drawText(
    const std::string& text, float x, float y, float scale, const glm::vec3& color) const {
    pendingText_.push_back(
        {text, x, y, std::max(scale * 8.0F, 13.0F), color});
}

void Renderer::drawTerrain(const CameraView& camera,
                           const Player* player,
                           bool terrainDebug,
                           int waterDebugMode) const {
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
    glUniform1i(shaders_.uniform(program_, "useFoundationTexture"), 0);
    glUniform1i(shaders_.uniform(program_, "terrainDebug"), terrainDebug ? 1 : 0);
    glUniform1i(shaders_.uniform(program_, "waterDebugMode"), waterDebugMode);
    glUniform1f(shaders_.uniform(program_, "waterLevel"), terrain_.waterLevel());
    const GLint location = shaders_.uniform(program_, "viewProjection");
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform3fv(
        shaders_.uniform(program_, "cameraPosition"), 1, glm::value_ptr(camera.position));
    const bool closeView = camera.detailDistance < 20.0F;
    glUniform2f(shaders_.uniform(program_, "fogRange"), 140.0F, 280.0F);
    glUniform1i(shaders_.uniform(program_, "useExploration"),
                player && !terrainDebug && waterDebugMode == 0 ? 1 : 0);
    glUniform1f(shaders_.uniform(program_, "explorationExtent"), activeWorldExtent());
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
    constexpr float closeViewRenderDistance = 190.0F;
    for (const TerrainChunk& chunk : terrainChunks_) {
        const float dx = chunk.center.x - focus.x;
        const float dz = chunk.center.z - focus.z;
        const float maximumDistance = closeViewRenderDistance + chunk.radius;
        // Strategy view must retain the complete active map. Distance-culling whole chunks here
        // exposed a hard grid-aligned boundary while water and fog continued rendering beyond it,
        // especially after taller landforms made the missing geometry easier to see. Close view
        // keeps the bounded distance because distant terrain is outside its useful horizon.
        if (closeView && dx * dx + dz * dz > maximumDistance * maximumDistance) {
            continue;
        }
        std::size_t lodLevel;
        if (closeView) {
            // Third-person terrain uses one topology. Independent per-chunk LODs create
            // T-junctions where a deformed fine edge meets a coarser neighboring edge.
            lodLevel = 0U;
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
    for (const FoundationMesh& mesh : foundationMeshes_) {
        if (!foundationTexture_)
            foundationTexture_ = resources_.requestTexture("foundation_concrete");
        resources_.textureOrMarker(foundationTexture_)->bind(4, true);
        glUniform1i(shaders_.uniform(program_, "foundationTexture"), 4);
        glUniform1i(shaders_.uniform(program_, "useFoundationTexture"), 1);
        glUniform1i(shaders_.uniform(program_, "terrainDebug"), 0);
        if (!mesh.visible || mesh.indexCount == 0)
            continue;
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indexCount), GL_UNSIGNED_INT,
                       nullptr);
    }
    glUniform1i(shaders_.uniform(program_, "useFoundationTexture"), 0);
    glUniform1i(shaders_.uniform(program_, "terrainDebug"), 0);
    glUniform1i(shaders_.uniform(program_, "waterDebugMode"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    drawWater(camera, player, waterDebugMode);
}

void Renderer::drawWater(const CameraView& camera, const Player* player, int debugMode) const {
    renderGraph_.enter(RenderPassKind::water);
    ProfileScope profile(profiler_, "render.water");
    RenderPass pass(RenderPassKind::water);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shaders_.use(waterProgram_);
    glUniformMatrix4fv(shaders_.uniform(waterProgram_, "viewProjection"), 1, GL_FALSE,
                       glm::value_ptr(camera.viewProjection()));
    glUniform3fv(shaders_.uniform(waterProgram_, "cameraPosition"), 1,
                 glm::value_ptr(camera.position));
    glUniform1f(shaders_.uniform(waterProgram_, "waterLevel"), terrain_.waterLevel());
    glUniform1f(shaders_.uniform(waterProgram_, "timeSeconds"),
                static_cast<float>(SDL_GetTicks()) * 0.001F);
    glUniform2f(shaders_.uniform(waterProgram_, "fogRange"), 140.0F, 280.0F);
    glUniform1f(shaders_.uniform(waterProgram_, "explorationExtent"), activeWorldExtent());
    glUniform1i(shaders_.uniform(waterProgram_, "explorationMap"), 7);
    glUniform1i(shaders_.uniform(waterProgram_, "useExploration"),
                player && debugMode == 0 ? 1 : 0);
    glUniform1i(shaders_.uniform(waterProgram_, "waterDebugMode"), debugMode);

    const glm::vec3 focus = camera.target;
    constexpr float renderDistance = 190.0F;
    // Water always uses the fine terrain topology. Switching coastline topology by camera
    // distance makes shores crawl and exposes cracks where neighboring LODs disagree.
    constexpr std::size_t lodLevel = 0U;
    for (const TerrainChunk& chunk : terrainChunks_) {
        const float dx = chunk.center.x - focus.x;
        const float dz = chunk.center.z - focus.z;
        const float maximumDistance = renderDistance + chunk.radius;
        if (dx * dx + dz * dz > maximumDistance * maximumDistance)
            continue;
        glBindVertexArray(chunk.vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebos[lodLevel]);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(chunk.indexCounts[lodLevel]),
                       GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

void Renderer::drawWorld(const World& world,
                         const CameraView& camera,
                         const Player* player,
                         bool powerOverlayVisible) const {
    renderGraph_.enter(RenderPassKind::world);
    ProfileScope profile(profiler_, "render.world");
    RenderPass pass(RenderPassKind::world);
    const glm::mat4 viewProjection = camera.viewProjection();
    shaders_.use(modelProgram_);
    glUniform3fv(
        shaders_.uniform(modelProgram_, "cameraPosition"), 1, glm::value_ptr(camera.position));
    glUniform2f(shaders_.uniform(modelProgram_, "fogRange"), 140.0F, 280.0F);
    glUniform1i(shaders_.uniform(modelProgram_, "rememberedEntity"), 0);
    glUniform1i(shaders_.uniform(modelProgram_, "explorationMap"), 7);
    glUniform1f(shaders_.uniform(modelProgram_, "explorationExtent"), activeWorldExtent());

    for (const Entity& entity : world.entities()) {
        if (entity.resource && entity.resource.remaining <= 0.0F)
            continue;
        const ModelHandle handle = modelHandle(entity.renderId());
        const Model* model = resources_.modelOrMarker(handle);
        if (model == nullptr) {
            continue;
        }
        const EntityDefinition* definition = resources_.entityDefinition(entity.renderId());
        const glm::mat4 transform =
            groundedEntityTransform(terrain_, entity.transform, definition, entity.id,
                                    model->baseY());
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
        glm::vec3 tint = entity.construction && !isOperational(entity)
            ? (entity.construction.placementValid ? glm::vec3{0.45F}
                                                  : glm::vec3{0.85F, 0.12F, 0.10F})
            : glm::vec3{1.0F};
        // Power information is presentation-only. It is derived from authoritative power
        // state and shown only for the local player's devices, so the overlay cannot reveal
        // hidden enemy infrastructure.
        if (powerOverlayVisible && player && entity.authority.owner == player->id && entity.power) {
            if (!entity.power.enabled || entity.power.gridId == 0 ||
                entity.power.state == PowerOperationalState::offline) {
                tint = {0.62F, 0.20F, 0.16F};
            } else if (entity.power.state == PowerOperationalState::underpowered) {
                tint = {0.96F, 0.62F, 0.12F};
            } else {
                tint = {0.18F, 0.88F, 0.30F};
            }
        }
        commandQueue_.submit(
            {handle,
             worldMaterial_,
             transform,
             std::move(animation),
             animationSeconds,
             tint,
             player && entity.authority.owner != player->id ? 1 : 0});
    }
    if (player)
        for (const LastKnownEntity& known : player->intelligence) {
            const Model* model = resources_.modelOrMarker(modelHandle(known.modelKey));
            if (!model)
                continue;
            const EntityDefinition* definition = resources_.entityDefinition(known.modelKey);
            const Transform shown{known.position, known.rotationDegrees, known.scale};
            const glm::mat4 transform =
                groundedEntityTransform(terrain_, shown, definition, known.id, model->baseY());
            const glm::vec3 tint =
                known.building ? glm::vec3{0.22F, 0.34F, 0.40F} : glm::vec3{0.27F, 0.29F, 0.31F};
            commandQueue_.submit(
                {modelHandle(known.modelKey), rememberedMaterial_, transform, {}, 0.0, tint, -1});
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
        glUniform1i(shaders_.uniform(material.shader, "visibilityMode"),
                    command.visibilityMode);
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
        const bool constructing = entity.construction && !isOperational(entity) &&
                                  entity.construction.powerRequired > 0.0F;
        const bool damaged = entity.health && entity.health.current > 0.0F &&
                             entity.health.maximum > 0.0F &&
                             entity.health.current < entity.health.maximum - 0.001F;
        if (!constructing && !damaged)
            continue;
        if (player && entity.authority.owner != player->id) {
            const glm::ivec2 cell = activeMapArea().gridCell(
                {entity.transform.position.x, entity.transform.position.z},
                Player::explorationCells);
            if (!player->visible[static_cast<std::size_t>(
                    cell.y * Player::explorationCells + cell.x)])
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

void Renderer::drawVegetation(const VegetationField& vegetation,
                              const CameraView& camera,
                              const Player* player) const {
    renderGraph_.enter(RenderPassKind::world);
    ProfileScope profile(profiler_, "render.vegetation");
    const glm::mat4 viewProjection = camera.viewProjection();
    std::map<std::string, std::vector<glm::mat4>> batches;
    constexpr float renderDistance = 300.0F;
    for (const VegetationChunk& chunk : vegetation.chunks()) {
        const glm::vec2 delta = chunk.center - glm::vec2{camera.position.x, camera.position.z};
        const float maximumDistance = renderDistance + chunk.radius;
        if (glm::dot(delta, delta) > maximumDistance * maximumDistance) continue;
        for (const VegetationInstance& instance : chunk.instances) {
            const std::string& renderId = instance.presentation.value;
            const Model* model = resources_.modelOrMarker(modelHandle(renderId));
            if (!model) continue;
            batches[renderId].push_back(groundedEntityTransform(
                terrain_, instance.transform, resources_.entityDefinition(renderId), 0,
                model->baseY()));
        }
    }
    if (batches.empty()) return;

    shaders_.use(modelProgram_);
    glUniform3fv(shaders_.uniform(modelProgram_, "cameraPosition"), 1,
                 glm::value_ptr(camera.position));
    glUniform2f(shaders_.uniform(modelProgram_, "fogRange"), 140.0F, 280.0F);
    glUniform1i(shaders_.uniform(modelProgram_, "rememberedEntity"), 0);
    glUniform1i(shaders_.uniform(modelProgram_, "visibilityMode"), player ? 1 : 0);
    glUniform1i(shaders_.uniform(modelProgram_, "explorationMap"), 7);
    glUniform1f(shaders_.uniform(modelProgram_, "explorationExtent"), activeWorldExtent());
    const RenderMaterial& material = materials_.get(worldMaterial_);
    // Most natural ground-cover meshes use crossed alpha cards and need both faces.
    glDisable(GL_CULL_FACE);
    glUniform4fv(shaders_.uniform(modelProgram_, "materialTint"), 1,
                 glm::value_ptr(material.tint));
    glUniform1f(shaders_.uniform(modelProgram_, "materialRoughness"), material.roughness);
    for (const auto& [renderId, transforms] : batches)
        if (const Model* model = resources_.modelOrMarker(modelHandle(renderId)))
            model->drawInstanced(modelBindings_, viewProjection, transforms);
    glEnable(GL_CULL_FACE);
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
        glUniform1i(shaders_.uniform(program_, "terrainDebug"), 0);
        glBindVertexArray(hudVao_);
        glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(TerrainVertex)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glDisableVertexAttribArray(3);
        glVertexAttrib4f(3, 0.0F, 0.0F, 0.0F, 0.0F);
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

void Renderer::drawResourceFieldDebug(const ResourceLayout& layout,
                                      const CameraView& camera) const {
    renderGraph_.enter(RenderPassKind::overlay);
    constexpr int segments = 64;
    const auto colorFor = [](const ResourceFieldId& id) {
        if (id.value.find("scrap") != std::string::npos)
            return glm::vec3{1.0F, 0.72F, 0.18F};
        if (id.value.find("oil") != std::string::npos)
            return glm::vec3{0.24F, 0.78F, 1.0F};
        if (id.value.find("uranium") != std::string::npos)
            return glm::vec3{0.30F, 1.0F, 0.34F};
        return glm::vec3{0.92F, 0.42F, 1.0F};
    };
    std::vector<TerrainVertex> lines;
    for (const GeneratedResourceField& field : layout.fields) {
        const glm::vec3 color = colorFor(field.definition);
        for (int index = 0; index < segments; ++index) {
            const float first = glm::two_pi<float>() * static_cast<float>(index) / segments;
            const float second = glm::two_pi<float>() * static_cast<float>(index + 1) / segments;
            for (const float angle : {first, second}) {
                const float x = field.center.x + std::cos(angle) * field.radius;
                const float z = field.center.y + std::sin(angle) * field.radius;
                lines.push_back({{x, terrain_.heightAt(x, z) + 0.28F, z}, {0, 1, 0}, color});
            }
        }
        constexpr float marker = 0.8F;
        const float y = terrain_.heightAt(field.center.x, field.center.y) + 0.34F;
        lines.push_back({{field.center.x - marker, y, field.center.y}, {0, 1, 0}, color});
        lines.push_back({{field.center.x + marker, y, field.center.y}, {0, 1, 0}, color});
        lines.push_back({{field.center.x, y, field.center.y - marker}, {0, 1, 0}, color});
        lines.push_back({{field.center.x, y, field.center.y + marker}, {0, 1, 0}, color});
    }
    for (const GeneratedResourceNode& node : layout.nodes) {
        const glm::vec3 color = colorFor(node.field);
        constexpr float marker = 0.45F;
        const float y = terrain_.heightAt(node.position.x, node.position.y) + 0.42F;
        lines.push_back({{node.position.x - marker, y, node.position.y - marker}, {0, 1, 0}, color});
        lines.push_back({{node.position.x + marker, y, node.position.y + marker}, {0, 1, 0}, color});
        lines.push_back({{node.position.x - marker, y, node.position.y + marker}, {0, 1, 0}, color});
        lines.push_back({{node.position.x + marker, y, node.position.y - marker}, {0, 1, 0}, color});
    }
    if (lines.empty()) return;
    shaders_.use(program_);
    const glm::mat4 viewProjection = camera.viewProjection();
    glUniformMatrix4fv(shaders_.uniform(program_, "viewProjection"), 1, GL_FALSE,
                       glm::value_ptr(viewProjection));
    glUniform3fv(shaders_.uniform(program_, "cameraPosition"), 1,
                 glm::value_ptr(camera.position));
    glUniform2f(shaders_.uniform(program_, "fogRange"), 10000.0F, 10001.0F);
    glUniform1i(shaders_.uniform(program_, "useExploration"), 0);
    glUniform1i(shaders_.uniform(program_, "terrainDebug"), 0);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(TerrainVertex)),
                 lines.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glVertexAttrib4f(3, 0.0F, 0.0F, 0.0F, 0.0F);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          reinterpret_cast<void*>(offsetof(TerrainVertex, position)));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          reinterpret_cast<void*>(offsetof(TerrainVertex, normal)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                          reinterpret_cast<void*>(offsetof(TerrainVertex, color)));
    glLineWidth(2.0F);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
    glLineWidth(1.0F);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindVertexArray(0);
}

void Renderer::drawTerrainDebugHud(glm::vec3 worldPosition,
                                   const ResourceLayout& resources) const {
    renderGraph_.enter(RenderPassKind::userInterface);
    const TerrainSample& sample = terrain_.sampleAt(worldPosition.x, worldPosition.z);
    const auto number = [](float value, int precision = 2) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    };
    const auto traversal = [](TerrainTraversalClass value) {
        switch (value) {
        case TerrainTraversalClass::open: return "open";
        case TerrainTraversalClass::difficult: return "difficult";
        case TerrainTraversalClass::impassable: return "impassable";
        }
        return "unknown";
    };
    const auto buildability = [](TerrainBuildabilityClass value) {
        switch (value) {
        case TerrainBuildabilityClass::buildable: return "buildable";
        case TerrainBuildabilityClass::restricted: return "restricted";
        case TerrainBuildabilityClass::forbidden: return "forbidden";
        }
        return "unknown";
    };

    constexpr float width = 430.0F;
    constexpr float height = 420.0F;
    const float left = std::max(8.0F, (static_cast<float>(viewportWidth_) - width) * 0.5F);
    const float top = 52.0F;
    UiDocument ui;
    ui.modal("terrain_debug.panel", {left, top, left + width, top + height},
             {0.025F, 0.035F, 0.045F});
    const glm::vec2 cursor{worldPosition.x, worldPosition.z};
    std::vector<std::string> containingFields;
    const GeneratedResourceNode* nearestNode = nullptr;
    float nearestNodeDistance = std::numeric_limits<float>::max();
    for (const GeneratedResourceField& field : resources.fields)
        if (glm::distance(cursor, field.center) <= field.radius)
            containingFields.push_back(field.definition.value);
    for (const GeneratedResourceNode& node : resources.nodes) {
        const float distance = glm::distance(cursor, node.position);
        if (distance < nearestNodeDistance) {
            nearestNodeDistance = distance;
            nearestNode = &node;
        }
    }
    std::ostringstream fieldNames;
    for (std::size_t index = 0; index < containingFields.size(); ++index) {
        if (index) fieldNames << ", ";
        fieldNames << containingFields[index];
    }
    const std::vector<std::string> lines{
        Text::get("terrain_debug.title"),
        Text::format("terrain_debug.cursor",
                     {number(worldPosition.x), number(sample.baseHeight), number(worldPosition.z),
                      number(sample.slopeDegrees)}),
        Text::format("terrain_debug.identity", {sample.biome.value, sample.surface.value}),
        Text::format("terrain_debug.rules",
                     {traversal(sample.traversal), buildability(sample.buildability)}),
        Text::format("terrain_debug.fields_a",
                     {number(sample.continentalness, 3), number(sample.erosion, 3)}),
        Text::format("terrain_debug.fields_b",
                     {number(sample.peaks, 3), number(sample.moisture, 3),
                      number(sample.temperature, 3)}),
        Text::format("terrain_debug.landforms_a",
                     {number(sample.plains, 3), number(sample.hills, 3),
                      number(sample.mountainBelt, 3)}),
        Text::format("terrain_debug.landforms_b",
                     {number(sample.rockyOutcrops, 3), number(sample.coastalShelf, 3),
                      number(sample.basin, 3)}),
        Text::format("terrain_debug.water",
                     {number(terrain_.waterLevel()), number(sample.waterDepth),
                      sample.submerged ? "yes" : "no"}),
        Text::format("terrain_debug.hydrology",
                     {number(sample.waterSurfaceHeight), number(sample.drainage, 3),
                      sample.river ? "river" : sample.lake ? "lake" :
                      sample.waterKind == WaterKind::ocean ? "ocean" :
                      sample.wetland ? "wetland" : "none",
                      number(sample.waterCoverage, 3)}),
        Text::format("terrain_debug.river",
                     {number(sample.riverDistance, 2), number(sample.riverHalfWidth, 2),
                      std::to_string(sample.streamOrder),
                      sample.floodplain ? "yes" : "no"}),
        Text::format("terrain_debug.river_zones",
                     {sample.riverBank ? "yes" : "no",
                      sample.confluence ? "yes" : "no",
                      sample.estuary ? "yes" : "no",
                      sample.sediment ? "yes" : "no"}),
        Text::format("terrain_debug.domains",
                     {number(sample.movementCosts[0]), number(sample.movementCosts[1]),
                      number(sample.movementCosts[2])}),
        Text::format("terrain_debug.grid", {number(Terrain::semanticCellSize, 1)}),
        Text::format("terrain_debug.resource_fields",
                     {std::to_string(resources.fields.size()),
                      fieldNames.str().empty() ? "none" : fieldNames.str()}),
        Text::format("terrain_debug.resource_node",
                     {nearestNode ? nearestNode->archetype.value : "none",
                      nearestNode ? number(nearestNodeDistance, 1) : "-"})};
    for (std::size_t index = 0; index < lines.size(); ++index)
        ui.label("terrain_debug.line." + std::to_string(index),
                 {left + 14.0F, top + 13.0F + static_cast<float>(index) * 27.0F,
                  left + width - 14.0F, top + 38.0F + static_cast<float>(index) * 27.0F},
                 lines[index], index == 0 ? 1.25F : 1.05F,
                 index == 0 ? glm::vec3{1.0F, 0.82F, 0.28F}
                            : glm::vec3{0.92F, 0.95F, 0.98F});
    uiRenderer_->draw(ui, viewportWidth_, viewportHeight_);
}

void Renderer::drawWaterDebugHud(glm::vec3 worldPosition, int mode) const {
    renderGraph_.enter(RenderPassKind::userInterface);
    const TerrainSample& sample = terrain_.sampleAt(worldPosition.x, worldPosition.z);
    const float generatedSurface =
        terrain_.generatedWaterSurfaceAt(worldPosition.x, worldPosition.z);
    const float generatedCoverage =
        terrain_.generatedWaterCoverageAt(worldPosition.x, worldPosition.z);
    const float generatedDepth = std::max(0.0F, generatedSurface - sample.baseHeight);
    const float smoothedDepth = std::max(0.0F, sample.waterSurfaceHeight - sample.baseHeight);
    const glm::vec2 flow = terrain_.riverFlowAt(worldPosition.x, worldPosition.z);
    const auto number = [](float value, int precision = 3) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    };
    const char* modeName = mode == 1 ? "RAW GENERATION"
                           : mode == 2 ? "SMOOTHED RESULT"
                                       : "SMOOTHING DIFFERENCE";
    constexpr float width = 430.0F;
    constexpr float height = 205.0F;
    const float left = std::max(8.0F, (static_cast<float>(viewportWidth_) - width) * 0.5F);
    const float top = 52.0F;
    UiDocument ui;
    ui.modal("water_debug.panel", {left, top, left + width, top + height},
             {0.018F, 0.030F, 0.045F});
    const std::vector<std::string> lines{
        std::string{"WATER DEBUG - "} + modeName + "  F6 TO CYCLE",
        "CURSOR  X " + number(worldPosition.x, 2) + "  Z " + number(worldPosition.z, 2),
        "GENERATED  SURFACE " + number(generatedSurface) + "  DEPTH " +
            number(generatedDepth) + "  COVERAGE " + number(generatedCoverage),
        "SMOOTHED   SURFACE " + number(sample.waterSurfaceHeight) + "  DEPTH " +
            number(smoothedDepth) + "  COVERAGE " + number(sample.waterCoverage),
        "DELTA      SURFACE " + number(sample.waterSurfaceHeight - generatedSurface) +
            "  DEPTH " + number(smoothedDepth - generatedDepth),
        "FLOW       X " + number(flow.x) + "  Z " + number(flow.y) +
            "  LENGTH " + number(glm::length(flow))};
    for (std::size_t index = 0; index < lines.size(); ++index)
        ui.label("water_debug.line." + std::to_string(index),
                 {left + 14.0F, top + 13.0F + static_cast<float>(index) * 29.0F,
                  left + width - 14.0F, top + 39.0F + static_cast<float>(index) * 29.0F},
                 lines[index], index == 0 ? 1.15F : 1.0F,
                 index == 0 ? glm::vec3{0.25F, 0.82F, 1.0F}
                            : glm::vec3{0.90F, 0.95F, 0.98F});
    uiRenderer_->draw(ui, viewportWidth_, viewportHeight_);
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
        if (entity->battery) {
            lines.push_back("Battery");
            lines.push_back("  charge [saved]: " + number(entity->battery.charge));
            lines.push_back("  capacity [resolved]: " + number(entity->battery.capacity));
            lines.push_back("  reserve [resolved]: " +
                            number(entity->battery.reserveThreshold));
            lines.push_back("  returningToCharge [saved]: " +
                            std::string(entity->battery.returningToCharge ? "true" : "false"));
            lines.push_back("  chargerTarget [saved]: " +
                            std::to_string(entity->battery.chargerTarget));
            lines.push_back("  suspendedOrder [saved]: " +
                            std::to_string(static_cast<unsigned>(entity->battery.suspendedOrder)));
            lines.push_back("  suspendedTarget [saved]: " +
                            std::to_string(entity->battery.suspendedTarget));
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

void Renderer::drawResourceHud(const Player& player,
                               const World& world,
                               const DefinitionRegistry& definitions,
                               bool powerOverlayVisible,
                               const UiDocument& layout) const {
    renderGraph_.enter(RenderPassKind::overlay);
    uiRenderer_->draw(layout, viewportWidth_, viewportHeight_);
    const auto amount = [&player](const char* id) {
        const auto found = player.resources.find(id);
        return found == player.resources.end() ? 0.0F : found->second;
    };
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    float generation = 0.0F, demand = 0.0F, suppliedTotal = 0.0F;
    float storage = 0.0F, stored = 0.0F;
    std::vector<std::uint64_t> grids;
    std::size_t connectionCount = 0;
    struct DeviceLine { std::string name; float production; float demand; float supplied;
                        std::string status; float waitingCargo; std::uint64_t grid;
                        std::size_t connections; };
    std::vector<DeviceLine> devices;
    for (const Entity& entity : world.entities()) {
        if (entity.authority.owner != player.id) continue;
        const EntityArchetype* archetype = definitions.archetype(entity.archetype);
        if (!archetype || !archetype->powerDevice) continue;
        const PowerDeviceDefinition* device = definitions.powerDevice(*archetype->powerDevice);
        if (!device) continue;
        const bool active = isOperational(entity);
        const bool enabled = active && entity.power && entity.power.enabled;
        const float produced = enabled ? entity.power.generation : 0.0F;
        const float consumed = enabled ? entity.power.demand : 0.0F;
        generation += produced;
        demand += consumed;
        suppliedTotal += enabled ? entity.power.supplied : 0.0F;
        storage += entity.power ? entity.power.storageCapacity : 0.0F;
        stored += entity.power ? entity.power.stored : 0.0F;
        if (entity.power && entity.power.gridId != 0) grids.push_back(entity.power.gridId);
        connectionCount += entity.power ? entity.power.connections.size() : 0;
        std::string status = enabled ? "POWERED" : "OFFLINE";
        if (enabled && entity.power->state == PowerOperationalState::underpowered) status = "UNDERPOWERED";
        if (enabled && entity.power->state == PowerOperationalState::offline) status = "OFFLINE";
        float waitingCargo = 0.0F;
        if (entity.processor && active) {
            for (const auto& [resource, bufferedAmount] : entity.processor.bufferedInputs)
                waitingCargo += bufferedAmount;
            switch (entity.processor.state) {
            case ProcessorOperationalState::processing: status = "PROCESSING"; break;
            case ProcessorOperationalState::blocked: status = "BLOCKED"; break;
            case ProcessorOperationalState::underpowered: status = "UNDERPOWERED"; break;
            case ProcessorOperationalState::offline: status = "OFFLINE"; break;
            case ProcessorOperationalState::powered: status = "POWERED"; break;
            default: status = "IDLE"; break;
            }
        }
        devices.push_back({entity.name, produced, consumed,
                           active && entity.power ? entity.power.supplied : 0.0F,
                           std::move(status), waitingCargo,
                           entity.power ? entity.power.gridId : 0,
                           entity.power ? entity.power.connections.size() : 0});
    }
    const auto powerText = [](float used, float capacity) {
        float scale = 1.0F;
        const char* unit = "kW";
        const float largest = std::max(std::abs(used), std::abs(capacity));
        if (largest >= 1000000.0F) { scale = 1000000.0F; unit = "GW"; }
        else if (largest >= 1000.0F) { scale = 1000.0F; unit = "MW"; }
        std::ostringstream output;
        output << std::fixed << std::setprecision(scale == 1.0F ? 0 : 1)
               << used / scale << '/' << capacity / scale << ' ' << unit;
        return output.str();
    };
    std::size_t localIndex = 0;
    for (const ResourceDefinition* resource : definitions.enabledResources()) {
        if (resource->storage != ResourceStorageKind::stockpile) continue;
        const UiElement* slot = layout.find("resources.local." + std::to_string(localIndex++));
        if (!slot) continue;
        const float iconSize = slot->bounds.bottom - slot->bounds.top;
        drawIcon(resource->icon, slot->bounds.left, slot->bounds.top,
                 slot->bounds.left + iconSize, slot->bounds.bottom);
        drawText(std::to_string(static_cast<unsigned>(amount(resource->id.c_str()))),
                 slot->bounds.left + iconSize + 4.0F, slot->bounds.top + 4.0F, 1.35F);
    }
    const auto resources = definitions.enabledResources();
    const auto power = std::find_if(resources.begin(), resources.end(),
        [](const ResourceDefinition* resource) { return resource->storage == ResourceStorageKind::network; });
    const std::string powerIcon = power == resources.end() ? "resource_power" : (*power)->icon;
    const UiElement* powerSlot = layout.find("resources.power");
    if (!powerSlot) return;
    const float powerIconSize = powerSlot->bounds.bottom - powerSlot->bounds.top - 6.0F;
    drawIcon(powerIcon, powerSlot->bounds.left, powerSlot->bounds.top + 3.0F,
             powerSlot->bounds.left + powerIconSize, powerSlot->bounds.bottom - 3.0F);
    drawText(powerText(demand, generation), powerSlot->bounds.left + powerIconSize + 4.0F,
             powerSlot->bounds.top + 7.0F, 1.18F,
             demand > generation ? glm::vec3{1.0F, 0.35F, 0.25F} : glm::vec3{0.88F, 0.94F, 0.86F});

    if (!powerOverlayVisible) return;
    const UiElement* powerPanel = layout.find("power.panel");
    if (!powerPanel) return;
    const float top = powerPanel->bounds.top;
    const float textLeft = powerPanel->bounds.left + 14.0F;
    drawText("POWER GRID", textLeft, top + 12.0F, 1.55F, {0.25F, 0.82F, 0.95F});
    std::sort(grids.begin(), grids.end());
    grids.erase(std::unique(grids.begin(), grids.end()), grids.end());
    drawText("Supply / demand: " + powerText(suppliedTotal, demand) +
                 "  capacity " + std::to_string(static_cast<int>(generation)) + " kW",
             textLeft, top + 37.0F, 1.18F);
    drawText("Storage: " + std::to_string(static_cast<int>(stored)) + "/" +
                 std::to_string(static_cast<int>(storage)) + " kWh",
             textLeft, top + 58.0F, 1.15F);
    drawText("Grids: " + std::to_string(grids.size()) + "  Connections: " +
                 std::to_string(connectionCount / 2),
             textLeft, top + 78.0F, 1.05F, {0.62F, 0.78F, 0.82F});
    float y = top + 100.0F;
    std::size_t displayed = 0;
    for (const DeviceLine& device : devices) {
        if (y + 20.0F > powerPanel->bounds.bottom) break;
        const std::string value = device.production > 0.0F
            ? "+" + std::to_string(static_cast<int>(device.production)) + " kW"
            : std::to_string(static_cast<int>(device.supplied)) + "/" +
                  std::to_string(static_cast<int>(device.demand)) + " kW";
        const std::string waiting = device.waitingCargo > 0.0F
            ? "  " + std::to_string(static_cast<int>(device.waitingCargo)) + " RAW WAITING"
            : std::string{};
        drawText(device.name + "  " + value + "  " + device.status + "  G" +
                     std::to_string(device.grid) + " L" + std::to_string(device.connections) + waiting,
                 textLeft, y, 1.1F,
                 device.production > 0.0F ? glm::vec3{0.35F, 0.92F, 0.45F}
                                          : glm::vec3{0.95F, 0.72F, 0.28F});
        y += 22.0F;
        ++displayed;
    }
    if (displayed < devices.size())
        drawText("+" + std::to_string(devices.size() - displayed) + " MORE DEVICES",
                 textLeft, powerPanel->bounds.bottom - 18.0F, 1.0F, {0.72F, 0.78F, 0.82F});
}

void Renderer::drawPowerConnections(const World& world,
                                    const CameraView& camera,
                                    PlayerId owner) const {
    renderGraph_.enter(RenderPassKind::overlay);
    std::vector<glm::vec2> powered, underpowered, failed;
    const auto screen = [this, &camera](const Entity& entity) -> std::optional<glm::vec2> {
        const float ground = terrain_.heightAt(entity.transform.position.x, entity.transform.position.z);
        const glm::vec4 clip = camera.viewProjection() *
            glm::vec4{entity.transform.position.x, ground + 2.0F,
                      entity.transform.position.z, 1.0F};
        if (clip.w <= 0.0F) return std::nullopt;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec2{(ndc.x + 1.0F) * 0.5F * viewportWidth_,
                         (1.0F - ndc.y) * 0.5F * viewportHeight_};
    };
    for (const Entity& source : world.entities()) {
        if (source.authority.owner != owner || !source.power) continue;
        const auto start = screen(source);
        if (!start) continue;
        for (EntityId targetId : source.power.connections) {
            if (targetId <= source.id) continue;
            const Entity* target = world.findEntity(targetId);
            if (!target || !target->power || target->authority.owner != owner) continue;
            const auto end = screen(*target);
            if (!end) continue;
            glm::vec2 clippedStart = *start, clippedEnd = *end;
            if (!clipScreenLine(clippedStart, clippedEnd,
                                static_cast<float>(viewportWidth_),
                                static_cast<float>(viewportHeight_))) continue;
            std::vector<glm::vec2>* vertices = &failed;
            const bool connected = source.power.enabled && target->power.enabled &&
                                   source.power.gridId != 0 &&
                                   source.power.gridId == target->power.gridId;
            if (connected) {
                vertices = source.power.state == PowerOperationalState::underpowered ||
                                   target->power.state == PowerOperationalState::underpowered
                               ? &underpowered
                               : &powered;
            }
            appendHudLine(*vertices, clippedStart, clippedEnd, 3.0F,
                          viewportWidth_, viewportHeight_);
        }
    }
    if (powered.empty() && underpowered.empty() && failed.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shaders_.use(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    const auto draw = [this](const std::vector<glm::vec2>& vertices, glm::vec3 color) {
        if (vertices.empty()) return;
        glUniform3fv(shaders_.uniform(hudProgram_, "hudColor"), 1, glm::value_ptr(color));
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                     vertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    };
    draw(powered, {0.18F, 0.90F, 0.32F});
    draw(underpowered, {0.96F, 0.62F, 0.12F});
    draw(failed, {0.95F, 0.28F, 0.18F});
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}



void Renderer::regenerateTerrain(std::uint32_t seed, std::uint32_t chunksPerSide,
                                 TerrainLayoutId layout) {
    clearParticles();
    terrainSeed_ = seed;
    activeTerrainChunksPerSide_ = std::clamp(
        chunksPerSide, 10U, static_cast<std::uint32_t>(Terrain::chunksPerSide));
    terrain_ = Terrain(seed, std::move(layout));
    terrain_.rebuildFoundations(terrainFoundations_);
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
                    const float worldX =
                        static_cast<float>(gridX) * Terrain::spacing - halfExtent;
                    const float worldZ =
                        static_cast<float>(gridZ) * Terrain::spacing - halfExtent;
                    vertices.push_back({{worldX,
                                         terrain_.vertexHeight(gridX, gridZ),
                                         worldZ},
                                        terrain_.normalAt(gridX, gridZ),
                                        terrain_.colorAt(worldX, worldZ),
                                        terrain_.materialWeightsAt(worldX, worldZ),
                                        terrain_.traversalAt(worldX, worldZ) ==
                                                TerrainTraversalClass::impassable
                                            ? 1.0F
                                            : (terrain_.traversalAt(worldX, worldZ) ==
                                                       TerrainTraversalClass::difficult
                                                   ? 0.5F
                                                   : 0.0F),
                                        terrain_.waterSurfaceAt(worldX, worldZ),
                                        terrain_.waterCoverageAt(worldX, worldZ),
                                        terrain_.riverFlowAt(worldX, worldZ),
                                        terrain_.generatedWaterSurfaceAt(worldX, worldZ),
                                        terrain_.generatedWaterCoverageAt(worldX, worldZ)});
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

void Renderer::stageGeneratedTerrain(const Terrain& terrain, std::uint32_t seed,
                                     std::uint32_t chunksPerSide,
                                     const std::vector<TerrainFoundation>& foundations) {
    clearParticles();
    terrainSeed_ = seed;
    activeTerrainChunksPerSide_ = std::clamp(
        chunksPerSide, 10U, static_cast<std::uint32_t>(Terrain::chunksPerSide));
    terrain_ = terrain;
    terrainFoundations_ = foundations;
    terrain_.rebuildFoundations(terrainFoundations_);
    syncFoundationMeshes();
    const MapArea map = activeMapArea();
    const int firstChunk = map.firstTerrainChunk();
    const int chunkEnd = map.terrainChunkEnd();
    const int uploadSide = map.intersectingTerrainChunksPerSide();
    pendingInitialTerrainUploads_.clear();
    pendingInitialTerrainUploads_.reserve(
        static_cast<std::size_t>(uploadSide * uploadSide));
    // Reverse insertion makes pop_back() upload from the visible map's first chunk onward.
    for (int z = chunkEnd - 1; z >= firstChunk; --z)
        for (int x = chunkEnd - 1; x >= firstChunk; --x)
            pendingInitialTerrainUploads_.push_back({x, z});
    initialTerrainUploadCount_ = pendingInitialTerrainUploads_.size();
}

float Renderer::terrainUploadProgress() const {
    if (initialTerrainUploadCount_ == 0) return 1.0F;
    return 1.0F - static_cast<float>(pendingInitialTerrainUploads_.size()) /
                      static_cast<float>(initialTerrainUploadCount_);
}

void Renderer::setTerrainFoundations(const std::vector<TerrainFoundation>& foundations) {
    const auto equal = [](const TerrainFoundation& a, const TerrainFoundation& b) {
        return a.sourceEntity == b.sourceEntity && a.shape == b.shape &&
               a.center == b.center && a.innerRadius == b.innerRadius &&
               a.outerRadius == b.outerRadius && a.edgeFalloff == b.edgeFalloff &&
               a.halfExtents == b.halfExtents &&
               a.rotationDegrees == b.rotationDegrees && a.gradient == b.gradient &&
               a.influence == b.influence && a.sourceSlopeDegrees == b.sourceSlopeDegrees &&
               a.requiresSlab == b.requiresSlab;
    };
    if (terrainFoundations_.size() == foundations.size() &&
        std::equal(terrainFoundations_.begin(), terrainFoundations_.end(), foundations.begin(), equal))
        return;
    terrainFoundations_ = foundations;
    terrain_.rebuildFoundations(terrainFoundations_);
    syncFoundationMeshes();
    for (const auto chunk : terrain_.dirtyChunks())
        if (std::find(pendingTerrainChunkUploads_.begin(), pendingTerrainChunkUploads_.end(), chunk) ==
            pendingTerrainChunkUploads_.end())
            pendingTerrainChunkUploads_.push_back(chunk);
    terrain_.clearDirtyChunks();
}

void Renderer::syncFoundationMeshes() {
    std::vector<std::uint64_t> expected;
    for (const TerrainFoundation& foundation : terrainFoundations_)
        if (foundation.requiresSlab)
            expected.push_back(foundation.sourceEntity);
    const bool unchanged = foundationMeshes_.size() == expected.size() &&
        std::equal(foundationMeshes_.begin(), foundationMeshes_.end(), expected.begin(),
                   [](const FoundationMesh& mesh, std::uint64_t source) {
                       return mesh.sourceEntity == source;
                   });
    if (unchanged)
        return;
    for (const FoundationMesh& mesh : foundationMeshes_) {
        if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
        if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
    }
    foundationMeshes_.clear();
    for (const TerrainFoundation& foundation : terrainFoundations_) {
        if (!foundation.requiresSlab)
            continue;
        std::vector<TerrainVertex> vertices;
        std::vector<std::uint32_t> indices;
        const glm::vec3 normal = glm::normalize(
            glm::vec3{-foundation.gradient.x, 1.0F, -foundation.gradient.y});
        const glm::vec3 sideColor{0.20F, 0.22F, 0.23F};
        const float excessSlope = std::max(foundation.sourceSlopeDegrees - 5.0F, 0.0F);
        const float thickness = std::clamp(
            0.15F + std::tan(glm::radians(excessSlope)) * foundation.outerRadius,
            0.15F, 2.5F);
        const auto height = [&](glm::vec2 point) {
            const glm::vec2 offset = point - glm::vec2{foundation.center.x, foundation.center.z};
            return foundation.center.y + glm::dot(foundation.gradient, offset);
        };
        if (foundation.shape == FootprintShape::circle) {
            constexpr std::uint32_t segments = 24;
            for (std::uint32_t i = 0; i < segments; ++i) {
                const float angle = glm::two_pi<float>() * static_cast<float>(i) / segments;
                const glm::vec2 point{foundation.center.x + std::cos(angle) * foundation.outerRadius,
                                      foundation.center.z + std::sin(angle) * foundation.outerRadius};
                vertices.push_back({{point.x, height(point) - 0.06F, point.y}, normal, sideColor});
                vertices.push_back({{point.x, height(point) - thickness, point.y},
                                    {std::cos(angle), 0.0F, std::sin(angle)}, sideColor});
            }
            for (std::uint32_t i = 0; i < segments; ++i) {
                const std::uint32_t next = (i + 1) % segments;
                indices.insert(indices.end(), {i * 2, i * 2 + 1, next * 2 + 1,
                                               i * 2, next * 2 + 1, next * 2});
            }
        } else {
            const float radians = glm::radians(foundation.rotationDegrees);
            const float c = std::cos(radians), s = std::sin(radians);
            const std::array<glm::vec2, 4> local{{{-foundation.halfExtents.x, -foundation.halfExtents.y},
                                                   {foundation.halfExtents.x, -foundation.halfExtents.y},
                                                   {foundation.halfExtents.x, foundation.halfExtents.y},
                                                   {-foundation.halfExtents.x, foundation.halfExtents.y}}};
            for (const glm::vec2 p : local) {
                const glm::vec2 point{foundation.center.x + c * p.x - s * p.y,
                                      foundation.center.z + s * p.x + c * p.y};
                vertices.push_back({{point.x, height(point) - 0.06F, point.y}, normal, sideColor});
                vertices.push_back({{point.x, height(point) - thickness, point.y}, normal, sideColor});
            }
            for (std::uint32_t i = 0; i < 4; ++i) {
                const std::uint32_t next = (i + 1) % 4;
                indices.insert(indices.end(), {i * 2, i * 2 + 1, next * 2 + 1,
                                               i * 2, next * 2 + 1, next * 2});
            }
        }
        FoundationMesh mesh;
        mesh.sourceEntity = foundation.sourceEntity;
        mesh.center = foundation.center;
        mesh.halfExtents = foundation.halfExtents;
        mesh.rotationDegrees = foundation.rotationDegrees;
        mesh.indexCount = static_cast<std::uint32_t>(indices.size());
        glGenVertexArrays(1, &mesh.vao); glGenBuffers(1, &mesh.vbo); glGenBuffers(1, &mesh.ebo);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(TerrainVertex)),
                     vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                     indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, position)));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, normal)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, color)));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, materialWeights)));
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, traversalClass)));
        glBindVertexArray(0);
        foundationMeshes_.push_back(mesh);
    }
}

void Renderer::uploadTerrainChunk(int chunkX, int chunkZ) {
    constexpr int chunkSide = Terrain::chunkCellCount + 1;
    constexpr float halfExtent = static_cast<float>(Terrain::cellCount) * Terrain::spacing * 0.5F;
    std::vector<TerrainVertex> vertices;
    vertices.reserve(chunkSide * chunkSide);
    const int startX = chunkX * Terrain::chunkCellCount;
    const int startZ = chunkZ * Terrain::chunkCellCount;
    for (int localZ = 0; localZ < chunkSide; ++localZ)
        for (int localX = 0; localX < chunkSide; ++localX) {
            const int x = startX + localX, z = startZ + localZ;
            const float worldX = x * Terrain::spacing - halfExtent;
            const float worldZ = z * Terrain::spacing - halfExtent;
            vertices.push_back({{worldX, terrain_.vertexHeight(x, z), worldZ},
                                terrain_.normalAt(x, z), terrain_.colorAt(worldX, worldZ),
                                terrain_.materialWeightsAt(worldX, worldZ),
                                terrain_.traversalAt(worldX, worldZ) ==
                                        TerrainTraversalClass::impassable
                                    ? 1.0F
                                   : (terrain_.traversalAt(worldX, worldZ) ==
                                               TerrainTraversalClass::difficult
                                           ? 0.5F
                                           : 0.0F),
                                terrain_.waterSurfaceAt(worldX, worldZ),
                                terrain_.waterCoverageAt(worldX, worldZ),
                                terrain_.riverFlowAt(worldX, worldZ),
                                terrain_.generatedWaterSurfaceAt(worldX, worldZ),
                                terrain_.generatedWaterCoverageAt(worldX, worldZ)});
        }
    const std::size_t index = static_cast<std::size_t>(chunkZ * Terrain::chunksPerSide + chunkX);
    if (index < terrainChunks_.size()) {
        glBindBuffer(GL_ARRAY_BUFFER, terrainChunks_[index].vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(vertices.size() * sizeof(TerrainVertex)), vertices.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}




void Renderer::drawStrategyHud(const World& world, EntityId selected, const Player* player,
                               const UiDocument& layout) const {
    renderGraph_.enter(RenderPassKind::overlay);
    (void)selected;
    std::vector<glm::vec2> dots, rememberedFog, visibleFog, rememberedDots;
    const UiElement* mapElement = layout.find("strategy.minimap");
    if (!mapElement) return;
    const float mapLeft = mapElement->bounds.left, mapTop = mapElement->bounds.top,
                mapRight = mapElement->bounds.right, mapBottom = mapElement->bounds.bottom;
    uiRenderer_->draw(layout, viewportWidth_, viewportHeight_);
    const MapArea map = activeMapArea();
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
            const glm::ivec2 cell = activeMapArea().gridCell(
                {entity.transform.position.x, entity.transform.position.z},
                Player::explorationCells);
            if (!player->visible[static_cast<std::size_t>(
                    cell.y * Player::explorationCells + cell.x)])
                continue;
        }
        const glm::vec2 normalized = map.normalized(
            {entity.transform.position.x, entity.transform.position.z});
        const float x = mapLeft + 8.0F + normalized.x * (mapRight - mapLeft - 16.0F);
        const float y = mapTop + 28.0F + normalized.y * (mapBottom - mapTop - 36.0F);
        appendHudRectangle(
            dots, x - 2.5F, y - 2.5F, x + 2.5F, y + 2.5F, viewportWidth_, viewportHeight_);
    }
    if (player)
        for (const LastKnownEntity& known : player->intelligence) {
            const glm::ivec2 cell = activeMapArea().gridCell(
                {known.position.x, known.position.z}, Player::explorationCells);
            if (player->visible[static_cast<std::size_t>(
                    cell.y * Player::explorationCells + cell.x)])
                continue;
            const glm::vec2 normalized = map.normalized({known.position.x, known.position.z});
            const float x = mapLeft + 8.0F + normalized.x * (mapRight - mapLeft - 16.0F),
                        y = mapTop + 28.0F + normalized.y * (mapBottom - mapTop - 36.0F);
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
    draw(rememberedFog, {0.055F, 0.085F, 0.085F});
    draw(visibleFog, {0.10F, 0.22F, 0.18F});
    draw(rememberedDots, {0.32F, 0.38F, 0.40F});
    draw(dots, {0.92F, 0.82F, 0.25F});
    // Overlay team markers in their faction colours.
    for (PlayerId team : {PlayerId{1}, PlayerId{2}}) {
        std::vector<glm::vec2> teamDots;
        for (const Entity& e : world.entities())
            if (e.authority.owner == team) {
                if (player && team != player->id) {
                    const glm::ivec2 cell = activeMapArea().gridCell(
                        {e.transform.position.x, e.transform.position.z},
                        Player::explorationCells);
                    if (!player->visible[static_cast<std::size_t>(
                            cell.y * Player::explorationCells + cell.x)])
                        continue;
                }
                const glm::vec2 normalized =
                    map.normalized({e.transform.position.x, e.transform.position.z});
                float x = mapLeft + 8.0F + normalized.x * (mapRight - mapLeft - 16.0F);
                float y = mapTop + 28.0F + normalized.y * (mapBottom - mapTop - 36.0F);
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

void Renderer::drawEntityHud(const EntityHudModel& model,
                             const UiDocument& layout) const {
    renderGraph_.enter(RenderPassKind::overlay);
    uiRenderer_->draw(layout, viewportWidth_, viewportHeight_);

    if (const UiElement* selectionPanel = layout.find("selection.panel")) {
        const std::size_t visible = std::min<std::size_t>(model.selectionGroups.size(), 6);
        for (std::size_t i = 0; i < visible; ++i) {
            const auto& group = model.selectionGroups[i];
            const UiElement* row =
                layout.find(EntityHudLayout::selectionElementId(group.archetype));
            if (!row) continue;
            const float padding = std::max(2.0F, (row->bounds.bottom - row->bounds.top) * 0.1F);
            const float iconSize = row->bounds.bottom - row->bounds.top - padding * 2.0F;
            drawIcon(group.icon, row->bounds.left + padding, row->bounds.top + padding,
                     row->bounds.left + padding + iconSize, row->bounds.bottom - padding);
        }
        return;
    }

    if (const UiElement* portrait = layout.find("entity.portrait"))
        drawIcon(model.portraitIcon, portrait->bounds.left, portrait->bounds.top,
                 portrait->bounds.right, portrait->bounds.bottom);

    for (std::size_t i = 0; i < model.cards.size(); ++i)
        if (const UiElement* card = layout.find("entity.card." + std::to_string(i)))
            drawIcon(model.cards[i].icon, card->bounds.left, card->bounds.top,
                     card->bounds.right, card->bounds.bottom);

    for (const HudActionModel& action : model.actions)
        if (const UiElement* element =
                layout.find(EntityHudLayout::actionElementId(action.id))) {
            const float padding = std::max(3.0F,
                (element->bounds.bottom - element->bounds.top) * 0.08F);
            const glm::vec3 tint = action.enabled ? glm::vec3{1.0F} : glm::vec3{0.34F};
            drawIcon(action.icon, element->bounds.left + padding, element->bounds.top + padding,
                     element->bounds.right - padding, element->bounds.bottom - padding, tint);
        }

    for (std::size_t i = 0; i < model.queue.size(); ++i)
        if (const UiElement* element =
                layout.find(EntityHudLayout::queueElementId(i))) {
            const float padding = std::max(2.0F,
                (element->bounds.bottom - element->bounds.top) * 0.09F);
            drawIcon(model.queue[i].icon, element->bounds.left + padding,
                     element->bounds.top + padding, element->bounds.right - padding,
                     element->bounds.bottom - padding);
        }
}



void Renderer::drawCrosshair() const {
    renderGraph_.enter(RenderPassKind::overlay);
    const float cx = viewportWidth_ * 0.5F, cy = viewportHeight_ * 0.5F;
    std::vector<glm::vec2> vertices;
    appendHudRectangle(vertices, cx - 10.0F, cy - 1.0F, cx + 10.0F, cy + 1.0F,
                       viewportWidth_, viewportHeight_);
    appendHudRectangle(vertices, cx - 1.0F, cy - 10.0F, cx + 1.0F, cy + 10.0F,
                       viewportWidth_, viewportHeight_);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); shaders_.use(hudProgram_);
    glUniform3f(shaders_.uniform(hudProgram_, "hudColor"), 0.95F, 0.95F, 0.84F);
    glBindVertexArray(hudVao_); glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                 vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
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
        if (entity.authority.owner != owner || entity.kind != EntityKind::unit)
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


void Renderer::drawOrderMarkers(const World& world,
                                EntityId selected,
                                const std::vector<EntityId>& selection,
                                const CameraView& camera) const {
    renderGraph_.enter(RenderPassKind::overlay);
    std::vector<glm::vec2> destinations;
    std::vector<const Entity*> cargoEntities;
    const auto add = [&](EntityId id) {
        const Entity* entity = world.findEntity(id);
        if (!entity) return;
        if (entity->gatherer && entity->gatherer.carriedAmount > 0.0F)
            cargoEntities.push_back(entity);
        if (!entity->unitControl || !entity->unitControl.hasStrategicDestination) return;
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
    if (destinations.empty() && cargoEntities.empty())
        return;
    std::vector<glm::vec2> poles, flags, cargo;
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
    for (const Entity* entity : cargoEntities) {
        const glm::vec4 clip = camera.viewProjection() *
            glm::vec4{entity->transform.position.x,
                      entity->transform.position.y + 2.2F,
                      entity->transform.position.z, 1.0F};
        if (clip.w <= 0.0F) continue;
        const glm::vec2 point = screen(clip);
        appendHudRectangle(cargo, point.x - 5.0F, point.y - 5.0F,
                           point.x + 5.0F, point.y + 5.0F,
                           viewportWidth_, viewportHeight_);
    }
    if (poles.empty() && cargo.empty())
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
    draw(cargo, {0.20F, 0.85F, 0.95F});
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
        if (entity.kind == EntityKind::decoration)
            continue;
        if (entity.resource && entity.resource.remaining <= 0.0F)
            continue;
        glm::vec3 pickPosition = entity.transform.position;
        if (player && entity.authority.owner != player->id) {
            const glm::ivec2 cell = activeMapArea().gridCell(
                {entity.transform.position.x, entity.transform.position.z},
                Player::explorationCells);
            const std::size_t index =
                static_cast<std::size_t>(cell.y * Player::explorationCells + cell.x);
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
        const glm::ivec2 cell = activeMapArea().gridCell(
            {shown.position.x, shown.position.z}, Player::explorationCells);
        if (!player->visible[static_cast<std::size_t>(
                cell.y * Player::explorationCells + cell.x)]) {
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
        groundedEntityTransform(terrain_, shown, definition, id, model->baseY(), 1.035F);
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
    const float half = activeWorldExtent() * 0.5F - 0.5F;
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
