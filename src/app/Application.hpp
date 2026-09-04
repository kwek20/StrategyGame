#pragma once

#include <memory>

struct SDL_Window;

namespace strategy {

class GameState;
class Renderer;

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
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<GameState> state_;
    bool running_{true};
};

} // namespace strategy

