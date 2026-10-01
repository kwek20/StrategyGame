#pragma once

#include <SDL3/SDL_events.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace strategy {

struct RmlUiScreenDefinition {
    std::string documentPath;
    std::vector<std::pair<std::string, std::string>> text;
    std::vector<std::string> focusOrder;
    std::vector<std::string> actionIds;
    float uiScale{1.0F};
};
using RmlUiScreenHandle = std::uint64_t;
struct RmlUiRect {
    float left{}, top{}, right{}, bottom{};
    [[nodiscard]] bool contains(float x, float y) const {
        return x >= left && x <= right && y >= top && y <= bottom;
    }
};

class RmlUiManager final {
  public:
    RmlUiManager();
    ~RmlUiManager();
    RmlUiManager(const RmlUiManager&) = delete;
    RmlUiManager& operator=(const RmlUiManager&) = delete;

    [[nodiscard]] RmlUiScreenHandle pushScreen(RmlUiScreenDefinition definition);
    void removeScreen(RmlUiScreenHandle handle);
    void handleEvent(const SDL_Event& event);
    void focus(int direction);
    void activateFocused();
    [[nodiscard]] std::string focusedId() const;
    [[nodiscard]] std::optional<std::string> takeAction();
    [[nodiscard]] bool pointerOverUi() const;
    void setControls(std::vector<std::string> focusOrder,
                     std::vector<std::string> actionIds);
    [[nodiscard]] std::optional<RmlUiRect> bounds(const std::string& id) const;
    void setText(const std::string& id, const std::string& text);
    void setValue(const std::string& id, const std::string& value);
    [[nodiscard]] std::string value(const std::string& id) const;
    void setAttribute(const std::string& id, const std::string& name, const std::string& value);
    void setProperty(const std::string& id, const std::string& name, const std::string& value);
    void render(int width, int height);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace strategy
