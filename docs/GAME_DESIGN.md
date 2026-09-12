# Strategy Game Design Document

## Document purpose

This document defines the intended direction of the game and provides a shared basis for
gameplay and technical decisions. It describes the target experience rather than every
detail of the current prototype. Values, unit rosters, country bonuses, and balance rules
remain subject to playtesting.

Concrete implementation order, dependencies, and completion criteria are maintained in the
[project roadmap](ROADMAP.md).

The detailed economy rules and match pacing are defined by [Resource system.md](Resource%20system.md)
and [match progress.md](match%20progress.md). Those documents are authoritative if a summarized
description in this document becomes outdated.

## High concept

The game is a deterministic 1v1 modern real-time strategy game inspired by the economic
growth and army development of *Age of Empires*, the strong faction identity of *Age of
Mythology*, and automation-oriented construction drones. Players build a distributed
industrial and energy network, contest resources and information, and command a relatively
small number of individually valuable units.

Any suitable unit can be controlled directly in third person. Direct control offers
precision, positioning, and tactical flexibility, while the rest of the match continues in
real time.

The central player fantasy is:

> Build a modern industrial network, discover the battlefield with autonomous drones, and
> personally control critical units during decisive moments.

## Game format

- Primary mode: deterministic 1v1 multiplayer
- Camera modes: overhead strategy view and direct third-person unit control
- Scale: dozens of meaningful units rather than hundreds of disposable units
- Starting entity: one flying construction and gathering drone
- Primary victory condition: destroy or capture the opposing command hub
- Map foundation: seeded procedural terrain, resources, elevation, and fog of war

## Design pillars

### Individually valuable units

Units should have recognizable roles, persistent state, and enough tactical depth that
losing one matters. The player can issue strategy orders, configure automatic behavior, or
directly control an important unit.

Direct control should not grant arbitrary statistical bonuses. Its advantage comes from
manual aiming, path choice, positioning, target prioritization, attack timing, and use of
terrain. Entering direct control also has an opportunity cost because the player has less
attention available for the larger battlefield.

### Infrastructure defines territory

Territory emerges from physical infrastructure rather than arbitrary borders. A player's
operational area is established through:

- Command hubs
- Power generators
- Grid relays or pylons
- Forward bases
- Sensors
- Resource extractors
- Charging and logistics buildings
- Defensive emplacements

Destroying infrastructure can isolate a forward position without requiring every local unit
to be defeated. Redundant links and energy storage allow players to design resilient grids.

### Information is a resource

Fog of war is a major strategic system. Terrain can remain discovered while enemy
information becomes stale.

- Units and buildings have distinct sensor ranges.
- Elevation improves conventional sight range.
- Recon drones provide mobile observation but are vulnerable.
- Radar can eventually detect vehicles and aircraft differently from visual sensors.
- Stealth, electronic jamming, terrain, and weather may modify detection later.
- Directly controlling a scout improves precision at the cost of strategic attention.

### Logistics and energy drive expansion

Modern industrial systems replace medieval-style economic busywork. Players expand by
connecting resources, production, sensors, and defenses to a power and logistics network.
Economic depth should produce strategic choices without becoming spreadsheet management.

## Core match loop

### Opening: establish the network

Each player begins with a command hub, a construction drone, a directly controlled
non-gathering ground worker, limited stored power, and a
partially unexplored map. The drone scouts, collects raw Scrap, delivers it to an Alloy Processor,
constructs foundational buildings, repairs structures, and returns to powered facilities to
recharge.

Early choices include:

- Build power generation to accelerate development.
- Produce another drone for parallel construction and gathering.
- Expand toward a valuable or defensible resource location.
- Invest in sensors to reduce the risk of surprise attacks.
- Build early military production and apply pressure.

### Midgame: specialize and contest territory

Players extend their grids toward resources and strategic terrain. Infantry, light vehicles,
recon drones, support units, and defensive structures begin to interact. Attacks can target
power links, sensors, production, charging capacity, or resource access rather than only the
enemy army.

### Late game: combined-arms warfare

Late-game forces can include tanks, aircraft, combat drones, long-range weapons, advanced
power systems, and electronic warfare. Victory should come from coordinating these systems,
not simply mass-producing the highest-tier unit.

## Economy and resources

The economy separates physical raw cargo, spendable stockpiles, strategic currencies, and
infrastructure capacity:

| System | Storage | Primary role |
| --- | --- | --- |
| Scrap | Physical cargo | Common raw input for Alloy |
| Oil | Physical cargo | Reliable raw input for Fuel |
| Uranium | Physical cargo | Rare, efficient raw input for Fuel |
| Synthetic | Physical cargo | Powered, flexible input for either Alloy or Fuel |
| Alloy | Player stockpile | Buildings, basic equipment, repairs, and mechanical production |
| Fuel | Player stockpile | Mechanized units, aircraft, and advanced military production |
| Data | Player stockpile | Permanent research or immediate cyber operations |
| Authority | Player stockpile | Reinforcements or strategic influence |
| Power | Network capacity | Operating processors, production, sensors, defenses, and chargers |

