#include "audio/AudioSystem.hpp"

#include <SDL3/SDL_init.h>
#include <algorithm>
#include <cstring>
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
    {"possess_unit", AudioCue::possessUnit},
    {"gather_order", AudioCue::gatherOrder},
    {"delivery_complete", AudioCue::deliveryComplete},
    {"conversion_complete", AudioCue::conversionComplete},
    {"resource_warning", AudioCue::resourceWarning},
    {"cannon_fire", AudioCue::cannonFire},
    {"cannon_impact", AudioCue::cannonImpact},
    {"vehicle_destroyed", AudioCue::vehicleDestroyed}};
}
AudioSystem::~AudioSystem() {
    if (stream_)
        SDL_DestroyAudioStream(stream_);
    if (ownsAudioSubsystem_)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
void AudioSystem::load(const std::filesystem::path& manifest) {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    voices_.clear();
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
    const SDL_AudioSpec output{SDL_AUDIO_F32, 2, 48000};
    for (auto& asset : assets_) {
        if (asset.path.empty())
            continue;
        SDL_AudioSpec sourceSpec{};
        Uint8* source{};
        Uint32 sourceBytes{};
        Uint8* converted{};
        int convertedBytes{};
        const bool loaded =
            SDL_LoadWAV(asset.path.string().c_str(), &sourceSpec, &source, &sourceBytes) &&
            SDL_ConvertAudioSamples(&sourceSpec,
                                    source,
                                    static_cast<int>(sourceBytes),
                                    &output,
                                    &converted,
                                    &convertedBytes);
        if (loaded) {
            asset.samples = std::make_shared<std::vector<float>>(convertedBytes / sizeof(float));
            std::memcpy(asset.samples->data(), converted, convertedBytes);
        } else
            asset.state = ResourceState::failed;
        SDL_free(source);
        SDL_free(converted);
    }
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            return;
        ownsAudioSubsystem_ = true;
    }
    stream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &output,
        [](void* data, SDL_AudioStream* stream, int bytes, int) {
            static_cast<AudioSystem*>(data)->mix(stream, bytes);
        },
        this);
    if (stream_)
        (void)SDL_ResumeAudioStreamDevice(stream_);
}
void AudioSystem::mix(SDL_AudioStream* stream, int bytes) {
    if (bytes <= 0)
        return;
    std::vector<float> output((static_cast<std::size_t>(bytes) + sizeof(float) - 1) / sizeof(float),
                              0.F);
    {
        std::lock_guard lock(voicesMutex_);
        for (auto& voice : voices_) {
            const float volume = muted_.load()
                                     ? 0.F
                                     : masterVolume_.load() * (voice.music ? musicVolume_.load()
                                                                           : effectsVolume_.load());
            const auto count = std::min(output.size(), voice.samples->size() - voice.offset);
            for (std::size_t i = 0; i < count; ++i)
                output[i] += (*voice.samples)[voice.offset + i] * volume;
            voice.offset += count;
        }
        std::erase_if(voices_,
                      [](const auto& voice) { return voice.offset >= voice.samples->size(); });
    }
    for (auto& sample : output)
        sample = std::clamp(sample, -1.F, 1.F);
    mixedSoundSamples_ += static_cast<std::uint64_t>(std::count_if(
        output.begin(), output.end(), [](float sample) { return std::abs(sample) > .00001F; }));
    (void)SDL_PutAudioStreamData(
        stream, output.data(), static_cast<int>(output.size() * sizeof(float)));
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
    const auto& asset = assets_[audio.index];
    if (stream_ && asset.samples && !asset.samples->empty()) {
        std::lock_guard lock(voicesMutex_);
        if (voices_.size() >= 32)
            voices_.erase(voices_.begin());
        const auto cue = static_cast<AudioCue>(audio.index);
        voices_.push_back({asset.samples,
                           0,
                           cue == AudioCue::menuAmbient || cue == AudioCue::gameAmbient ||
                               cue == AudioCue::buildAmbient});
    }
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
    if (!audio || audio.index >= assets_.size() ||
        assets_[audio.index].generation != audio.generation)
        return ResourceState::invalid;
    return assets_[audio.index].state;
}
} // namespace strategy
