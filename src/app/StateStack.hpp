#pragma once

#include "app/GameState.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace strategy {

class StateStack final {
  public:
    explicit StateStack(StateContext& context);

    template <class State, class... Args> void push(Args&&... args) {
        pending_.push_back({Action::push,
                            std::make_unique<State>(context_, std::forward<Args>(args)...)});
    }

    template <class State, class... Args> void replace(Args&&... args) {
        pending_.push_back({Action::replace,
                            std::make_unique<State>(context_, std::forward<Args>(args)...)});
    }

    void pop();
    void clear();
    void applyPendingChanges();

    [[nodiscard]] GameState* current();
    [[nodiscard]] const GameState* current() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;

  private:
    enum class Action { push, pop, replace, clear };
    struct PendingChange {
        Action action;
        std::unique_ptr<GameState> state;
    };

    StateContext& context_;
    std::vector<std::unique_ptr<GameState>> states_;
    std::vector<PendingChange> pending_;
};

} // namespace strategy
