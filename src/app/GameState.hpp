#pragma once

#include <SDL3/SDL_events.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace strategy {

class Renderer;
class AudioSystem;
class EventBus;
class Logger;
class DefinitionRegistry;

struct StateContext {
    AudioSystem& audio;
    EventBus& events;
    Logger& logger;
    DefinitionRegistry& definitions;
    std::filesystem::path configPath;
};

enum class StateRequest {
    none,
    startGame,
    buildMap,
    loadGame,
    openSettings,
    returnToMainMenu,
    exitApplication
};

class GameState {
  public:
    explicit GameState(StateContext& context)
        : context_(context) {}
    virtual ~GameState() = default;
    virtual void handleEvent(const SDL_Event& event) = 0;
    virtual void update(float deltaSeconds) = 0;
    virtual void render(Renderer& renderer) const = 0;
    virtual StateRequest takeRequest() {
        return StateRequest::none;
    }
    [[nodiscard]] virtual std::uint32_t terrainSeed() const {
        return 0x5EED1234U;
    }
    [[nodiscard]] virtual std::string playerOneCountry() const {
        return "spain";
    }
    [[nodiscard]] virtual std::string playerTwoCountry() const {
        return "japan";
    }

  protected:
    StateContext& context_;
};

} // namespace strategy
