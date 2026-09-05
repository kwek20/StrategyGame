#include "app/StateStack.hpp"
#include "audio/AudioSystem.hpp"
#include "core/EventBus.hpp"
#include "diagnostics/Logger.hpp"
#include "ui/UiDocument.hpp"

#include <filesystem>
#include <iostream>

namespace {

class TestState final : public strategy::GameState {
  public:
    TestState(strategy::StateContext& context, int value)
        : GameState(context)
        , value(value) {}

    void handleEvent(const SDL_Event&) override {}
    void update(float) override {}
    void render(strategy::Renderer&) const override {}

    int value;
};

} // namespace

int main() {
    strategy::AudioSystem audio;
    strategy::EventBus events;
    strategy::Logger logger{std::filesystem::temp_directory_path() / "strategy-test.log"};
    strategy::StateContext context{audio, events, logger, "test-config.json"};
    strategy::StateStack stack{context};

    bool valid = stack.empty();
    stack.push<TestState>(1);
    valid = valid && stack.empty(); // Mutations are deferred until the dispatch boundary.
    stack.applyPendingChanges();
    valid = valid && stack.size() == 1 &&
            static_cast<TestState*>(stack.current())->value == 1;

    stack.push<TestState>(2);
    stack.applyPendingChanges();
    valid = valid && stack.size() == 2 &&
            static_cast<TestState*>(stack.current())->value == 2;

    stack.pop();
    stack.applyPendingChanges();
    valid = valid && stack.size() == 1 &&
            static_cast<TestState*>(stack.current())->value == 1;

    stack.replace<TestState>(3);
    stack.applyPendingChanges();
    valid = valid && stack.size() == 1 &&
            static_cast<TestState*>(stack.current())->value == 3;

    stack.clear();
    stack.applyPendingChanges();
    valid = valid && stack.empty() && stack.current() == nullptr;

    struct TestEvent {
        int value;
    };
    int received = 0;
    const auto subscription = events.subscribe<TestEvent>(
        [&](const TestEvent& event) { received += event.value; });
    events.publish(TestEvent{2});
    events.enqueue(TestEvent{3});
    valid = valid && received == 2;
    events.dispatchQueued();
    valid = valid && received == 5;
    events.unsubscribe<TestEvent>(subscription);
    events.publish(TestEvent{10});
    valid = valid && received == 5;

    strategy::UiDocument ui;
    ui.button("confirm", {10, 10, 110, 50}, "Confirm");
    ui.textField("name", {10, 60, 210, 100}, "Player");
    ui.pointerMoved({20, 20});
    valid = valid && ui.hovered("confirm") && !ui.hovered("name");
    const auto activated = ui.activate({20, 75});
    ui.focus(activated ? *activated : "");
    valid = valid && activated && *activated == "name" && ui.focused("name");

    logger.info("test", "diagnostic entry");
    logger.flush();
    const auto entries = logger.recentEntries();
    valid = valid && !entries.empty() && entries.back().category == "test" &&
            entries.back().message == "diagnostic entry";

    if (!valid) {
        std::cerr << "State stack validation failed\n";
        return 1;
    }
    std::cout << "State stack validation passed\n";
    return 0;
}
