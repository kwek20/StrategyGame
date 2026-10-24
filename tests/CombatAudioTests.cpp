#include "audio/AudioSystem.hpp"

#include <SDL3/SDL.h>
#include <chrono>
#include <stdexcept>
#include <thread>

int strategyTestMain() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    strategy::AudioSystem audio;
    audio.load();
    for (auto cue : {strategy::AudioCue::cannonFire,
                     strategy::AudioCue::cannonImpact,
                     strategy::AudioCue::vehicleDestroyed}) {
        if (audio.state(audio.handle(cue)) != strategy::ResourceState::ready)
            throw std::runtime_error("Combat WAV failed to load/convert");
        audio.play(cue);
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (audio.mixedSoundSamples() == 0 && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (audio.emittedCueCount() != 3 || audio.mixedSoundSamples() == 0)
        throw std::runtime_error("Combat cues must produce real mixed PCM samples");
    return 0;
}
