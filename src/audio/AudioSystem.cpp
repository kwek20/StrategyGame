#include "audio/AudioSystem.hpp"

#include <algorithm>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
namespace strategy {
namespace {
const std::pair<const char*, AudioCue> cueNames[] = {
    {"ui_click", AudioCue::uiClick},
    {"menu_ambient", AudioCue::menuAmbient},
    {"game_ambient", AudioCue::gameAmbient},
    {"build_ambient", AudioCue::buildAmbient},
    {"selection", AudioCue::selection},
    {"move_order", AudioCue::moveOrder},
    {"building_open", AudioCue::buildingOpen},
    {"building_upgrade", AudioCue::buildingUpgrade},
    {"training_upgrade", AudioCue::trainingUpgrade},
    {"train_unit", AudioCue::trainUnit},
    {"save_game", AudioCue::saveGame},
    {"load_game", AudioCue::loadGame},
    {"possess_unit", AudioCue::possessUnit}};
}
void AudioSystem::load(const std::filesystem::path& manifest) {
    assets_.assign(static_cast<std::size_t>(AudioCue::count), AudioSlot{});
    std::ifstream stream(manifest);
    if (!stream)
        return;
    rapidjson::IStreamWrapper input(stream);
    rapidjson::Document document;
    document.ParseStream(input);
    if (document.HasParseError() || !document.IsObject() || !document.HasMember("cues") ||
        !document["cues"].IsObject())
        return;
    for (const auto& [name, cue] : cueNames)
        if (document["cues"].HasMember(name) && document["cues"][name].IsString()) {
            const std::string path = document["cues"][name].GetString();
            if (!path.empty())
                assets_[static_cast<std::size_t>(cue)].path = manifest.parent_path() / path;
        }
}
void AudioSystem::setMasterVolume(float volume) {
    masterVolume_ = std::clamp(volume, 0.0F, 1.0F);
}
void AudioSystem::setMusicVolume(float volume) {
    musicVolume_ = std::clamp(volume, 0.0F, 1.0F);
}
void AudioSystem::setEffectsVolume(float volume) {
    effectsVolume_ = std::clamp(volume, 0.0F, 1.0F);
}
void AudioSystem::setMuted(bool muted) {
    muted_ = muted;
}
void AudioSystem::play(AudioCue cue) {
    play(handle(cue));
}
void AudioSystem::play(AudioHandle audio) {
    if (state(audio) == ResourceState::invalid || state(audio) == ResourceState::failed)
        return;
    ++emittedCueCount_;
    (void)audio; /* Playback backend intentionally accepts empty cue assets. */
}
void AudioSystem::setAmbient(AudioCue cue) {
    if (ambient_ == cue)
        return;
    ambient_ = cue;
    play(cue);
}
AudioHandle AudioSystem::handle(AudioCue cue) const {
    const auto index = static_cast<std::uint32_t>(cue);
    return index < assets_.size() ? AudioHandle{index, assets_[index].generation} : AudioHandle{};
}
ResourceState AudioSystem::state(AudioHandle audio) const {
    if (!audio || audio.index >= assets_.size() || assets_[audio.index].generation != audio.generation)
        return ResourceState::invalid;
    return assets_[audio.index].state;
}
} // namespace strategy
