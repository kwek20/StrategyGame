#pragma once
#include "assets/ResourceHandle.hpp"

#include <SDL3/SDL_audio.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace strategy {
enum class AudioCue {
    uiClick,
    menuAmbient,
    gameAmbient,
    buildAmbient,
    selection,
    moveOrder,
    buildingOpen,
    buildingUpgrade,
    trainingUpgrade,
    trainUnit,
    saveGame,
    loadGame,
    possessUnit,
    gatherOrder,
    deliveryComplete,
    conversionComplete,
    resourceWarning,
    cannonFire,
    cannonImpact,
    vehicleDestroyed,
    count
};
class AudioSystem final {
  public:
    AudioSystem() = default;
    ~AudioSystem();
    void load(const std::filesystem::path& manifest = "assets/audio/audio.json");
    void setMasterVolume(float volume);
    void setMusicVolume(float volume);
    void setEffectsVolume(float volume);
    void setMuted(bool muted);
    void play(AudioCue cue);
    void play(AudioHandle handle);
    void setAmbient(AudioCue cue);
    [[nodiscard]] AudioHandle handle(AudioCue cue) const;
    [[nodiscard]] ResourceState state(AudioHandle handle) const;
    [[nodiscard]] float masterVolume() const {
        return masterVolume_;
    }
    [[nodiscard]] float musicVolume() const {
        return musicVolume_;
    }
    [[nodiscard]] float effectsVolume() const {
        return effectsVolume_;
    }
    [[nodiscard]] bool muted() const {
        return muted_;
    }
    [[nodiscard]] std::uint64_t emittedCueCount() const {
        return emittedCueCount_;
    }
    [[nodiscard]] std::uint64_t mixedSoundSamples() const {
        return mixedSoundSamples_.load();
    }

  private:
    std::atomic<float> masterVolume_{1.0F}, musicVolume_{0.8F}, effectsVolume_{1.0F};
    std::atomic<bool> muted_{false};
    struct AudioSlot {
        std::uint32_t generation{1};
        std::filesystem::path path;
        ResourceState state{ResourceState::ready};
        std::shared_ptr<std::vector<float>> samples;
    };
    std::vector<AudioSlot> assets_;
    AudioCue ambient_{AudioCue::menuAmbient};
    std::uint64_t emittedCueCount_{0};
    SDL_AudioStream* stream_{};
    bool ownsAudioSubsystem_{false};
    struct Voice {
        std::shared_ptr<std::vector<float>> samples;
        std::size_t offset{};
        bool music{};
    };
    std::vector<Voice> voices_;
    std::mutex voicesMutex_;
    std::atomic<std::uint64_t> mixedSoundSamples_{0};
    void mix(SDL_AudioStream*, int bytes);
};
} // namespace strategy
