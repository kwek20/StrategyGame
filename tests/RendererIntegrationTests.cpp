#include "diagnostics/Logger.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"
#include "render/ShaderManager.hpp"
#include "simulation/GameSession.hpp"
#include "terrain/Terrain.hpp"
#include "world/Vegetation.hpp"
#include "world/World.hpp"
#include "world/WorldGeneration.hpp"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
bool noGlErrors(const char* stage) {
    bool valid = true;
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        std::cerr << stage << " generated OpenGL error " << error << '\n';
        valid = false;
    }
    return valid;
}

bool checkFormPanel(strategy::Renderer& renderer, const char* selectorId, const char* actionId) {
    renderer.focusUi(0);
    auto* context = Rml::GetContext("strategy_ui");
    auto* focused = context ? context->GetFocusElement() : nullptr;
    if (!focused || focused->GetId() != selectorId) {
        std::cerr << "Form selector did not receive keyboard focus: " << selectorId << '\n';
        return false;
    }
    for (const char* id : {"shell", "content", selectorId, actionId}) {
        const auto bounds = renderer.uiBounds(id);
        if (!bounds || bounds->right <= bounds->left || bounds->bottom <= bounds->top) {
            std::cerr << "Templated form element has no layout: " << id << '\n';
            return false;
        }
    }
    auto* button = focused->GetElementById(std::string(selectorId) + ".left");
    if (!button)
        return false;
    // Compact artwork must not mask the shared interaction tints.
    for (const auto& [state, color] : {
             std::pair{"hover", Rml::Colourb(223, 255, 255)},
             std::pair{"active", Rml::Colourb(174, 191, 196)},
             std::pair{"disabled", Rml::Colourb(104, 114, 119)}}) {
        button->SetPseudoClass(state, true);
        context->Update();
        const bool matches = button->GetProperty<Rml::Colourb>("image-color") == color;
        button->SetPseudoClass(state, false);
        if (!matches) {
            std::cerr << "Selector tint overridden for state: " << state << '\n';
            return false;
        }
    }
    return true;
}

bool captureTerrain(const char* name) {
    const char* directory = SDL_getenv("STRATEGY_TERRAIN_SCREENSHOTS");
    if (!directory)
        return true;
    std::filesystem::create_directories(directory);
    std::vector<unsigned char> pixels(1280 * 720 * 4), flipped(pixels.size());
    glFinish();
    glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int row = 0; row < 720; ++row)
        std::copy_n(
            pixels.data() + row * 1280 * 4, 1280 * 4, flipped.data() + (719 - row) * 1280 * 4);
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(1280, 720, SDL_PIXELFORMAT_RGBA32, flipped.data(), 1280 * 4);
    const bool saved =
        surface && SDL_SaveBMP(surface, (std::filesystem::path(directory) / name).string().c_str());
    SDL_DestroySurface(surface);
    return saved;
}

bool checkVegetationCacheBuffers() {
    strategy::ResourceManager resources{"assets"};
    strategy::VegetationInstance instance;
    instance.presentation = strategy::PresentationId{"grass_fresh"};
    instance.transform.position = {2, 0, 3};
    strategy::VegetationChunk first, second;
    first.center = {2, 3};
    first.radius = 1;
    first.instances.push_back(instance);
    second.center = {40, 3};
    second.radius = 1;
    instance.transform.position.x = 40;
    second.instances.push_back(instance);
    strategy::VegetationField field{{first, second}};
    strategy::VegetationRenderCache cache;
    const strategy::VegetationRenderCache::PrepareTransform prepare =
        [](const auto& item, const auto*, float baseY) {
            return glm::translate(glm::mat4{1}, item.transform.position + glm::vec3{0, baseY, 0});
        };
    cache.beginFrame(field);
    const auto& markerBatch =
        cache.prepareChunk(0, field.chunks()[0], resources, prepare).at("grass_fresh");
    const auto handle = markerBatch.handle;
    const auto* markerModel = markerBatch.model;
    for (int attempt = 0;
         attempt < 400 && resources.state(handle) != strategy::ResourceState::ready;
         ++attempt) {
        resources.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (resources.state(handle) != strategy::ResourceState::ready)
        return false;
    cache.beginFrame(field);
    const auto& loadedBatch =
        cache.prepareChunk(0, field.chunks()[0], resources, prepare).at("grass_fresh");
    bool valid = loadedBatch.model != markerModel && cache.stats().rebuiltChunks == 1;
    glm::mat4 matrix;
    glBindBuffer(GL_ARRAY_BUFFER, loadedBatch.buffer);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(matrix), &matrix);
    valid &= matrix[3].x == 2 && matrix[3].z == 3 && matrix[3].y == loadedBatch.model->baseY();
    const auto firstBuffer = loadedBatch.buffer;
    const auto secondBuffer =
        cache.prepareChunk(1, field.chunks()[1], resources, prepare).at("grass_fresh").buffer;
    cache.beginFrame(field);
    cache.prepareChunk(0, field.chunks()[0], resources, prepare);
    cache.prepareChunk(1, field.chunks()[1], resources, prepare);
    valid &= cache.stats().rebuiltChunks == 0 && cache.stats().uploadedBatches == 0 &&
             glIsBuffer(firstBuffer) && glIsBuffer(secondBuffer);
    cache.invalidateTerrainRegion({0, 0}, {4, 5});
    cache.beginFrame(field);
    cache.prepareChunk(0, field.chunks()[0], resources, prepare);
    const auto& untouched = cache.prepareChunk(1, field.chunks()[1], resources, prepare);
    valid &= cache.stats().rebuiltChunks == 1 && untouched.at("grass_fresh").buffer == secondBuffer;
    field.removeInstance(field.chunks()[0].instances[0].id);
    cache.beginFrame(field);
    valid &=
        cache.prepareChunk(0, field.chunks()[0], resources, prepare).at("grass_fresh").count == 0;
    cache.prepareChunk(1, field.chunks()[1], resources, prepare);
    valid &= cache.stats().rebuiltChunks == 1 && cache.stats().uploadedBatches == 0;
    const auto& survivor =
        cache.prepareChunk(1, field.chunks()[1], resources, prepare).at("grass_fresh");
    glBindBuffer(GL_ARRAY_BUFFER, survivor.buffer);
    glGetBufferSubData(GL_ARRAY_BUFFER, survivor.first * sizeof(matrix), sizeof(matrix), &matrix);
    valid &= matrix[3].x == 40 && matrix[3].z == 3;
    cache.clear();
    valid &= !glIsBuffer(secondBuffer);
    if (!valid)
        std::cerr << "Vegetation cache buffer/readiness/invalidation test failed\n";
    return noGlErrors("vegetation cache buffers") && valid;
}

