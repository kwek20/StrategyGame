#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace strategy {

struct ProfileMetric {
    std::string name;
    double lastMilliseconds{0};
    double averageMilliseconds{0};
    double peakMilliseconds{0};
};

class FrameProfiler final {
  public:
    void beginFrame() {
        current_.clear();
        frameStart_ = Clock::now();
    }
    void record(const std::string& name, double milliseconds) { current_[name] += milliseconds; }
    void endFrame() {
        record("frame.total", millisecondsSince(frameStart_));
        for (const auto& [name, value] : current_) {
            Entry& entry = entries_[name];
            entry.last = value;
            entry.average = entry.samples == 0 ? value : entry.average * 0.9 + value * 0.1;
            entry.peak = std::max(entry.peak, value);
            ++entry.samples;
        }
    }
    [[nodiscard]] std::vector<ProfileMetric> snapshot() const {
        std::vector<ProfileMetric> result;
        result.reserve(entries_.size());
        for (const auto& [name, entry] : entries_)
            result.push_back({name, entry.last, entry.average, entry.peak});
        return result;
    }
    [[nodiscard]] double last(const std::string& name) const {
        const auto found = entries_.find(name);
        return found == entries_.end() ? 0.0 : found->second.last;
    }

    using Clock = std::chrono::steady_clock;
    [[nodiscard]] static double millisecondsSince(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

  private:
    struct Entry {
        double last{0}, average{0}, peak{0};
        std::size_t samples{0};
    };
    Clock::time_point frameStart_{Clock::now()};
    std::map<std::string, double> current_;
    std::map<std::string, Entry> entries_;
};

class ProfileScope final {
  public:
    ProfileScope(FrameProfiler& profiler, std::string name)
        : profiler_(profiler), name_(std::move(name)), start_(FrameProfiler::Clock::now()) {}
    ~ProfileScope() { profiler_.record(name_, FrameProfiler::millisecondsSince(start_)); }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

  private:
    FrameProfiler& profiler_;
    std::string name_;
    FrameProfiler::Clock::time_point start_;
};

} // namespace strategy
