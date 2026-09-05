#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace strategy {

class EventBus final {
  public:
    using SubscriptionId = std::size_t;

    template <class Event, class Handler> SubscriptionId subscribe(Handler&& handler) {
        const SubscriptionId id = nextSubscriptionId_++;
        handlers_[typeid(Event)].push_back(
            {id,
             [callback = std::function<void(const Event&)>(std::forward<Handler>(handler))](
                 const void* event) { callback(*static_cast<const Event*>(event)); }});
        return id;
    }

    template <class Event> void unsubscribe(SubscriptionId id) {
        const auto found = handlers_.find(typeid(Event));
        if (found == handlers_.end())
            return;
        auto& handlers = found->second;
        handlers.erase(std::remove_if(handlers.begin(),
                                      handlers.end(),
                                      [id](const HandlerEntry& entry) { return entry.id == id; }),
                       handlers.end());
    }

    template <class Event> void publish(const Event& event) const {
        const auto found = handlers_.find(typeid(Event));
        if (found == handlers_.end())
            return;
        // Copying permits handlers to alter subscriptions during delivery.
        const auto handlers = found->second;
        for (const HandlerEntry& handler : handlers)
            handler.callback(&event);
    }

    template <class Event> void enqueue(Event event) {
        queued_.push_back(std::make_unique<QueuedEvent<Event>>(std::move(event)));
    }

    void dispatchQueued() {
        auto queued = std::move(queued_);
        queued_.clear();
        for (const auto& event : queued)
            event->dispatch(*this);
    }

    void clearQueued() {
        queued_.clear();
    }

  private:
    struct HandlerEntry {
        SubscriptionId id;
        std::function<void(const void*)> callback;
    };

    struct QueuedEventBase {
        virtual ~QueuedEventBase() = default;
        virtual void dispatch(const EventBus& bus) const = 0;
    };

    template <class Event> struct QueuedEvent final : QueuedEventBase {
        explicit QueuedEvent(Event value)
            : value(std::move(value)) {}
        void dispatch(const EventBus& bus) const override {
            bus.publish(value);
        }
        Event value;
    };

    std::unordered_map<std::type_index, std::vector<HandlerEntry>> handlers_;
    std::vector<std::unique_ptr<QueuedEventBase>> queued_;
    SubscriptionId nextSubscriptionId_{1};
};

} // namespace strategy
