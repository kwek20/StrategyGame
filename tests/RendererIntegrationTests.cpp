#include "diagnostics/Logger.hpp"
#include "render/Renderer.hpp"
#include "render/ShaderManager.hpp"
#include "terrain/Terrain.hpp"
#include "world/Vegetation.hpp"
#include "world/World.hpp"
#include "world/WorldGeneration.hpp"

#include <SDL3/SDL.h>
#include <chrono>
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
        SDL_CreateWindow("renderer-test", 640, 360, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
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

    bool valid = noGlErrors("context creation");
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
        vegetation.chunks().push_back(std::move(vegetationChunk));

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
                                   {"settings.resolution.left", "settings.apply"},
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
        renderer.removeUiScreen(settingsScreen);
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
                           "<div class='resource'><img src='../icons/source/resource_scrap.png'/><span>125</span></div>");
        renderer.setUiText("entity-content",
                           "<div class='entity-info'><div class='entity-title'>CONSTRUCTION DRONE</div>"
                           "<div class='hud-bar health'><div class='hud-fill' style='width:72%;'></div>"
                           "<span>HEALTH 72 / 100</span></div></div>"
                           "<div class='entity-actions'><button id='action.preview' class='action'>"
                           "<img src='../icons/source/building_alloy_processor.png'/></button></div>");
        renderer.setUiControls({"resources.power", "action.preview"},
                               {"resources.power", "action.preview"});
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("RmlUi gameplay HUD") && valid;
        const auto unscaledActionBounds = renderer.uiBounds("action.preview");
        renderer.setUiScale(1.5F);
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        const auto scaledActionBounds = renderer.uiBounds("action.preview");
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
