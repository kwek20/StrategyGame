#include "input/KeyBindings.hpp"
#include "persistence/GameConfig.hpp"

#include <filesystem>
#include <stdexcept>
using namespace strategy;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int strategyTestMain() {
    KeyBindings keys;
    for (const auto& action : inputActions())
        for (unsigned i = 0; i < 2; ++i)
            require(keys.conflicts(action.id, i, keys.values.at(action.id)[i]).empty(),
                    "Default bindings must have no ambiguous actions in the same context");
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_W;
    require(keys.matches("unit_forward", event, InputContext::direct) &&
                keys.matches("vehicle_forward", event, InputContext::vehicle) &&
                keys.matches("aircraft_throttle_up", event, InputContext::aircraft) &&
                !keys.matches("unit_forward", event, InputContext::vehicle) &&
                !keys.matches("vehicle_forward", event, InputContext::aircraft),
            "Foot, vehicle and aircraft movement use independent contexts");
    event.key.key = SDLK_ESCAPE;
    require(keys.matches("pause", event, InputContext::vehicle) &&
                keys.matches("pause", event, InputContext::aircraft),
            "Shared controls remain available in every direct-control context");
    require(!keys.conflicts("vehicle_forward", 0, {BindingDevice::key, static_cast<int>(SDLK_ESCAPE)}).empty(),
            "Shared controls conflict with vehicle bindings");
    keys.values["vehicle_forward"][0] = {BindingDevice::key, static_cast<int>(SDLK_I)};
    keys.reset("foot");
    require(keys.values.at("vehicle_forward")[0].code == static_cast<int>(SDLK_I),
            "Resetting foot controls preserves vehicle bindings");
    keys.reset("vehicle");
    event.key.key = SDLK_W;
    require(keys.matches("forward", event, InputContext::strategy) &&
                !keys.matches("unit_forward", event, InputContext::strategy),
            "Camera and direct-control actions are context-specific");
    event.key.mod = SDL_KMOD_LSHIFT;
    require(keys.matches("forward", event, InputContext::strategy),
            "Holding a utility modifier does not disable camera movement");
    event.key.key = SDLK_1;
    event.key.mod = SDL_KMOD_LCTRL;
    require(keys.matches("assign_group_1", event, InputContext::strategy) &&
                !keys.matches("group_1", event, InputContext::strategy),
            "Control-group modifiers must not also recall the group");
    require(!keys.matches("assign_group_1", event, InputContext::placement),
            "Ctrl range lock cannot accidentally assign a group during placement");
    InputState state;
    SDL_SetModState(SDL_KMOD_NONE);
    event.key.key = SDLK_LCTRL;
    state.observe(event);
    event.key.key = SDLK_LSHIFT;
    state.observe(event);
    require(keys.held("range_lock", state) && keys.held("repeat_build", state),
            "Ctrl and Shift utilities can be combined independently");
    event.type = SDL_EVENT_KEY_UP;
    event.key.key = SDLK_LCTRL;
    state.observe(event);
    require(!keys.held("range_lock", state) && keys.held("repeat_build", state),
            "Releasing one modifier preserves the other");
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    state.observe(event);
    require(!keys.held("repeat_build", state), "Focus loss clears held keys");
    require(!keys.conflicts("range_lock", 0, {BindingDevice::modifier, 2}).empty(),
            "Reusing Shift for both placement modifiers is a real conflict");
    require(
        keys.conflicts("unit_forward", 0, {BindingDevice::key, static_cast<int>(SDLK_W)}).empty(),
        "Different input contexts can reuse keys");
    require(!keys.conflicts("camera_orbit", 0, {BindingDevice::drag, SDL_BUTTON_LEFT}).empty(),
            "A selection drag and camera drag cannot share a button");
    keys.values["range_lock"][1] = {BindingDevice::key, static_cast<int>(SDLK_L)};
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_L;
    state.observe(event);
    require(keys.held("range_lock", state),
            "Range locking can use a held ordinary key as secondary binding");
    keys.applyPreset("grid");
    for (const auto& action : inputActions())
        for (unsigned i = 0; i < 2; ++i)
            require(keys.conflicts(action.id, i, keys.values.at(action.id)[i]).empty(),
                    "The grid preset must resolve WASD and rotation conflicts");
    require(keys.values.at("forward")[0].code == static_cast<int>(SDLK_UP) &&
                keys.values.at("unit_forward")[0].code == static_cast<int>(SDLK_W),
            "Grid camera changes must not alter direct-control movement");
    const auto path = std::filesystem::temp_directory_path() / "strategy-key-bindings.json";
    GameConfig config;
    config.keybinds = keys;
    config.keybinds.values["range_lock"][1] = {BindingDevice::mouse, SDL_BUTTON_X1, 4};
    config.keybinds.values["stop"] = {};
    config.keybinds.values["vehicle_fire"][0] = {BindingDevice::key, static_cast<int>(SDLK_SPACE)};
    config.keybinds.values["aircraft_roll_left"][1] = {BindingDevice::key, static_cast<int>(SDLK_LEFT)};
    config.write(path);
    const auto restored = GameConfig::load(path);
    require(restored.keybinds.values == config.keybinds.values,
            "Settings preserve both slots, modifiers, mouse bindings and explicit unbinding");
    std::filesystem::remove(path);
    return 0;
}
