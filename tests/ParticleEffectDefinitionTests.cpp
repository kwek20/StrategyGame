#include "assets/ParticleEffectDefinitions.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() {
    bool valid = true;
    const strategy::ParticleEffectCatalogue catalogue =
        strategy::ParticleEffectCatalogue::load();
    valid = valid && catalogue.size() == 12;

    const strategy::ParticleEffectId alloyId{"processing.alloy"};
    const strategy::ParticleEffectHandle alloyHandle = catalogue.handle(alloyId);
    const auto* alloy = catalogue.effect(alloyHandle);
    valid = valid && alloy &&
            alloy->id == alloyId && catalogue.id(alloyHandle) == alloyId &&
            alloy->emission.mode == strategy::ParticleEmissionMode::continuous &&
            alloy->render.blend == strategy::ParticleBlendMode::additive &&
            alloy->render.texture ==
                strategy::ParticleTextureId{"particles/kenney/spark"} &&
            alloy->emission.maximumParticles == 96;
    const auto* explosion = catalogue.effect(strategy::ParticleEffectId{"explosion.large"});
    valid = valid && explosion &&
            explosion->emission.mode == strategy::ParticleEmissionMode::burst &&
            explosion->emission.burstCount == 120 &&
            explosion->render.softParticles;
    const auto* construction =
        catalogue.effect(strategy::ParticleEffectId{"construction.contact"});
    const auto* miningDust = catalogue.effect(strategy::ParticleEffectId{"mining.dust"});
    valid = valid && construction && miningDust &&
            construction->emission.ratePerSecond == 36.0F &&
            miningDust->render.texture ==
                strategy::ParticleTextureId{"particles/kenney/dust"};
    valid = valid && !catalogue.handle(strategy::ParticleEffectId{"missing.effect"}) &&
            catalogue.effect(strategy::ParticleEffectHandle{999, 1}) == nullptr &&
            catalogue.effect(strategy::ParticleEffectHandle{alloyHandle.index,
                                                             alloyHandle.generation + 1}) == nullptr;

    const std::filesystem::path invalidPath =
        std::filesystem::temp_directory_path() / "invalid-particle-effects.json";
    {
        std::ofstream output(invalidPath, std::ios::trunc);
        output << R"({"version":1,"effects":{"broken":{"quality":"medium","maximumDistance":100,"emission":{"mode":"burst","burstCount":0,"maximumParticles":8},"spawn":{"shape":"point","extents":[0,0,0]},"particle":{"lifetimeSeconds":[1,2],"speed":[0,1],"startSize":[1,1],"endSize":[1,1],"gravity":[0,0,0],"startColor":[1,1,1,1],"endColor":[1,1,1,0]},"render":{"texture":"particles/test","billboard":"camera","blend":"alpha"}}}})";
    }
    try {
        (void)strategy::ParticleEffectCatalogue::load(invalidPath);
        valid = false;
    } catch (const std::runtime_error& error) {
        valid = valid && std::string{error.what()}.find("burstCount") != std::string::npos;
    }
    std::filesystem::remove(invalidPath);

    if (!valid) std::cerr << "Particle effect definition validation failed\n";
    return valid ? 0 : 1;
}
