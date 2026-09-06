#pragma once

#include "app\GameState.hpp"
#include "app\StateStack.hpp"
#include "core\EventBus.hpp"

#include <memory>

struct SDL_Window;

namespace strategy {

class AudioSystem;
class Logger;
class Renderer;
class DefinitionRegistry;

class Application final {
  public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

  private:
    SDL_Window* window_{nullptr};
    void* glContext_{nullptr};
    EventBus events_;
    std::unique_ptr<Logger> logger_;
    std::unique_ptr<AudioSystem> audio_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<DefinitionRegistry> definitions_;
    std::unique_ptr<StateContext> stateContext_;
    std::unique_ptr<StateStack> states_;
    bool running_{true};
};

} // namespace strategy
