#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
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
    possessUnit
};
class AudioSystem final {
  public:
    AudioSystem() = default;
    void load(const std::filesystem::path& manifest = "assets/audio/audio.json");
    void setMasterVolume(float volume);
    void setMusicVolume(float volume);
    void setEffectsVolume(float volume);
    void setMuted(bool muted);
    void play(AudioCue cue);
    void setAmbient(AudioCue cue);
    [[nodiscard]] float masterVolume() const {
        return masterVolume_;
    }
    [[nodiscard]] float musicVolume() const { return musicVolume_; }
    [[nodiscard]] float effectsVolume() const { return effectsVolume_; }
    [[nodiscard]] bool muted() const { return muted_; }
    [[nodiscard]] std::uint64_t emittedCueCount() const {
        return emittedCueCount_;
    }

  private:
    float masterVolume_{1.0F};
    float musicVolume_{0.8F};
    float effectsVolume_{1.0F};
    bool muted_{false};
    std::unordered_map<AudioCue, std::filesystem::path> assets_;
    AudioCue ambient_{AudioCue::menuAmbient};
    std::uint64_t emittedCueCount_{0};
};
} // namespace strategy
