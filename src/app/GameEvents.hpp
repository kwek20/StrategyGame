#pragma once

#include "audio/AudioSystem.hpp"
#include "world/Entity.hpp"

namespace strategy {

struct AudioEvent {
    AudioCue cue;
};

struct EntityDamagedEvent {
    EntityId entity;
    EntityId attacker;
    float damage;
};

struct UnitProducedEvent {
    EntityId producer;
    EntityId unit;
    PlayerId owner;
};

struct ResourcesDepositedEvent {
    EntityId unit;
    PlayerId player;
    std::string resourceType;
    float amount;
};

} // namespace strategy
