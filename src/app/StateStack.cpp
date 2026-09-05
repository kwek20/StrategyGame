#include "app/StateStack.hpp"

namespace strategy {

StateStack::StateStack(StateContext& context)
    : context_(context) {}

void StateStack::pop() {
    pending_.push_back({Action::pop, nullptr});
}

void StateStack::clear() {
    pending_.push_back({Action::clear, nullptr});
}

void StateStack::applyPendingChanges() {
    for (auto& change : pending_) {
        switch (change.action) {
        case Action::push:
            states_.push_back(std::move(change.state));
            break;
        case Action::pop:
            if (!states_.empty())
                states_.pop_back();
            break;
        case Action::replace:
            states_.clear();
            states_.push_back(std::move(change.state));
            break;
        case Action::clear:
            states_.clear();
            break;
        }
    }
    pending_.clear();
}

GameState* StateStack::current() {
    return states_.empty() ? nullptr : states_.back().get();
}

const GameState* StateStack::current() const {
    return states_.empty() ? nullptr : states_.back().get();
}

bool StateStack::empty() const {
    return states_.empty();
}

std::size_t StateStack::size() const {
    return states_.size();
}

} // namespace strategy
