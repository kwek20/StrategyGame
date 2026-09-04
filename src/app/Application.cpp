#include "app/Application.hpp"

#include "app/GameState.hpp"
#include "game/PlayState.hpp"
#include "game/BuildState.hpp"
#include "game/StartMenuState.hpp"
#include "game/SettingsState.hpp"
#include "persistence/GameConfig.hpp"
#include "persistence/SaveGame.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"

#include <SDL3/SDL.h>
#include <glad/glad.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <iostream>
#include <string_view>

namespace strategy {
namespace {

[[noreturn]] void throwSdlError(const char* action) {
    throw std::runtime_error(std::string(action) + ": " + SDL_GetError());
}

std::string_view loadingGlyph(char value) {
    switch(value){case 'L':return "10000100001000010000100001000011111";case 'O':return "01110100011000110001100011000101110";case 'A':return "01110100011000111111100011000110001";case 'D':return "11110100011000110001100011000111110";case 'I':return "11111001000010000100001000010011111";case 'N':return "10001110011010110011100011000110001";case 'G':return "01110100011000010111100011000101110";default:return {};}
}

void drawBootstrapLoading(SDL_Window* window) {
    int width=1,height=1;SDL_GetWindowSizeInPixels(window,&width,&height);glViewport(0,0,width,height);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);constexpr std::string_view word="LOADING";constexpr int pixel=7,advance=7*pixel;const int left=(width-static_cast<int>(word.size())*advance)/2,top=(height-7*pixel)/2;
    for(std::size_t letter=0;letter<word.size();++letter){const auto glyph=loadingGlyph(word[letter]);for(int row=0;row<7;++row)for(int column=0;column<5;++column)if(glyph[static_cast<std::size_t>(row*5+column)]=='1'){glScissor(left+static_cast<int>(letter)*advance+column*pixel,height-(top+(row+1)*pixel),pixel-1,pixel-1);glClearColor(0.9F,0.94F,0.96F,1);glClear(GL_COLOR_BUFFER_BIT);}}
    glDisable(GL_SCISSOR_TEST);SDL_GL_SwapWindow(window);
}

} // namespace

Application::Application() {
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

    window_ = SDL_CreateWindow(Text::get("menu.title").c_str(), 1280, 720,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

    drawBootstrapLoading(window_);
    SDL_GL_SetSwapInterval(1);
    renderer_ = std::make_unique<Renderer>();
    state_ = std::make_unique<StartMenuState>();
}

Application::~Application() {
    state_.reset();
    renderer_.reset();
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
    const auto showLoading=[this](float progress,const char* textKey){int width=1,height=1;SDL_GetWindowSizeInPixels(window_,&width,&height);renderer_->beginFrame(width,height);renderer_->drawLoadingScreen(progress,Text::get(textKey));SDL_GL_SwapWindow(window_);SDL_PumpEvents();};

    while (running_) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            }
            state_->handleEvent(event);
        }

        if (state_->request() == StateRequest::startGame) {
            const std::uint32_t seed = state_->terrainSeed();
            const std::string playerOneCountry=state_->playerOneCountry();
            const std::string playerTwoCountry=state_->playerTwoCountry();
            showLoading(0.08F,"loading.terrain");renderer_->regenerateTerrain(seed);
            showLoading(0.55F,"loading.world");auto next=std::make_unique<PlayState>(seed,playerOneCountry,playerTwoCountry);
            showLoading(0.94F,"loading.finalize");state_=std::move(next);
        } else if (state_->request() == StateRequest::buildMap) {
            const std::uint32_t seed = state_->terrainSeed();
            showLoading(0.08F,"loading.terrain");renderer_->regenerateTerrain(seed);
            showLoading(0.60F,"loading.world");auto next=std::make_unique<BuildState>(seed);
            showLoading(0.94F,"loading.finalize");state_=std::move(next);
        } else if (state_->request() == StateRequest::loadGame) {
            try {
                showLoading(0.08F,"loading.save");
                const GameConfig config = GameConfig::load("gamedata/config.json");
                SaveData data = SaveGame::read(config.savePath());
                showLoading(0.30F,"loading.terrain");renderer_->regenerateTerrain(data.terrainSeed);
                showLoading(0.62F,"loading.world");auto next=std::make_unique<PlayState>(std::move(data));
                showLoading(0.94F,"loading.finalize");state_=std::move(next);
            } catch (const std::exception& error) {
                std::cerr << "Could not load from main menu: " << error.what() << '\n';
                state_ = std::make_unique<StartMenuState>();
            }
        } else if (state_->request() == StateRequest::returnToMainMenu) {
            state_ = std::make_unique<StartMenuState>();
        } else if(state_->request()==StateRequest::openSettings) {
            state_=std::make_unique<SettingsState>();
        } else if (state_->request() == StateRequest::exitApplication) {
            running_ = false;
        }
        if (!running_) {
            break;
        }

        const auto now = Clock::now();
        const float delta = std::chrono::duration<float>(now - previous).count();
        previous = now;

        state_->update(delta);

        int width = 1;
        int height = 1;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        renderer_->beginFrame(width, height);
        state_->render(*renderer_);
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