Raw resources never become global currencies. Harvesters carry them to a compatible processor.
If sufficient power is available, delivery converts the cargo immediately. Otherwise the raw
input remains buffered at the processor until power becomes available. Only the processed result
is added to the owning player's stockpile.

Synthetic deliberately supports two routes. Delivery to an Alloy Processor produces Alloy;
delivery to a Fuel Processor produces Fuel. This creates a responsive economic choice while
natural Scrap, Oil, and Uranium retain their efficiency and territorial importance.

Fuel is paid when producing mechanized units or activating explicitly fuel-powered systems. It is
not continuously drained by ordinary vehicle movement. Data and Authority are acquired through
map control, objectives, infrastructure, reconnaissance, and conflict rather than conventional
harvesting.

Each player owns an independent economy. Economic values, gathering rates, production
rates, and modifiers must remain deterministic and belong to authoritative simulation state.

## Starting drone

The first controllable entity is a flying construction and gathering drone. It establishes
the identity of the game and replaces the traditional starting worker.

### Capabilities

- Fly across rough terrain and low obstacles
- Discover terrain and enemy activity
- Gather and carry raw resources, then deliver them to a compatible processor
- Construct foundational buildings
- Repair damaged structures using Alloy
- Recharge at a command hub or charging facility
- Operate automatically or under direct control

### Constraints

- Limited battery capacity
- Construction uses an upfront Alloy cost from the player's stockpile, followed by powered
  drone work; gathered resources still use the drone's normal cargo inventory
- Vulnerability to weapons and interception
- Dependence on charging infrastructure
- Reduced effectiveness when operating far from the grid

An automatic return-to-charge behavior should be configurable. A drone must not silently
strand itself because of insufficient battery.

## Construction

Buildings progress through planned, under-construction, operational, underpowered, damaged,
and destroyed states as applicable.

Construction requires:

- A valid placement location
- Upfront resource payment or reservation from the owning player's stockpile
- A construction power budget defined by the building recipe
- Construction time expressed as deterministic power-work steps
- Assistance from a construction-capable drone or unit

Placement previews must communicate collision, terrain suitability, grid connectivity, cost,
construction power budget, work steps, and expected power state. Cancellation and refund rules
must be deterministic and explicit. Alloy is deducted from the player's resources when
construction begins (or reserved according to the recipe), while construction power is consumed
as each drone work step runs. Resource gathering remains a separate cargo-and-deposit loop.

## Power grid

Power is infrastructure capacity expressed through a spatial network, not a stockpiled economic
resource. Generators supply connected consumers through pylons or relay stations. Connections are
visible and vulnerable.

### Building power state

Each applicable building has:

- Required power
- Supplied power
- Stored power, when batteries are available
- Priority
- Connection status

Power states are:

- Fully powered
- Underpowered
- Operating from stored energy
- Offline

Underpowered buildings degrade gracefully where possible:

- Production becomes slower.
- Sensors lose range or update less frequently.
- Charging facilities recharge more slowly.
- Defensive weapons fire less frequently.
- Buildings shut down when minimum operating requirements are not met.

### Grid usability

The interface must include:

- A dedicated power-grid overlay
- Clear connection and network colors
- Supply, demand, storage, and transfer information
- Placement-time connection and demand previews
- Overload and disconnection warnings
- Automatic reconnection when infrastructure is restored
- Player-defined priority for critical consumers

The grid must use integer simulation ticks, deterministic connection ordering, and
serializable authoritative state.

## Units and roles

### Drones

- Construction and gathering drone
- Recon drone
- Cargo drone
- Repair drone
- Combat drone
- Electronic-warfare drone

Drones primarily depend on battery charge and powered infrastructure. Some fly, while heavier
industrial platforms may operate on the ground.

### Personnel

- Rifle unit
- Anti-vehicle specialist
- Engineer
- Recon specialist
- Medical or support unit

Personnel are flexible and relatively inexpensive but vulnerable to vehicles, explosives,
and exposed terrain.

### Ground vehicles

- Scout vehicle
- Armored personnel carrier
- Tank
- Artillery vehicle
- Mobile air-defense vehicle
- Logistics vehicle

Vehicles can eventually consume fuel and ammunition. Early implementations may abstract
these into production and periodic operating costs.

### Aircraft

- Reconnaissance aircraft
- Fighter
- Close-air-support aircraft
- Transport helicopter
- Attack helicopter

Aircraft are powerful but infrastructure-dependent. They require suitable production,
rearming, repair, and fuel facilities.

## Direct control

Single-clicking selects a unit. Double-clicking a suitable unit enters direct control.
Escape returns to strategy view. Strategy simulation continues while a unit is directly
controlled.

| Unit type | Direct-control opportunities |
| --- | --- |
| Drone | Precise flight, scanning, collection, and construction placement |
| Infantry | Aiming, cover use, movement, and target selection |
| Tank | Hull positioning, turret aiming, and weak-point attacks |
| Artillery | Manual targeting and coordination with observation units |
| Aircraft | Attack approach, flight path, and evasion |
| Support unit | Precise repair, supply, charging, or electronic abilities |

