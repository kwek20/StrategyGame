#include "app/Application.hpp"

#include "app/GameState.hpp"
#include "app/GameEvents.hpp"
#include "audio/AudioSystem.hpp"
#include "diagnostics/Logger.hpp"
#include "game/BuildState.hpp"
#include "game/PlayState.hpp"
#include "game/SettingsState.hpp"
#include "game/StartMenuState.hpp"
#include "game/MatchSetupState.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "persistence/GameConfig.hpp"
#include "persistence/SaveGame.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <chrono>
#include <glad/glad.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace strategy {
namespace {

[[noreturn]] void throwSdlError(const char* action) {
    throw std::runtime_error(std::string(action) + ": " + SDL_GetError());
}

std::string_view loadingGlyph(char value) {
    switch (value) {
    case 'L':
        return "10000100001000010000100001000011111";
    case 'O':
        return "01110100011000110001100011000101110";
    case 'A':
        return "01110100011000111111100011000110001";
    case 'D':
        return "11110100011000110001100011000111110";
    case 'I':
        return "11111001000010000100001000010011111";
    case 'N':
        return "10001110011010110011100011000110001";
    case 'G':
        return "01110100011000010111100011000101110";
    default:
        return {};
    }
}

void drawBootstrapLoading(SDL_Window* window) {
    int width = 1, height = 1;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    constexpr std::string_view word = "LOADING";
    constexpr int pixel = 7, advance = 7 * pixel;
    const int left = (width - static_cast<int>(word.size()) * advance) / 2,
              top = (height - 7 * pixel) / 2;
    for (std::size_t letter = 0; letter < word.size(); ++letter) {
        const auto glyph = loadingGlyph(word[letter]);
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (glyph[static_cast<std::size_t>(row * 5 + column)] == '1') {
                    glScissor(left + static_cast<int>(letter) * advance + column * pixel,
                              height - (top + (row + 1) * pixel),
                              pixel - 1,
                              pixel - 1);
                    glClearColor(0.9F, 0.94F, 0.96F, 1);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
    }
    glDisable(GL_SCISSOR_TEST);
    SDL_GL_SwapWindow(window);
}

} // namespace

Application::Application() {
    logger_ = std::make_unique<Logger>();
    logger_->info("lifecycle", "Application startup began");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        throwSdlError("Could not initialize SDL");
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    const GameConfig startupConfig = GameConfig::load("gamedata/config.json");
    audio_ = std::make_unique<AudioSystem>();
    audio_->load();
    audio_->setMasterVolume(startupConfig.masterVolume);
    audio_->setMusicVolume(startupConfig.musicVolume);
    audio_->setEffectsVolume(startupConfig.effectsVolume);
    audio_->setMuted(startupConfig.muted);
    events_.subscribe<AudioEvent>([this](const AudioEvent& event) { audio_->play(event.cue); });
    SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (startupConfig.fullscreen)
        windowFlags |= SDL_WINDOW_FULLSCREEN;
    window_ = SDL_CreateWindow(Text::get("menu.title").c_str(),
                               startupConfig.resolutionWidth,
                               startupConfig.resolutionHeight,
                               windowFlags);
    if (window_ == nullptr) {
        throwSdlError("Could not create window");
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (glContext_ == nullptr) {
        throwSdlError("Could not create OpenGL context");
    }

    const auto loader = reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress);
    if (gladLoadGLLoader(loader) == 0) {
        throw std::runtime_error("Could not load OpenGL functions");
    }
    const auto glText = [](GLenum value) {
        const auto* text = glGetString(value);
        return text ? reinterpret_cast<const char*>(text) : "unknown";
    };
    logger_->info("system",
                  std::string("OpenGL vendor=") + glText(GL_VENDOR) + " renderer=" +
                      glText(GL_RENDERER) + " version=" + glText(GL_VERSION));
    logger_->info("configuration",
                  "Resolution=" + std::to_string(startupConfig.resolutionWidth) + "x" +
                      std::to_string(startupConfig.resolutionHeight) +
                      " fullscreen=" + (startupConfig.fullscreen ? "true" : "false"));

    drawBootstrapLoading(window_);
    SDL_GL_SetSwapInterval(1);
    renderer_ = std::make_unique<Renderer>(logger_.get());
    definitions_ = std::make_unique<DefinitionRegistry>();
    stateContext_ =
        std::make_unique<StateContext>(
            StateContext{*audio_, events_, *logger_, *definitions_, "gamedata/config.json"});
    states_ = std::make_unique<StateStack>(*stateContext_);
    states_->push<StartMenuState>();
    states_->applyPendingChanges();
    audio_->setAmbient(AudioCue::menuAmbient);
    logger_->info("lifecycle", "Application startup completed");
}

Application::~Application() {
    states_.reset();
    stateContext_.reset();
    definitions_.reset();
    renderer_.reset();
    audio_.reset();
    logger_->info("lifecycle", "Application shutdown");
    logger_->flush();
    logger_.reset();
    if (glContext_ != nullptr) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

int Application::run() {
    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    auto fpsStart = previous;
    int frames = 0;
    const auto showLoading = [this](float progress, const std::string& status) {
        int width = 1, height = 1;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        renderer_->beginProfileFrame();
        renderer_->beginFrame(width, height);
        renderer_->drawLoadingScreen(progress, status);
        renderer_->endFrame();
        SDL_GL_SwapWindow(window_);
        SDL_PumpEvents();
    };
    const auto preloadAssets = [this, &showLoading](const std::string& group,
                                                    float progressStart,
                                                    float progressRange) {
        renderer_->preloadAssetGroup(group);
        AssetLoadProgress progress;
        do {
            progress = renderer_->assetProgress(group);
            std::string phase = Text::get("loading.assets.queued");
            if (progress.activeState == ResourceState::importing)
                phase = Text::get("loading.assets.importing");
            else if (progress.activeState == ResourceState::uploading)
                phase = Text::get("loading.assets.uploading");
            else if (progress.finished())
                phase = progress.failed == 0 ? Text::get("loading.assets.ready")
                                             : Text::get("loading.assets.failed");
            showLoading(progressStart + progressRange * progress.fraction(),
                        Text::format("loading.assets",
                                     {std::to_string(progress.completed),
                                      std::to_string(progress.total), phase}));
            if (!progress.finished())
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (!progress.finished());
        if (progress.failed > 0)
            logger_->warning("assets",
                             std::to_string(progress.failed) + " preload assets failed");
    };

    while (running_) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            }
            if (GameState* state = states_->current())
                state->handleEvent(event);
        }

        GameState* state = states_->current();
        if (state == nullptr)
            break;
        const StateRequest request = state->takeRequest();
        if (request == StateRequest::openMatchSetup) {
            states_->replace<MatchSetupState>();
        } else if (request == StateRequest::startGame) {
            logger_->info("state", "Starting a new game");
            MatchSetupOptions setup = state->matchSetup();
            const std::uint32_t seed = setup.terrainSeed;
            showLoading(0.08F, Text::get("loading.terrain"));
            renderer_->regenerateTerrain(seed, setup.mapChunksPerSide);
            showLoading(0.32F, Text::get("loading.world"));
            preloadAssets("match", 0.36F, 0.56F);
            states_->replace<PlayState>(std::move(setup));
            showLoading(0.94F, Text::get("loading.finalize"));
            stateContext_->audio.setAmbient(AudioCue::gameAmbient);
        } else if (request == StateRequest::buildMap) {
            logger_->info("state", "Opening map builder");
            const std::uint32_t seed = state->terrainSeed();
            showLoading(0.08F, Text::get("loading.terrain"));
            renderer_->regenerateTerrain(seed);
            showLoading(0.32F, Text::get("loading.world"));
            preloadAssets("build", 0.36F, 0.56F);
            states_->replace<BuildState>(seed);
            showLoading(0.94F, Text::get("loading.finalize"));
            stateContext_->audio.setAmbient(AudioCue::buildAmbient);
        } else if (request == StateRequest::loadGame) {
            logger_->info("state", "Loading saved game");
            try {
                showLoading(0.08F, Text::get("loading.save"));
                const GameConfig config = GameConfig::load(stateContext_->configPath);
                SaveData data = SaveGame::read(config.savePath());
                showLoading(0.30F, Text::get("loading.terrain"));
                renderer_->regenerateTerrain(data.terrainSeed, data.mapChunksPerSide);
                showLoading(0.36F, Text::get("loading.world"));
                preloadAssets("match", 0.40F, 0.52F);
                states_->replace<PlayState>(std::move(data));
                showLoading(0.94F, Text::get("loading.finalize"));
                stateContext_->audio.setAmbient(AudioCue::gameAmbient);
            } catch (const std::exception& error) {
                logger_->error("persistence",
                               std::string("Could not load from main menu: ") + error.what());
                states_->replace<StartMenuState>();
            }
        } else if (request == StateRequest::returnToMainMenu) {
            logger_->debug("state", "Returning to main menu");
            const bool poppingSettings =
                dynamic_cast<SettingsState*>(state) != nullptr && states_->size() > 1;
            if (poppingSettings)
                states_->pop();
            else
                states_->replace<StartMenuState>();
            if (!poppingSettings)
                stateContext_->audio.setAmbient(AudioCue::menuAmbient);
        } else if (request == StateRequest::openSettings) {
            logger_->debug("state", "Opening settings");
            states_->push<SettingsState>();
        } else if (request == StateRequest::exitApplication) {
            logger_->info("lifecycle", "Exit requested");
            running_ = false;
        }
        states_->applyPendingChanges();
        events_.dispatchQueued();
        if (!running_) {
            break;
        }

        const auto now = Clock::now();
        const float delta = std::chrono::duration<float>(now - previous).count();
        previous = now;

        renderer_->beginProfileFrame();
        const auto simulationStart = Clock::now();
        states_->current()->update(delta);
        renderer_->recordProfile(
            "simulation", std::chrono::duration<double, std::milli>(Clock::now() - simulationStart).count());

        int width = 1;
        int height = 1;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        renderer_->beginFrame(width, height);
        const auto renderStart = Clock::now();
        states_->current()->render(*renderer_);
        renderer_->recordProfile(
            "render.total", std::chrono::duration<double, std::milli>(Clock::now() - renderStart).count());
        renderer_->endFrame();
        SDL_GL_SwapWindow(window_);
        ++frames;
        const float fpsInterval = std::chrono::duration<float>(now - fpsStart).count();
        if (fpsInterval >= 0.5F) {
            renderer_->setFramesPerSecond(static_cast<float>(frames) / fpsInterval);
            frames = 0;
            fpsStart = now;
        }
    }

    return 0;
}

} // namespace strategy
