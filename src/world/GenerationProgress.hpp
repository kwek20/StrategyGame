#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace strategy {

enum class WorldGenerationPhase : std::uint8_t {
    terrainFields,
    water,
    navigation,
    starts,
    resources,
    validation,
    decoration,
    complete
};

struct WorldGenerationProgressSnapshot {
    WorldGenerationPhase phase{WorldGenerationPhase::terrainFields};
    float phaseProgress{0.0F};
    bool cancellationRequested{false};
};

class WorldGenerationCancelled final : public std::runtime_error {
  public:
    WorldGenerationCancelled() : std::runtime_error("World generation cancelled") {}
};

// The worker only writes atomics. The loading screen may read them from the render thread.
class WorldGenerationProgress final {
  public:
    void report(WorldGenerationPhase phase, float progress) {
        phase_.store(phase, std::memory_order_release);
        progressPermille_.store(static_cast<std::uint32_t>(
            std::clamp(progress, 0.0F, 1.0F) * 1000.0F), std::memory_order_release);
        throwIfCancellationRequested();
    }

    void requestCancellation() { cancelled_.store(true, std::memory_order_release); }
    [[nodiscard]] bool cancellationRequested() const {
        return cancelled_.load(std::memory_order_acquire);
    }
    void throwIfCancellationRequested() const {
        if (cancellationRequested()) throw WorldGenerationCancelled{};
    }
    [[nodiscard]] WorldGenerationProgressSnapshot snapshot() const {
        return {phase_.load(std::memory_order_acquire),
                static_cast<float>(progressPermille_.load(std::memory_order_acquire)) / 1000.0F,
                cancellationRequested()};
    }

  private:
    std::atomic<WorldGenerationPhase> phase_{WorldGenerationPhase::terrainFields};
    std::atomic<std::uint32_t> progressPermille_{0};
    std::atomic<bool> cancelled_{false};
};

} // namespace strategy
