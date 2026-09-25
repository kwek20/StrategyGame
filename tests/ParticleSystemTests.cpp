#include "assets/ParticleEffectDefinitions.hpp"
#include "particles/ParticleSystem.hpp"

#include <cmath>
#include <iostream>

namespace {
bool close(float lhs, float rhs) { return std::abs(lhs - rhs) < 0.00001F; }
}

int strategyTestMain() {
    const strategy::ParticleEffectCatalogue catalogue =
        strategy::ParticleEffectCatalogue::load();
    const auto burst = catalogue.handle(strategy::ParticleEffectId{"weapon.muzzle_kinetic"});
    strategy::ParticleSystem first(catalogue, 128, 8);
    strategy::ParticleSystem second(catalogue, 128, 8);
    const strategy::ParticleEmitterDesc description{burst, {2, 3, 4}, {0, 1, 0}, 741U};
    const auto firstHandle = first.emit(description);
    const auto secondHandle = second.emit(description);
    first.update(1.0F / 60.0F);
    second.update(1.0F / 60.0F);

    bool valid = firstHandle && secondHandle && first.particles().size() == 5 &&
                 first.particles().size() == second.particles().size();
    for (std::size_t index = 0; valid && index < first.particles().size(); ++index) {
        const auto& lhs = first.particles()[index];
        const auto& rhs = second.particles()[index];
        valid = close(lhs.position.x, rhs.position.x) && close(lhs.position.y, rhs.position.y) &&
                close(lhs.position.z, rhs.position.z) && close(lhs.lifetimeSeconds, rhs.lifetimeSeconds);
    }

    const auto continuous = catalogue.handle(strategy::ParticleEffectId{"processing.alloy"});
    strategy::ParticleSystem limited(catalogue, 4, 1);
    const auto continuousHandle = limited.emit({continuous, {}, {0, 1, 0}, 9U});
    limited.update(0.1F);
    limited.update(0.1F);
    valid = valid && continuousHandle && limited.particles().size() <= 4;
    limited.stop(continuousHandle, true);
    valid = valid && limited.particles().empty();

    if (!valid) std::cerr << "Particle runtime lifecycle or determinism failed\n";
    return valid ? 0 : 1;
}