bool checkMapBoundary(strategy::Renderer& renderer, const strategy::Terrain& original) {
    const strategy::Terrain terrain{
        7, strategy::TerrainLayoutId{"central_water"}, strategy::TerrainMode::v2, 10};
    renderer.stageGeneratedTerrain(terrain, 7, 10);
    strategy::CameraView camera;
    camera.projection = glm::ortho(-0.5F, 0.5F, -0.5F, 0.5F, 0.1F, 200.0F);
    const auto pixel = [&](glm::vec2 position, const strategy::Player* player) {
        camera.position = {position.x, 100, position.y};
        camera.target = {position.x, 0, position.y};
        camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 0, -1});
        renderer.beginProfileFrame();
        renderer.beginFrame(640, 360);
        renderer.drawTerrain(camera, player);
        renderer.endFrame();
        std::array<unsigned char, 4> result{};
        glReadPixels(320, 180, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        return result;
    };
    const strategy::Player hidden;
    bool valid = true;
    // Beyond each playable edge, land must continue, yet respect exploration visibility.
    const float outsideEdge = strategy::MapArea{10}.extent() * 0.5F + 2.0F;
    for (glm::vec2 position : {glm::vec2{-outsideEdge, 0},
                               glm::vec2{outsideEdge, 0},
                               glm::vec2{0, -outsideEdge},
                               glm::vec2{0, outsideEdge}}) {
        const auto revealed = pixel(position, nullptr);
        const auto unexplored = pixel(position, &hidden);
        const bool hasLand = std::abs(static_cast<int>(revealed[0]) - 82) > 12 ||
                             std::abs(static_cast<int>(revealed[1]) - 92) > 12 ||
                             std::abs(static_cast<int>(revealed[2]) - 97) > 12;
        valid &= hasLand && unexplored[0] < 8 && unexplored[1] < 8 && unexplored[2] < 8;
    }
    // A mode switch must discard the V2 backdrop instead of retaining stale outside land.
    renderer.stageGeneratedTerrain(original, 12345, 10);
    const auto outside = pixel({outsideEdge, 0}, nullptr);
    valid &= outside[2] > 200 && outside[0] < 120;
    if (!valid)
        std::cerr << "Map backdrop continuity, fog or mode-switch regression\n";
    return noGlErrors("map boundary") && valid;
}

bool captureForm(strategy::Renderer& renderer, const char* name) {
    const char* directory = SDL_getenv("STRATEGY_FORM_SCREENSHOTS");
    if (!directory)
        return true;
    renderer.setUiProperty("shell", "animation", "none");
    renderer.beginFrame(1280, 720);
    renderer.drawRmlUi();
    renderer.endFrame();
    glFinish();
    std::filesystem::create_directories(directory);
    std::vector<unsigned char> pixels(1280 * 720 * 4), flipped(pixels.size());
    glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int row = 0; row < 720; ++row)
        std::copy_n(pixels.data() + row * 1280 * 4, 1280 * 4,
                    flipped.data() + (719 - row) * 1280 * 4);
    SDL_Surface* surface = SDL_CreateSurfaceFrom(1280, 720, SDL_PIXELFORMAT_RGBA32, flipped.data(), 1280 * 4);
    const bool saved = surface && SDL_SaveBMP(surface, (std::filesystem::path(directory) / name).string().c_str());
    SDL_DestroySurface(surface);
    return saved;
}