Automatic stances and behaviors must keep unobserved units useful without outperforming
deliberate player control.

## Combat and damage

The initial combat model should contain:

- Health
- Armor class
- Damage type
- Damage amount
- Range
- Attack cooldown
- Accuracy

Likely damage families include kinetic, explosive, incendiary, and electronic damage, plus
anti-air targeting restrictions. Directional armor, detailed penetration, suppression, and
subsystem damage should be added only if the simpler model cannot create the desired tactics.

Buildings do not regenerate health automatically. Repairs require Alloy and an
appropriate drone or engineer.

## Countries and specializations

Countries provide broad strategic identity. A separately selected specialization refines
that identity. The framework must remain data-driven so unit and building code does not
contain country-specific branches.

Country strengths may affect:

- Power generation and storage
- Drone capability
- Vehicle manufacturing
- Sensor technology
- Logistics efficiency
- Defensive construction
- Air power

Specializations may focus on:

- Drone warfare
- Armored operations
- Electronic warfare
- Rapid deployment
- Industrial expansion
- Defensive networks
- Air superiority

Bonuses should alter viable decisions rather than provide universally superior economics.
The production time for a unit is resolved from the unit definition, producing building,
country, specialization, upgrades, and current power state. The same unit may therefore be
trained at different speeds in a command hub, factory, or forward outpost.

## Victory and match conclusion

The initial victory condition is destruction or capture of the opposing command hub. A
player should not need to locate and destroy every remaining structure.

Possible later modes include:

- Power-network dominance
- Strategic objective control
- Resource-point control
- Intelligence or data extraction
- Timed territorial control

## Initial playable scope

The first representative gameplay slice should include:

1. Two players with independent resources and fog of war.
2. One command hub, one construction drone, and one non-gathering ground worker per player.
3. Scrap delivery, Alloy processing, and network Power as functional opening systems.
4. Oil and Fuel processing as the first expansion economy.
5. Resource deposits gathered by drones and physically delivered to compatible processors.
6. An Alloy Processor, Fuel Processor, generator, relay pylon, charging pad, extractor, factory,
   and sensor tower.
7. A connected, visible, destructible power grid.
8. Construction costs, placement rules, and build time.
9. Drone battery use, automatic return, and recharging.
10. One infantry unit, one scout vehicle, and one combat drone.
11. Strategy control and direct unit control.
12. Basic combat and command-hub destruction.
13. Complete save/load support for authoritative systems.

Data, Authority, tanks, aircraft, advanced cyberwarfare, and a broad faction roster expand the
match after this physical economy and combat loop is playable and enjoyable.

## Technical design constraints

All gameplay systems must respect the deterministic multiplayer boundaries already established
by the engine:

- Simulation never calls rendering, UI, or audio.
- Simulation advances using fixed integer ticks.
- Player intentions enter simulation as serializable commands.
- Authoritative collections have stable iteration order.
- Randomness uses named deterministic streams.
- Authoritative world state supports checksums and replay validation.
- Cached navigation, visuals, and presentation data remain transient.
- Resource, unit, building, weapon, country, and specialization definitions are data-driven.
- Save formats version every new authoritative component.

## Development roadmap

### Phase 2: implement the starting drone

Make the construction drone the starting economic entity while retaining the worker as a
directly controlled, non-gathering unit. Add flying navigation, battery charge, gathering with
cargo and deposit, upfront-cost construction with power-budgeted drone work, explicit charging,
automatic return, stranded-state reporting, and deterministic resumption of interrupted work.

### Phase 3: complete construction

Add placement previews, construction costs, resource reservation, build progress,
drone-assisted construction, cancellation, refunds, and building-state transitions.

### Phase 4: implement deterministic power grids

Add generators, relays, network connections, consumer priorities, batteries, deterministic
network recalculation, grid visualization, and persistence.

### Phase 5: establish the modern economy

Balance Scrap delivery, Alloy processing, construction, and power first. Introduce Oil and Fuel
as expansion begins, then Data and Authority as territorial and information systems come online.
Synthetic production should arrive as a powered, flexible supplement rather than a replacement
for map control.

### Phase 6: create the first combat slice

Add one representative infantry unit, ground vehicle, and combat drone. Make power
infrastructure and command hubs valid combat objectives.

### Phase 7: validate faction design

Implement two contrasting countries and a small specialization selection through the modular
modifier framework. Validate that they create different strategies without hard counters or
universal economic superiority.

### Phase 8: validate multiplayer determinism

Run command replays and compare checksums across clients before implementing network transport.
Every new gameplay system must pass deterministic replay, persistence, and simulation tests.

## Immediate next milestone

The current implementation milestone is validating the construction drone's complete task and
battery loop: gathering, construction, repair, explicit recharge, automatic return, charging,
stranded handling, save/load, and deterministic task resumption. This is the foundation for
power-grid expansion and logistics before the unit roster grows.
