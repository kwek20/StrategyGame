#include "diagnostics/Logger.hpp"
#include "render/Renderer.hpp"
#include "render/ShaderManager.hpp"
#include "ui/UiDocument.hpp"
#include "world/World.hpp"

#include <SDL3/SDL.h>
#include <chrono>
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

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cout << "Renderer test skipped: " << SDL_GetError() << '\n';
        return 0;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow(
        "renderer-test", 640, 360, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
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
        write(fragmentPath,
              "#version 450 core\nout vec4 color;void main(){color=vec4(0,1,0,1);}");
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
        renderer.regenerateTerrain(12345);

        strategy::CameraView camera;
        camera.position = {24, 30, 35};
        camera.target = {0, 0, 0};
        camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 1, 0});
        camera.projection = glm::perspective(glm::radians(55.0F), 640.0F / 360.0F, 0.1F, 500.0F);

        strategy::World world;
        strategy::Entity& town = world.createEntity("Town Center", "town_center", 1);
        town.kind = strategy::EntityKind::building;
        strategy::Entity& worker = world.createEntity("Worker", "worker", 1);
        worker.kind = strategy::EntityKind::unit;
        worker.transform.position = {4, 0, 2};
        worker.unitControl.emplace();
        const strategy::EntityId workerId = worker.id;
        strategy::Entity& missing = world.createEntity("Missing", "missing_test_asset", 1);
        missing.transform.position = {-4, 0, 2};

        strategy::UiDocument ui;
        ui.panel("panel", {12, 12, 240, 90}, {0.04F, 0.07F, 0.10F});
        ui.label("label", {24, 24, 220, 60}, "Renderer integration", 1.5F);

        renderer.preloadAssetGroup("match");
        strategy::AssetLoadProgress loadProgress = renderer.assetProgress("match");
        for (int frame = 0; frame < 400 && !loadProgress.finished(); ++frame) {
            renderer.beginProfileFrame();
            renderer.beginFrame(640, 360);
            renderer.drawTerrain(camera);
            renderer.drawWorld(world, camera);
            renderer.drawUi(ui);
            renderer.endFrame();
            glFinish();
            valid = noGlErrors("frame") && valid;
            loadProgress = renderer.assetProgress("match");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const strategy::TextureHandle texture = renderer.requestTexture("ui/placeholder");
        renderer.bindTexture(texture, 0);
        valid = loadProgress.total == 10 && loadProgress.completed == 10 &&
                loadProgress.failed == 0 && renderer.loadedModelCount() == 5 && valid;
        valid = renderer.textureState(texture) == strategy::ResourceState::ready &&
                noGlErrors("standalone texture") && valid;
        renderer.beginProfileFrame();
        renderer.beginFrame(640, 360);
        renderer.drawTerrain(camera);
        renderer.drawWorld(world, camera);
        renderer.drawEntityOutline(world, workerId, camera);
        renderer.drawUi(ui);
        renderer.endFrame();
        glFinish();
        valid = noGlErrors("complete render") && valid;
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return valid ? 0 : 1;
}