bool checkProgress(strategy::Renderer& renderer, const char* id, Rml::Colourb tint) {
    auto* context = Rml::GetContext("strategy_ui");
    Rml::Element* progress = nullptr;
    for (int i = 0; context && i < context->GetNumDocuments(); ++i)
        if (auto* element = context->GetDocument(i)->GetElementById(id))
            progress = element;
    if (!progress || progress->GetNumChildren(true) != 1)
        return false;
    const auto originalValue = progress->GetAttribute<Rml::String>("value", "0");
    const auto originalMax = progress->GetAttribute<Rml::String>("max", "1");
    const float trackWidth = progress->GetParentNode()->GetBox().GetSize().x;
    progress->SetAttribute("max", 100);
    bool valid = progress->GetProperty<Rml::Colourb>("image-color") == tint;
    for (const auto& [value, ratio] : {std::pair{0.0F, 0.0F}, {0.25F, 0.0025F},
             {25.0F, 0.25F}, {62.5F, 0.625F}, {100.0F, 1.0F}, {150.0F, 1.0F}, {-5.0F, 0.0F}}) {
        progress->SetAttribute("value", value);
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        const float width = progress->GetBox().GetSize().x;
        const float fillWidth = progress->GetChild(0)->GetBox().GetSize().x;
        valid = width > 0.0F && std::abs(fillWidth - width * ratio) < 0.1F &&
                std::abs(progress->GetParentNode()->GetBox().GetSize().x - trackWidth) < 0.1F && valid;
    }
    progress->SetAttribute("max", originalMax);
    progress->SetAttribute("value", originalValue);
    if (!valid)
        std::cerr << "Progress bar fill, tint, or fixed track failed: " << id << '\n';
    return valid;
}
} // namespace

