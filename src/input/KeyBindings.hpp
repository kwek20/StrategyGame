#pragma once

#include <SDL3/SDL.h>
#include <array>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace strategy {
enum class InputContext : unsigned { strategy = 1, placement = 2, direct = 4, editor = 8 };
enum class BindingDevice { key, mouse, drag, wheel, modifier };
struct InputBinding {
    BindingDevice device{BindingDevice::key};
    int code{};
    unsigned modifiers{};
    bool operator==(const InputBinding&) const = default;
    explicit operator bool() const {
        return code != 0;
    }
};
using BindingSlots = std::array<InputBinding, 2>;
struct InputAction {
    std::string id;
    std::string labelKey;
    std::string category;
    unsigned contexts{};
    BindingSlots defaults;
    bool extraModifiers{};
    bool requiresRelease{};
};
[[nodiscard]] bool
inputBindingsOverlap(const InputBinding& a, const InputBinding& b, bool extraA, bool extraB);
[[nodiscard]] unsigned normalizedModifiers(SDL_Keymod modifiers);
[[nodiscard]] unsigned modifierForKey(SDL_Keycode key);
[[nodiscard]] const std::vector<InputAction>& inputActions();
[[nodiscard]] const InputAction* inputAction(std::string_view id);
[[nodiscard]] std::string bindingName(const InputBinding& binding);
[[nodiscard]] InputBinding bindingFromEvent(const SDL_Event& event);

class InputState {
  public:
    void observe(const SDL_Event& event);
    void clear();
    [[nodiscard]] bool held(const InputBinding& binding) const;

  private:
    std::set<int> keys_;
    std::set<int> buttons_;
};
class KeyBindings {
  public:
    KeyBindings();
    std::map<std::string, BindingSlots> values;
    [[nodiscard]] bool matches(std::string_view action,
                               const SDL_Event& event,
                               InputContext context,
                               bool release = false) const;
    [[nodiscard]] bool held(std::string_view action, const InputState& state) const;
    [[nodiscard]] std::string label(std::string_view action) const;
    [[nodiscard]] std::vector<std::string>
    conflicts(std::string_view action, unsigned slot, const InputBinding& proposed) const;
    void reset(std::string_view category = {});
    void applyPreset(std::string_view preset);
};
} // namespace strategy
