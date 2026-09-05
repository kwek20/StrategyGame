#pragma once

#include <cstdint>
#include <string_view>

namespace strategy {

class DeterministicRandom final {
  public:
    DeterministicRandom(std::uint32_t worldSeed, std::string_view streamName) {
        state_ = worldSeed;
        for (char value : streamName) state_ = (state_ ^ static_cast<unsigned char>(value)) * 16777619U;
        if (state_ == 0) state_ = 0x6D2B79F5U;
    }
    [[nodiscard]] std::uint32_t next() {
        std::uint32_t value = state_;
        value ^= value << 13; value ^= value >> 17; value ^= value << 5;
        state_ = value; return value;
    }
    [[nodiscard]] float range(float minimum, float maximum) {
        const float normalized = static_cast<float>(next() >> 8) * (1.0F / 16777216.0F);
        return minimum + (maximum - minimum) * normalized;
    }
  private: std::uint32_t state_{};
};
} // namespace strategy