int strategyTestMain() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cout << "Renderer test skipped: " << SDL_GetError() << '\n';
        return 0;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window =
        SDL_CreateWindow("renderer-test", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cout << "Renderer test skipped: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 0;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::cout << "Renderer test skipped: " << SDL_GetError() << '\n';
        if (context)
            SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    bool valid = noGlErrors("context creation") && checkVegetationCacheBuffers();
    {
        strategy::ShaderManager shaders;
        constexpr std::string_view vertex = R"(#version 450 core
void main(){gl_Position=vec4(0,0,0,1);})";
        constexpr std::string_view fragment = R"(#version 450 core
out vec4 color;void main(){color=vec4(1);})";
        const strategy::ShaderHandle first = shaders.load("integration-test", vertex, fragment);
        const strategy::ShaderHandle cached = shaders.load("integration-test", vertex, fragment);
        valid = first == cached && shaders.state(first) == strategy::ResourceState::ready && valid;
        const GLint firstLocation = shaders.uniform(first, "notPresent");
        const GLint cachedLocation = shaders.uniform(first, "notPresent");
        shaders.use(first);
        valid = firstLocation == -1 && cachedLocation == firstLocation &&
                noGlErrors("shader manager") && valid;

        const std::filesystem::path shaderDirectory =
            std::filesystem::temp_directory_path() / "strategy-shader-reload-test";
        std::filesystem::create_directories(shaderDirectory);
        const std::filesystem::path vertexPath = shaderDirectory / "test.vert";
        const std::filesystem::path fragmentPath = shaderDirectory / "test.frag";
        const auto write = [](const std::filesystem::path& path, std::string_view source) {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream << source;
        };
        write(vertexPath, vertex);
        write(fragmentPath, fragment);
        const strategy::ShaderHandle fileShader =
            shaders.loadFiles("reload-test", vertexPath, fragmentPath);
        const std::uint32_t originalProgram = shaders.program(fileShader);
        write(fragmentPath, "#version 450 core\nout vec4 color;void main(){color=vec4(0,1,0,1);}");
        std::filesystem::last_write_time(
            fragmentPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        const auto successfulReload = shaders.reloadChanged();
        valid = successfulReload.size() == 1 && successfulReload.front().succeeded &&
                shaders.program(fileShader) != originalProgram && valid;
        const std::uint32_t validReplacement = shaders.program(fileShader);
        write(fragmentPath, "this is not valid GLSL");
        std::filesystem::last_write_time(
            fragmentPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2));
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        const auto failedReload = shaders.reloadChanged();
        valid = failedReload.size() == 1 && !failedReload.front().succeeded &&
                shaders.program(fileShader) == validReplacement && valid;
        std::filesystem::remove(vertexPath);
        std::filesystem::remove(fragmentPath);
        std::filesystem::remove(shaderDirectory);

        strategy::Logger logger{std::filesystem::temp_directory_path() /
                                "strategy-renderer-test.log"};
        strategy::Renderer renderer{&logger};
        const strategy::Terrain generatedTerrain{12345};
        valid = checkMapBoundary(renderer, generatedTerrain) && valid;
        renderer.stageGeneratedTerrain(generatedTerrain, 12345, 10);
        valid =
            !renderer.terrainUploadFinished() && renderer.terrainUploadProgress() == 0.0F && valid;

        strategy::CameraView camera;
        camera.position = {24, 30, 35};
        camera.target = {0, 0, 0};
        camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 1, 0});
        camera.projection = glm::perspective(glm::radians(55.0F), 640.0F / 360.0F, 0.1F, 500.0F);

        strategy::World world;
        strategy::Entity& town = world.createEntity("Town Center", "town_center", 1);
        town.kind = strategy::EntityKind::building;
        town.power.emplace();
        town.power.generation = 10.0F;
        town.power.gridId = town.id;
        town.power.state = strategy::PowerOperationalState::powered;
        const strategy::EntityId townId = town.id;
        strategy::Entity& worker = world.createEntity("Worker", "worker", 1);
        worker.kind = strategy::EntityKind::unit;
        worker.transform.position = {4, 0, 2};
        worker.unitControl.emplace();
        worker.power.emplace();
        worker.power.demand = 2.0F;
        worker.power.supplied = 2.0F;
        worker.power.gridId = townId;
        worker.power.state = strategy::PowerOperationalState::powered;
        worker.power.connections = {townId};
        worker.transient.powerParent = townId;
        worker.transient.powerRoot = townId;
        worker.transient.powerReceivedLastTick = 2.0F;
        worker.transient.powerIncomingLastTick = {{townId, 2.0F}};
        const strategy::EntityId workerId = worker.id;
        strategy::Entity* stableTown = world.findEntity(townId);
        stableTown->power.connections = {workerId};
        stableTown->transient.powerRoot = townId;
        stableTown->transient.powerSentLastTick = 2.0F;
        strategy::Entity& missing = world.createEntity("Missing", "missing_test_asset", 1);
        missing.transform.position = {-4, 0, 2};
        strategy::VegetationField vegetation;
        strategy::VegetationChunk vegetationChunk;
        vegetationChunk.center = {0.0F, 0.0F};
        vegetationChunk.radius = 16.0F;
        strategy::VegetationInstance vegetationInstance;
        vegetationInstance.archetype = strategy::EntityArchetypeId{"grass_fresh"};
        vegetationInstance.presentation = strategy::PresentationId{"grass_fresh"};
        vegetationInstance.transform.position = {2.0F, 0.0F, -2.0F};
        vegetationChunk.instances.push_back(vegetationInstance);
        vegetationInstance.transform.position.x = 8;
        vegetationChunk.instances.push_back(vegetationInstance);
        strategy::VegetationChunk otherChunk;
        otherChunk.center = {40, 0};
        otherChunk.radius = 16;
        vegetationInstance.transform.position.x = 40;
        otherChunk.instances.push_back(vegetationInstance);
        vegetation = strategy::VegetationField{{std::move(vegetationChunk), std::move(otherChunk)}};

        renderer.preloadAssetGroup("match");
        strategy::AssetLoadProgress loadProgress = renderer.assetProgress("match");
        for (int frame = 0;
             frame < 400 && (!loadProgress.finished() || !renderer.terrainUploadFinished());
             ++frame) {
            renderer.beginProfileFrame();
            renderer.beginFrame(640, 360);
            renderer.drawTerrain(camera);
            renderer.drawVegetation(vegetation, camera);
            renderer.drawWorld(world, camera);
            renderer.endFrame();
            glFinish();
            valid = noGlErrors("frame") && valid;
            loadProgress = renderer.assetProgress("match");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto vegetationFrame = [&] {
            renderer.beginProfileFrame();
            renderer.beginFrame(1280, 720);
            renderer.drawTerrain(camera);
            renderer.drawVegetation(vegetation, camera);
            renderer.endFrame();
            glFinish();
            return renderer.vegetationCacheStats();
        };
        vegetationFrame(); // Settle the last async asset completion before cache assertions.
        auto cacheStats = vegetationFrame();
        valid = cacheStats.rebuiltChunks == 0 && cacheStats.uploadedBatches == 0 &&
                cacheStats.submittedInstances == 3 && valid;
        valid = vegetation.removeInstance(vegetation.chunks()[0].instances[0].id) && valid;
        cacheStats = vegetationFrame();
        valid = cacheStats.rebuiltChunks == 1 && cacheStats.uploadedBatches == 1 &&
                cacheStats.submittedInstances == 2 && valid;
        cacheStats = vegetationFrame();
        valid = cacheStats.rebuiltChunks == 0 && cacheStats.uploadedBatches == 0 && valid;
        valid =
            vegetation.clearWithin({strategy::FootprintShape::circle, {8, -2}, 1}) == 1 && valid;
        cacheStats = vegetationFrame();
        valid = cacheStats.rebuiltChunks == 1 && cacheStats.uploadedBatches == 0 &&
                cacheStats.submittedInstances == 1 && valid;
        vegetation = strategy::VegetationField{vegetation};
        cacheStats = vegetationFrame();
        valid = cacheStats.rebuiltChunks == 2 && cacheStats.submittedInstances == 1 && valid;
        renderer.stageGeneratedTerrain(generatedTerrain, 12345, 10);
        cacheStats = vegetationFrame();
        valid = cacheStats.rebuiltChunks == 2 && cacheStats.submittedInstances == 1 &&
                noGlErrors("vegetation cache destruction and replacement") && valid;
        while (!renderer.terrainUploadFinished())
            vegetationFrame();
        if (SDL_getenv("STRATEGY_TERRAIN_SCREENSHOTS")) {
            for (const char* layout :
                 {"open_plains", "rolling_hills", "central_hill", "central_water", "continental"}) {
                strategy::Terrain terrainV2{
                    7, strategy::TerrainLayoutId{layout}, strategy::TerrainMode::v2, 10};
                renderer.stageGeneratedTerrain(terrainV2, 7, 10);
                auto terrainCamera = camera;
                terrainCamera.position = {150, 205, 185};
                terrainCamera.target = {0, 12, 0};
                terrainCamera.view =
                    glm::lookAt(terrainCamera.position, terrainCamera.target, glm::vec3{0, 1, 0});
                terrainCamera.projection =
                    glm::perspective(glm::radians(55.0F), 1280.0F / 720.0F, 0.1F, 1000.0F);
                for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
                    renderer.beginProfileFrame();
                    renderer.beginFrame(1280, 720);
                    renderer.drawTerrain(terrainCamera);
                    renderer.endFrame();
                }
                renderer.beginProfileFrame();
                renderer.beginFrame(1280, 720);
                renderer.drawTerrain(terrainCamera);
                renderer.endFrame();
                valid = captureTerrain((std::string{layout} + ".bmp").c_str()) &&
                        noGlErrors("V2 terrain") && valid;
            }
            const strategy::DefinitionRegistry gameplay;
            strategy::GameSession v2Session{gameplay,
                                            7,
                                            "spain",
                                            "japan",
                                            "unassigned",
                                            "unassigned",
                                            10,
                                            1,
                                            1,
                                            strategy::TerrainLayoutId{"central_water"},
                                            nullptr,
                                            strategy::TerrainMode::v2};
            renderer.stageGeneratedTerrain(
                v2Session.terrain(), 7, 10, v2Session.world().foundations());
            auto reviewCamera = camera;
            reviewCamera.projection =
                glm::perspective(glm::radians(55.0F), 1280.0F / 720.0F, 0.1F, 600.0F);
            for (int view = 0; view < 6; ++view) {
                const glm::vec3 focus = view == 0 ? glm::vec3{0, 12, 0}
                                                  : glm::vec3{view == 1   ? -85.0F
                                                              : view == 2 ? 85.0F
                                                                          : 0.0F,
                                                              12,
                                                              view == 3   ? -85.0F
                                                              : view == 4 ? 85.0F
                                                                          : 0.0F};
                reviewCamera.target = focus;
                reviewCamera.position =
                    focus + (view == 5 ? glm::vec3{150, 205, 185} : glm::vec3{0, 80, 75});
                reviewCamera.view =
                    glm::lookAt(reviewCamera.position, reviewCamera.target, glm::vec3{0, 1, 0});
                for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
                    renderer.beginProfileFrame();
                    renderer.beginFrame(1280, 720);
                    renderer.drawTerrain(reviewCamera);
                    renderer.endFrame();
                }
                renderer.beginProfileFrame();
                renderer.beginFrame(1280, 720);
                renderer.drawTerrain(reviewCamera);
                renderer.drawVegetation(v2Session.vegetation(), reviewCamera);
                renderer.drawWorld(v2Session.world(), reviewCamera);
                renderer.endFrame();
                valid = captureTerrain(
                            ("central-water-play-" + std::to_string(view) + ".bmp").c_str()) &&
                        noGlErrors("V2 movement review") && valid;
            }
            // Exercise continuous motion across shores, cliffs and active-map boundaries.
            for (int frame = 0; frame < 80; ++frame) {
                const float angle = glm::two_pi<float>() * frame / 80;
                reviewCamera.target = {85 * std::cos(angle), 12, 85 * std::sin(angle)};
                reviewCamera.position =
                    reviewCamera.target + glm::vec3{0, 65 + 25 * std::sin(angle), 75};
                reviewCamera.view =
                    glm::lookAt(reviewCamera.position, reviewCamera.target, glm::vec3{0, 1, 0});
                renderer.beginProfileFrame();
                renderer.beginFrame(1280, 720);
                renderer.drawTerrain(reviewCamera);
                renderer.drawVegetation(v2Session.vegetation(), reviewCamera);
                renderer.drawWorld(v2Session.world(), reviewCamera);
                renderer.endFrame();
            }
            valid = noGlErrors("V2 continuous camera motion") && valid;
            renderer.stageGeneratedTerrain(generatedTerrain, 12345, 10);
            for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
                renderer.beginProfileFrame();
                renderer.beginFrame(640, 360);
                renderer.drawTerrain(camera);
                renderer.endFrame();
            }
        }
        const strategy::TextureHandle texture = renderer.requestTexture("ui/placeholder");
        renderer.bindTexture(texture, 0);
        // Asset groups can grow without requiring this renderer behavior test to duplicate the
        // manifest's exact inventory. Verify that every declared item completed and that all
        // currently required presentation models were uploaded.
        valid = loadProgress.total >= 44 && loadProgress.completed == loadProgress.total &&
                loadProgress.failed == 0 && renderer.loadedModelCount() >= 29 &&
                renderer.terrainUploadFinished() && renderer.terrainUploadProgress() == 1.0F &&
                valid;
        valid = renderer.textureState(texture) == strategy::ResourceState::ready &&
                noGlErrors("standalone texture") && valid;
        renderer.beginProfileFrame();
        renderer.beginFrame(640, 360);
        renderer.drawTerrain(camera, nullptr, true);
        renderer.drawVegetation(vegetation, camera);
        renderer.drawWorld(world, camera);
        renderer.drawEntityOutline(world, workerId, camera);
        renderer.drawPowerRanges(world, camera, 1);
        strategy::Entity placementPreview;
        placementPreview.transform.position = {8.0F, 0.0F, 0.0F};
        placementPreview.power.emplace();
        placementPreview.power.demand = 4.0F;
        placementPreview.power.connectionRange = 24.0F;
        placementPreview.power.maximumConnections = 2;
        renderer.drawPowerPlacementConnection(world, camera, 1, placementPreview);
        placementPreview.transform.position = {1000.0F, 0.0F, 1000.0F};
        renderer.drawPowerPlacementConnection(world, camera, 1, placementPreview);
        renderer.drawPowerConnections(world, camera, 1, true);
        renderer.drawTerrainDebugHud({0.0F, 0.0F, 0.0F}, {});
        renderer.drawPowerDebugHud(world, 1, {0.0F, 0.0F, 0.0F});
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("complete render") && valid;
        renderer.beginFrame(1280, 720);
        renderer.drawLoadingScreen(0.42F, "GENERATING TERRAIN FIELDS");
        valid = checkProgress(renderer, "loading-fill", Rml::Colourb(84, 215, 242)) && valid;
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("RmlUi loading screen") && valid;
        renderer.beginFrame(1280, 720);
        renderer.drawLoadingScreen(1.0F, "CLICK TO START");
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("RmlUi completed loading screen") && valid;
        if (const char* loadingScreenshotPath = SDL_getenv("STRATEGY_LOADING_SCREENSHOT")) {
            std::vector<unsigned char> pixels(1280 * 720 * 4), flipped(pixels.size());
            glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (int row = 0; row < 720; ++row)
                std::copy_n(pixels.data() + row * 1280 * 4,
                            1280 * 4,
                            flipped.data() + (719 - row) * 1280 * 4);
            SDL_Surface* surface = SDL_CreateSurfaceFrom(
                1280, 720, SDL_PIXELFORMAT_RGBA32, flipped.data(), 1280 * 4);
            valid = surface && SDL_SaveBMP(surface, loadingScreenshotPath) && valid;
            SDL_DestroySurface(surface);
        }
        renderer.finishLoadingScreen();
        const strategy::RmlUiScreenHandle pauseScreen =
            renderer.pushUiScreen({"assets/ui/pause_menu.rml",
                                   {},
                                   {"pause.resume", "pause.settings", "pause.exit"},
                                   {"pause.resume", "pause.settings", "pause.exit"},
                                   1.0F});
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("RmlUi pause menu") && valid;
        renderer.removeUiScreen(pauseScreen);
        const strategy::RmlUiScreenHandle settingsScreen =
            renderer.pushUiScreen({"assets/ui/settings.rml",
                                   {},
                                   {"settings.resolution", "settings.apply"},
                                   {"settings.resolution.left", "settings.apply"},
                                   1.0F});
        renderer.setUiText("resolution-label", "RESOLUTION");
        renderer.setUiText("settings.resolution.value", "1280 x 720");
        renderer.setUiText("settings-controls", "CONTROLS");
        renderer.setUiText("bind.forward.label", "MOVE FORWARD");
        renderer.setUiText("bind.forward.value", "W");
        renderer.setUiText("apply-label", "APPLY AND BACK");
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("RmlUi settings menu") && valid;
        valid = checkFormPanel(renderer, "settings.resolution", "settings.apply") && valid;
        valid = captureForm(renderer, "settings.bmp") && valid;
        renderer.removeUiScreen(settingsScreen);
        const auto matchScreen = renderer.pushUiScreen(
            {"assets/ui/match_setup.rml",
             {{"match-title", "MATCH SETUP"},
              {"player-label", "PLAYER"},
              {"match.player_country.value", "BRAZIL"},
              {"start-label", "START MATCH"},
              {"generation-label", "TERRAIN GENERATION"},
              {"match.terrain_generation.value", "V2 - REGIONAL TERRAIN"},
              {"generation-description",
               "Stepped natural terrain, broad ramps, supported foundations and layout-controlled "
               "water. Archipelago requires future transport support."}},
             {"match.player_country", "match.start"},
             {"match.player_country.left", "match.player_country.right", "match.start"},
             1.0F});
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("RmlUi match setup") && valid;
        valid = checkFormPanel(renderer, "match.player_country", "match.start") && valid;
        for (const auto& [id, value] : std::vector<std::pair<std::string, std::string>>{
                 {"match-mode", "1 VS 1 SKIRMISH"},
                 {"players-header", "PLAYERS"},
                 {"world-header", "WORLD"},
                 {"economy-header", "ECONOMY"},
                 {"options-header", "MATCH OPTIONS"},
                 {"opponent-label", "OPPONENT COUNTRY"},
                 {"layout-label", "MAP LAYOUT"},
                 {"size-label", "MAP SIZE"},
                 {"resources-label", "STARTING RESOURCES"},
                 {"abundance-label", "RESOURCE ABUNDANCE"},
                 {"seed-label", "MAP SEED"},
                 {"random-label", "RANDOM SEED"},
                 {"match.opponent_country.value", "JAPAN"},
                 {"match.terrain_layout.value", "ROLLING HILLS"},
                 {"match.map_size.value", "MEDIUM"},
                 {"match.starting_resources.value", "NORMAL"},
                 {"match.abundance.value", "NORMAL"},
                 {"match.seed.value", "123"},
                 {"summary-header", "MATCH SUMMARY"},
                 {"summary-player", "YOU: BRAZIL"},
                 {"summary-opponent", "OPPONENT: JAPAN"},
                 {"summary-map", "MAP: MEDIUM"},
                 {"summary-resources", "START: NORMAL"},
                 {"summary-deposits", "DEPOSITS: NORMAL"},
                 {"preview-caption", "TACTICAL OVERVIEW"},
                 {"back-label", "BACK"}})
            renderer.setUiText(id, value);
        renderer.setUiAttribute("player-emblem", "src", "../textures/ui/country_emblem_brazil.png");
        renderer.setUiAttribute(
            "opponent-emblem", "src", "../textures/ui/country_emblem_japan.png");
        valid = captureForm(renderer, "match-setup.bmp") && valid;
        renderer.setUiScale(1.5F);
        renderer.setUiText("match-mode", strategy::Text::get("match_setup.generation_failed"));
        renderer.setUiProperty("match-mode", "color", "#ffbf75");
        valid = captureForm(renderer, "match-generation-failed.bmp") && valid;
        renderer.setUiScale(1.0F);
        renderer.removeUiScreen(matchScreen);
        const strategy::RmlUiScreenHandle buildScreen =
            renderer.pushUiScreen({"assets/ui/build_menu.rml",
                                   {},
                                   {"build.palette.command_hub"},
                                   {"build.palette.command_hub"},
                                   1.0F});
        renderer.setUiText("build-palette",
                           "<button id=\"build.palette.command_hub\" class=\"build-slot selected\">"
                           "<img src=\"../icons/source/building_command_hub.png\"/>"
                           "<span>COMMAND HUB</span></button>");
        renderer.focusUi(0);
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("RmlUi build menu") && valid;
        renderer.removeUiScreen(buildScreen);
        const strategy::RmlUiScreenHandle gameplayScreen =
            renderer.pushUiScreen({"assets/ui/gameplay_hud.rml",
                                   {},
                                   {"resources.power", "hud.satellite", "hud.menu"},
                                   {"resources.power", "hud.satellite", "hud.menu"},
                                   1.0F});
        renderer.setUiText("resources-content",
                           "<div class='resource'><img "
                           "src='../icons/source/resource_scrap.png'/><span>125</span></div>");
        renderer.setUiText(
            "entity-content",
            "<div id='test.info' class='entity-info'><div class='entity-title'>CONSTRUCTION "
            "DRONE</div>"
            "<div class='entity-main'><img class='portrait' "
            "src='../icons/source/building_alloy_processor.png'/><div class='bars-stats'>"
            "<div class='hud-bar progress-track'><progress id='test.health' class='progress-fill "
            "health' value='0.72' max='1'/>"
            "<span class='progress-label'>HEALTH 72 / 100</span></div>"
            "<div class='hud-bar progress-track'><progress id='test.power' class='progress-fill "
            "power' value='0.35' max='1'/>"
            "<span class='progress-label'>ENERGY 35 / 100</span></div>"
            "<div class='hud-bar progress-track'><progress id='test.construction' "
            "class='progress-fill construction' value='0.5' max='1'/>"
            "<span class='progress-label'>BUILD 50 / 100</span></div></div></div>"
            "<div class='stats-list'><div class='stat'><span class='stat-label'>CARGO</span>"
            "<span id='test.stat' class='stat-value'>100 / 100</span></div></div></div>"
            "<div class='entity-actions'><button id='action.preview' class='action'>"
            "<img src='../icons/source/building_alloy_processor.png'/></button></div>");
        auto* hudContext = Rml::GetContext("strategy_ui");
        auto* hudDocument = hudContext->GetDocument(hudContext->GetNumDocuments() - 1);
        auto* actionPanel = hudDocument->QuerySelector(".entity-actions");
        std::string palette = "<div class='action-heading'>CONSTRUCTION</div>";
        for (int i = 0; i < 11; ++i) {
            if (i % 4 == 0) palette += "<div class='action-row'>";
            palette += "<button id='" + std::string(i == 0 ? "action.preview" : "test.slot." + std::to_string(i)) +
                       "' class='action'><img src='../icons/source/building_alloy_processor.png'/>"
                       "<span class='action-name'>ALLOY PROCESSOR</span><span class='action-hotkey'>Alt+" +
                       std::string(1, "1234567890-="[i]) + "</span>"
                       "<div class='tooltip'><b>ALLOY PROCESSOR</b><br/>Converts scrap into alloy."
                       "<br/>COST: 150 SCRAP<br/>Requires power.</div></button>";
            if (i % 4 == 3 || i == 10) palette += "</div>";
        }
        actionPanel->SetInnerRML(palette);
        renderer.setUiControls({"resources.power", "action.preview"},
                               {"resources.power", "action.preview"});
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("RmlUi gameplay HUD") && valid;
        const auto infoBounds = renderer.uiBounds("test.info");
        const auto statBounds = renderer.uiBounds("test.stat");
        const auto lastSlot = renderer.uiBounds("test.slot.10");
        if (!infoBounds || !statBounds || !lastSlot || statBounds->right > infoBounds->right + 1 ||
            lastSlot->right >= infoBounds->left || lastSlot->bottom > 720) {
            std::cerr << "HUD panels overlap or clip their content\n";
            valid = false;
        }

        valid = checkProgress(renderer, "test.health", Rml::Colourb(239, 100, 92)) && valid;
        valid = checkProgress(renderer, "test.power", Rml::Colourb(84, 215, 242)) && valid;
        valid = checkProgress(renderer, "test.construction", Rml::Colourb(239, 191, 86)) && valid;
        valid = captureForm(renderer, "progress-bars.bmp") && valid;
        const auto unscaledActionBounds = renderer.uiBounds("action.preview");
        hudDocument->GetElementById("action.preview")->SetPseudoClass("hover", true);
        hudDocument->GetElementById("action.preview")->QuerySelector(".tooltip")->SetProperty("animation", "none");
        valid = captureForm(renderer, "hud-tooltip.bmp") && valid;
        hudDocument->GetElementById("action.preview")->SetPseudoClass("hover", false);
        renderer.setUiScale(1.5F);
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        const auto scaledActionBounds = renderer.uiBounds("action.preview");
        const std::string scrollMarkup =
            "<div id='test.scroll' style='height:60px; overflow:auto;'>"
            "<div style='height:500px;'>SCROLL RETENTION</div></div>";
        renderer.setUiText("entity-queue", scrollMarkup);
        hudContext->Update();
        auto* scrollPanel = hudDocument->GetElementById("test.scroll");
        scrollPanel->SetScrollTop(90);
        renderer.setUiText("entity-queue", scrollMarkup);
        hudContext->Update();
        if (hudDocument->GetElementById("test.scroll") != scrollPanel || scrollPanel->GetScrollTop() < 89) {
            std::cerr << "Identical HUD updates reset the scrolling subtree\n";
            valid = false;
        }
        // The cache must detect changes made through another UI API.
        renderer.resetUiScroll("test.scroll");
        if (scrollPanel->GetScrollTop() != 0) {
            std::cerr << "Explicit selection scroll reset failed\n";
            valid = false;
        }
        scrollPanel->SetInnerRML("ALTERED");
        renderer.setUiText("entity-queue", scrollMarkup);
        hudContext->Update();
        scrollPanel = hudDocument->GetElementById("test.scroll");
        if (scrollPanel->GetInnerRML().find("SCROLL RETENTION") == std::string::npos) {
            std::cerr << "Cached markup failed to restore externally changed content\n";
            valid = false;
        }
        const auto scrollBounds = renderer.uiBounds("test.scroll");
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.mouse_x = (scrollBounds->left + scrollBounds->right) * 0.5F;
        wheel.wheel.mouse_y = (scrollBounds->top + scrollBounds->bottom) * 0.5F;
        wheel.wheel.y = -3;
        renderer.handleUiEvent(wheel);
        for (int i = 0; i < 12; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            hudContext->Update();
        }
        if (!renderer.pointerOverUi() || scrollPanel->GetScrollTop() < 1) {
            std::cerr << "Wheel event did not target and scroll its UI panel\n";
            valid = false;
        }
        renderer.setUiText("entity-queue", "");
        valid = captureForm(renderer, "hud-large.bmp") && valid;
        renderer.setUiAttribute("entity-shell", "class", "info-only");
        hudContext->Update();
        const auto compactInfo = renderer.uiBounds("test.info");
        const auto compactShell = renderer.uiBounds("entity-shell");
        if (!compactInfo || !compactShell || compactInfo->right > compactShell->right ||
            compactInfo->left < compactShell->left ||
            actionPanel->GetProperty<Rml::Style::Display>("display") != Rml::Style::Display::None) {
            std::cerr << "Information-only HUD failed to compact its layout\n";
            valid = false;
        }
        valid = captureForm(renderer, "hud-info-only.bmp") && valid;
        renderer.setUiAttribute("entity-shell", "class", "");
        if (!unscaledActionBounds || !scaledActionBounds ||
            scaledActionBounds->right - scaledActionBounds->left <=
                (unscaledActionBounds->right - unscaledActionBounds->left) * 1.25F) {
            std::cerr << "RmlUi global scale did not resize the active HUD\n";
            valid = false;
        }
        renderer.setUiScale(1.0F);
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        if (const auto bounds = renderer.uiBounds("action.preview")) {
            SDL_Event down{};
            down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            down.button.button = SDL_BUTTON_LEFT;
            down.button.x = (bounds->left + bounds->right) * 0.5F;
            down.button.y = (bounds->top + bounds->bottom) * 0.5F;
            renderer.handleUiEvent(down);
            const auto clicked = renderer.takeUiAction();
            SDL_Event up = down;
            up.type = SDL_EVENT_MOUSE_BUTTON_UP;
            renderer.handleUiEvent(up);
            if (!clicked || *clicked != "action.preview") {
                std::cerr << "RmlUi gameplay action did not receive a pointer click at "
                          << down.button.x << ", " << down.button.y << "\n";
                valid = false;
            }
        } else {
            std::cerr << "RmlUi gameplay action has no bounds\n";
            valid = false;
        }
        renderer.removeUiScreen(gameplayScreen);
        if (const char* screenshotPath = SDL_getenv("STRATEGY_UI_SCREENSHOT")) {
            std::vector<unsigned char> pixels(1280 * 720 * 4), flipped(pixels.size());
            glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (int row = 0; row < 720; ++row)
                std::copy_n(pixels.data() + row * 1280 * 4,
                            1280 * 4,
                            flipped.data() + (719 - row) * 1280 * 4);
            SDL_Surface* surface =
                SDL_CreateSurfaceFrom(1280, 720, SDL_PIXELFORMAT_RGBA32, flipped.data(), 1280 * 4);
            valid = surface && SDL_SaveBMP(surface, screenshotPath) && valid;
            SDL_DestroySurface(surface);
        }
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return valid ? 0 : 1;
}
