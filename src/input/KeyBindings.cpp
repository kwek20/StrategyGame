#include "input/KeyBindings.hpp"

#include "localization/Text.hpp"

#include <algorithm>
#include <cmath>

namespace strategy {
namespace {
constexpr unsigned strategy = 1, placement = 2, direct = 4, editor = 8;
InputBinding key(SDL_Keycode code, unsigned mods = 0) {
    return {BindingDevice::key, static_cast<int>(code), mods};
}
InputBinding mouse(int button) {
    return {BindingDevice::mouse, button};
}
InputBinding drag(int button) {
    return {BindingDevice::drag, button};
}
InputBinding modifier(unsigned mods) {
    return {BindingDevice::modifier, static_cast<int>(mods)};
}
bool overlaps(const InputBinding& a, const InputBinding& b, bool extraA, bool extraB) {
    const bool pointerPair =
        (a.device == BindingDevice::mouse || a.device == BindingDevice::drag) &&
        (b.device == BindingDevice::mouse || b.device == BindingDevice::drag);
    if (!a || !b || (!pointerPair && a.device != b.device) || a.code != b.code)
        return false;
    if (a.modifiers == b.modifiers)
        return true;
    return (extraA && (b.modifiers & a.modifiers) == a.modifiers) ||
           (extraB && (a.modifiers & b.modifiers) == b.modifiers);
}
} // namespace
bool inputBindingsOverlap(const InputBinding& a, const InputBinding& b, bool extraA, bool extraB) {
    return overlaps(a, b, extraA, extraB);
}
unsigned normalizedModifiers(SDL_Keymod mods) {
    unsigned value = 0;
    if (mods & SDL_KMOD_CTRL)
        value |= 1;
    if (mods & SDL_KMOD_SHIFT)
        value |= 2;
    if (mods & SDL_KMOD_ALT)
        value |= 4;
    if (mods & SDL_KMOD_GUI)
        value |= 8;
    return value;
}
unsigned modifierForKey(SDL_Keycode code) {
    if (code == SDLK_LCTRL || code == SDLK_RCTRL)
        return 1;
    if (code == SDLK_LSHIFT || code == SDLK_RSHIFT)
        return 2;
    if (code == SDLK_LALT || code == SDLK_RALT)
        return 4;
    if (code == SDLK_LGUI || code == SDLK_RGUI)
        return 8;
    return 0;
}
const std::vector<InputAction>& inputActions() {
    static const auto actions = [] {
        std::vector<InputAction> result;
        const auto add = [&](std::string id,
                             std::string category,
                             unsigned contexts,
                             InputBinding first,
                             InputBinding second = {},
                             bool extra = false,
                             bool requiresRelease = false) {
            result.push_back({id,
                              "keybind.action." + id,
                              category,
                              contexts,
                              {first, second},
                              extra,
                              requiresRelease});
        };
        add("forward", "camera", strategy | placement | editor, key(SDLK_W), {}, true, true);
        add("backward", "camera", strategy | placement | editor, key(SDLK_S), {}, true, true);
        add("left", "camera", strategy | placement | editor, key(SDLK_A), {}, true, true);
        add("right", "camera", strategy | placement | editor, key(SDLK_D), {}, true, true);
        add("camera_fast", "camera", strategy | editor, modifier(2), {}, false, true);
        add("camera_pan",
            "camera",
            strategy | placement | editor,
            drag(SDL_BUTTON_RIGHT),
            {},
            true,
            true);
        add("camera_orbit",
            "camera",
            strategy | placement | editor,
            drag(SDL_BUTTON_MIDDLE),
            {},
            true,
            true);
        add("zoom_in",
            "camera",
            strategy | placement | direct | editor,
            {BindingDevice::wheel, 1},
            {},
            true);
        add("zoom_out",
            "camera",
            strategy | placement | direct | editor,
            {BindingDevice::wheel, -1},
            {},
            true);
        add("select", "selection", strategy, mouse(SDL_BUTTON_LEFT), {}, true, true);
        add("selection_add", "selection", strategy, modifier(2), {}, false, true);
        add("focus_hub", "selection", strategy, key(SDLK_H));
        add("idle_drone", "selection", strategy, key(SDLK_PERIOD));
        add("all_idle_drones", "selection", strategy, key(SDLK_PERIOD, 2));
        add("place", "building", placement | editor, mouse(SDL_BUTTON_LEFT), {}, true);
        add("repeat_build", "building", placement, modifier(2), {}, false, true);
        add("range_lock", "building", placement, modifier(1), {}, false, true);
        add("rotate_building", "building", placement, mouse(SDL_BUTTON_RIGHT), key(SDLK_R), true);
        add("cancel_placement", "building", placement, key(SDLK_ESCAPE));
        add("copy_building", "building", strategy, key(SDLK_B));
        add("order", "orders", strategy, mouse(SDL_BUTTON_RIGHT), {}, true);
        add("stop", "orders", strategy, key(SDLK_X));
        add("cancel_construction", "building", strategy, key(SDLK_DELETE));
        add("possess", "direct", strategy | direct, key(SDLK_P));
        add("unit_forward", "direct", direct, key(SDLK_W), {}, true, true);
        add("unit_backward", "direct", direct, key(SDLK_S), {}, true, true);
        add("unit_left", "direct", direct, key(SDLK_A), {}, true, true);
        add("unit_right", "direct", direct, key(SDLK_D), {}, true, true);
        add("unit_run", "direct", direct, modifier(2), {}, false, true);
        add("unit_fire", "direct", direct, mouse(SDL_BUTTON_LEFT), {}, true, true);
        add("unit_aim_zoom", "direct", direct, mouse(SDL_BUTTON_RIGHT), {}, true, true);
        add("pause", "interface", strategy | direct, key(SDLK_ESCAPE));
        add("quick_save", "interface", strategy | placement | direct, key(SDLK_F5));
        add("quick_load", "interface", strategy | placement | direct, key(SDLK_F9));
        add("debug", "diagnostics", strategy | placement | direct, key(SDLK_F3));
        add("terrain_debug", "diagnostics", strategy | placement | direct, key(SDLK_F4));
        add("water_debug", "diagnostics", strategy | placement | direct, key(SDLK_F6));
        add("power_debug", "diagnostics", strategy | placement | direct, key(SDLK_F7));
        add("editor_exit", "editor", editor, key(SDLK_ESCAPE));
        add("editor_team", "editor", editor, key(SDLK_T));
        add("editor_undo", "editor", editor, key(SDLK_BACKSPACE));
        add("editor_save", "editor", editor, key(SDLK_F5));
        for (int i = 0; i < 12; ++i) {
            const SDL_Keycode code = i < 9     ? SDLK_1 + i
                                     : i == 9  ? SDLK_0
                                     : i == 10 ? SDLK_MINUS
                                               : SDLK_EQUALS;
            add("action_" + std::to_string(i + 1),
                "orders",
                strategy | placement,
                key(code, 4),
                {},
                true);
            if (i < 9)
                add("editor_slot_" + std::to_string(i + 1), "editor", editor, key(code));
            if (i < 10) {
                add("group_" + std::to_string(i + 1), "selection", strategy, key(code));
                add("assign_group_" + std::to_string(i + 1), "selection", strategy, key(code, 1));
            }
        }
        return result;
    }();
    return actions;
}
const InputAction* inputAction(std::string_view id) {
    static const auto lookup = [] {
        std::map<std::string_view, const InputAction*, std::less<>> result;
        for (const auto& action : inputActions())
            result.emplace(action.id, &action);
        return result;
    }();
    const auto found = lookup.find(id);
    return found == lookup.end() ? nullptr : found->second;
}
std::string bindingName(const InputBinding& binding) {
    if (!binding)
        return Text::get("keybind.unbound");
    std::string result;
    const unsigned mods = binding.device == BindingDevice::modifier
                              ? static_cast<unsigned>(binding.code)
                              : binding.modifiers;
    for (const auto& [mask, label] :
         {std::pair{1U, "Ctrl"}, {2U, "Shift"}, {4U, "Alt"}, {8U, "Super"}})
        if (mods & mask) {
            if (!result.empty())
                result += "+";
            result += label;
        }
    if (binding.device == BindingDevice::modifier)
        return result;
    if (!result.empty())
        result += "+";
    if (binding.device == BindingDevice::mouse || binding.device == BindingDevice::drag) {
        const auto button = binding.code == SDL_BUTTON_LEFT    ? Text::get("keybind.mouse_left")
                            : binding.code == SDL_BUTTON_RIGHT ? Text::get("keybind.mouse_right")
                            : binding.code == SDL_BUTTON_MIDDLE
                                ? Text::get("keybind.mouse_middle")
                                : Text::format("keybind.mouse", {std::to_string(binding.code)});
        result += binding.device == BindingDevice::drag
                      ? Text::format("keybind.mouse_drag", {button})
                      : button;
    } else if (binding.device == BindingDevice::wheel)
        result += Text::get(binding.code > 0 ? "keybind.wheel_up" : "keybind.wheel_down");
    else
        result += SDL_GetKeyName(static_cast<SDL_Keycode>(binding.code));
    return result;
}
InputBinding bindingFromEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        const auto mod = modifierForKey(event.key.key);
        if (mod)
            return modifier(mod);
        return key(event.key.key, normalizedModifiers(event.key.mod));
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        return {BindingDevice::mouse, event.button.button, normalizedModifiers(SDL_GetModState())};
    if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0)
        return {BindingDevice::wheel,
                event.wheel.y > 0 ? 1 : -1,
                normalizedModifiers(SDL_GetModState())};
    return {};
}
void InputState::observe(const SDL_Event& event) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        clear();
    if (event.type == SDL_EVENT_KEY_DOWN)
        keys_.insert(static_cast<int>(event.key.key));
    if (event.type == SDL_EVENT_KEY_UP)
        keys_.erase(static_cast<int>(event.key.key));
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        buttons_.insert(event.button.button);
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        buttons_.erase(event.button.button);
}
void InputState::clear() {
    keys_.clear();
    buttons_.clear();
}
bool InputState::held(const InputBinding& binding) const {
    if (!binding)
        return false;
    unsigned mods = normalizedModifiers(SDL_GetModState());
    for (int code : keys_)
        mods |= modifierForKey(static_cast<SDL_Keycode>(code));
    if (binding.device == BindingDevice::modifier)
        return (mods & binding.code) == static_cast<unsigned>(binding.code);
    if ((mods & binding.modifiers) != binding.modifiers)
        return false;
    if (binding.device == BindingDevice::key)
        return keys_.contains(binding.code);
    if (binding.device == BindingDevice::mouse || binding.device == BindingDevice::drag)
        return buttons_.contains(binding.code);
    return false;
}
KeyBindings::KeyBindings() {
    reset();
}
void KeyBindings::reset(std::string_view category) {
    for (const auto& action : inputActions())
        if (category.empty() || action.category == category)
            values[action.id] = action.defaults;
}
bool KeyBindings::matches(std::string_view id,
                          const SDL_Event& event,
                          InputContext context,
                          bool release) const {
    const auto* action = inputAction(id);
    if (!action || !(action->contexts & static_cast<unsigned>(context)))
        return false;
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat)
        return false;
    if (release ? (event.type != SDL_EVENT_KEY_UP && event.type != SDL_EVENT_MOUSE_BUTTON_UP)
                : (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
                   event.type != SDL_EVENT_MOUSE_WHEEL))
        return false;
    const auto found = values.find(action->id);
    if (found == values.end())
        return false;
    const auto input = bindingFromEvent(event);
    for (auto binding : found->second) {
        if (binding.device == BindingDevice::drag && input.device == BindingDevice::mouse)
            binding.device = BindingDevice::mouse;
        if (!binding || binding.device != input.device || binding.code != input.code)
            continue;
        if (release || binding.modifiers == input.modifiers ||
            (action->extraModifiers && (input.modifiers & binding.modifiers) == binding.modifiers))
            return true;
    }
    return false;
}
bool KeyBindings::held(std::string_view id, const InputState& state) const {
    const auto found = values.find(std::string{id});
    return found != values.end() && std::any_of(found->second.begin(),
                                                found->second.end(),
                                                [&](const auto& b) { return state.held(b); });
}
std::string KeyBindings::label(std::string_view id) const {
    const auto found = values.find(std::string{id});
    if (found == values.end())
        return Text::get("keybind.unbound");
    if (found->second[0])
        return bindingName(found->second[0]);
    return bindingName(found->second[1]);
}
std::vector<std::string>
KeyBindings::conflicts(std::string_view id, unsigned slot, const InputBinding& proposed) const {
    std::vector<std::string> result;
    const auto* action = inputAction(id);
    if (!action || slot > 1 || !proposed)
        return result;
    for (const auto& other : inputActions()) {
        if (!(action->contexts & other.contexts))
            continue;
        // These independent modifiers deliberately share Shift in strategy mode.
        if ((id == "camera_fast" && other.id == "selection_add") ||
            (id == "selection_add" && other.id == "camera_fast"))
            continue;
        const auto found = values.find(other.id);
        if (found == values.end())
            continue;
        for (unsigned i = 0; i < 2; ++i)
            if (!(other.id == id && i == slot) &&
                overlaps(
                    proposed, found->second[i], action->extraModifiers, other.extraModifiers)) {
                const bool panClick =
                    ((id == "camera_pan" &&
                      (other.id == "order" || other.id == "rotate_building")) ||
                     (other.id == "camera_pan" && (id == "order" || id == "rotate_building"))) &&
                    proposed.device != found->second[i].device &&
                    (proposed.device == BindingDevice::drag ||
                     found->second[i].device == BindingDevice::drag);
                if (panClick)
                    continue;
                result.push_back(other.id);
                break;
            }
    }
    return result;
}
void KeyBindings::applyPreset(std::string_view preset) {
    reset();
    if (preset != "grid")
        return;
    for (const auto& [id, code] : {std::pair{"forward", SDLK_UP},
                                   {"backward", SDLK_DOWN},
                                   {"left", SDLK_LEFT},
                                   {"right", SDLK_RIGHT}})
        values[id][0] = key(code);
    const SDL_Keycode grid[]{SDLK_Q,
                             SDLK_W,
                             SDLK_E,
                             SDLK_R,
                             SDLK_A,
                             SDLK_S,
                             SDLK_D,
                             SDLK_F,
                             SDLK_Z,
                             SDLK_X,
                             SDLK_C,
                             SDLK_V};
    for (int i = 0; i < 12; ++i)
        values["action_" + std::to_string(i + 1)][0] = key(grid[i]);
    values["rotate_building"][1] = key(SDLK_T);
    values["stop"][0] = key(SDLK_SPACE);
}
} // namespace strategy
