# Strategy Game Development Roadmap

## Purpose

This roadmap translates the [game design](GAME_DESIGN.md) into an ordered implementation
plan. It tracks outcomes rather than dates. A milestone is complete only when its gameplay,
persistence, deterministic simulation, interface, diagnostics, and tests meet the listed exit
criteria.

## Current foundation

The prototype already provides:

- Seeded, chunked terrain with materials and level of detail
- Entity and component-based world state
- Units, buildings, resources, collision, and navigation
- Strategy selection and direct third-person unit control
- Gathering, production queues, upgrades, health, and combat foundations
- Per-player fog of war and visibility
- Country and specialization modifier foundations
- JSON configuration and versioned save/load
- State stack, event bus, dependency injection, and separated engine libraries
- Resource handles, manifests, asynchronous asset loading, materials, and shader management
- Renderer passes, UI framework, audio event foundations, logging, and diagnostics
- Fixed simulation ticks, serializable commands, deterministic random streams, and checksums

This foundation should be preserved while the placeholder medieval gameplay is replaced with
the modern drone, infrastructure, and power-grid loop.

## Completed definition foundation

The repository currently has:

- Central JSON definitions for units, buildings, resource nodes, resources, weapons, power devices,
  recipes, countries, specializations, match rules, and upgrades.
- Strict startup validation for IDs, references, tags, modifiers, localization keys, icons, recipes,
  generation settings, and upgrade metadata.
- Deterministic recipe-driven production with integer tick queues, typed player resources, and
  authoritative costs.
- Upgrade definitions with prerequisites, researcher lists, exclusive groups, and modifier data;
  completed levels resolve into permanent or producing-building modifier layers.
- Match setup, build palette, terrain resource generation, spawn placement, interaction margins,
  and upgrade targets moved out of simulation/rendering constants into definitions.
- Separate entity archetype and presentation IDs at the entity/save boundary, plus typed registry IDs.
- No legacy save migrations; entity runtime identity no longer uses `Entity::modelKey`.

Definitions, typed identity, recipe-backed upgrades, deterministic simulation boundaries,
persistence, diagnostics, and automated coverage are implemented without legacy migration paths.
The initial command hub, construction drone, materials, generator, relay, charging pad, extractor,
factory, and sensor-tower archetypes are present and validated.

## Development loop

Each content change should follow this loop:

1. Add or change a definition JSON entry.
2. Start the game or run definition tests; invalid references must fail with context.
3. Exercise the authoritative command in a fixed-tick simulation test.
4. Verify save/load and checksum behavior for the new authoritative state.
5. Verify renderer/UI consumption through presentation data without adding gameplay constants.
6. Run the complete CTest suite before moving to the next definition or gameplay system.

Current work continues with the construction-drone milestone:
drone definition → flight/battery components → gather/return commands → charger/power state →
save/checksum tests → HUD and presentation integration.

## Working rules

Every gameplay milestone follows these rules:

1. Authoritative behavior runs only in the fixed-tick simulation.
2. Player actions enter simulation as serializable commands.
3. New state is included in saves, replay checksums, and debug inspection.
4. Definitions and balance values come from data files rather than renderer or UI code.
5. Rendering and audio consume events or snapshots and never mutate simulation state.
6. Automated tests cover deterministic behavior before the next system is expanded.
7. Placeholder visuals are acceptable when they clearly communicate gameplay state.

## Milestone 0: stabilize the current prototype

### Goal

Establish a reliable baseline before replacing the starting worker and economy.

### Work

- Resolve unit congestion at shared destinations with arrival slots and local avoidance.
- Add navigation diagnostics: destination, waypoint, path length, retry state, and stuck time.
- Verify terrain exposure, fog, and material readability at all camera distances.
- Remove remaining gameplay constants from rendering and UI code.
- Document the authoritative component and command boundaries.
- Add a repeatable smoke-test match configuration.

### Exit criteria

- Groups can navigate around buildings and arrive without indefinite retry loops.
- A ten-minute smoke test produces no OpenGL errors, crashes, or severe frame spikes.
- Debug mode can explain why a selected unit is idle, moving, blocked, or retrying.
- All existing automated tests pass.

## Milestone 2: starting construction drone

### Goal

Replace the ground worker with the defining starting unit of the game.

### Work

- Add a flight-capable navigation mode with altitude and valid operating bounds.
- Add authoritative battery, cargo/gathering, construction, and repair components.
- Add commands for gather, construct, repair, recharge, stop, and direct control.
- Implement automatic return to an available charger at a configurable reserve level.
- Handle unreachable, destroyed, occupied, or disconnected charging destinations.
- Implement direct-control flight and a drone-specific HUD.
- Add readable battery, task, route, construction-power, and work-step indicators.

### Exit criteria

- Each player starts with exactly one command hub and one construction drone.
- The drone can gather materials into cargo, deposit them into the player's stockpile, construct
  after an upfront material cost, recharge, and resume work.
- Each construction recipe defines an upfront material cost, a power budget, and deterministic work
  steps; power is consumed per step while gathering continues to use drone cargo.
- Identical command streams produce identical battery, cargo, construction-power, movement, and
  resource results.
- Drone state survives save/load at every point in its work cycle.
- A drone cannot become permanently stranded without an explicit visible reason.

## Milestone 3: construction gameplay

### Goal

Allow the drone to establish a functional base through clear, deterministic construction.

### Work

- Add building placement commands and authoritative placement validation.
- Show terrain suitability, collision, resource cost, and power connectivity in previews.
- Add resource reservation, construction progress, drone work contribution, and cancellation.
- Player cancellation always refunds the full upfront material cost. Destroyed construction
  refunds nothing, disappears, and leaves its terrain deformation behind.
- Add planned, under-construction, operational, damaged, and destroyed states.
- Prevent unfinished structures from providing unintended full functionality.
- Add construction audio and interface events.

### Exit criteria

- A player can build all foundational structures using only the starting drone.
- Placement preview and simulation validation agree.
- Construction remains deterministic with multiple drones and simultaneous projects.
- Cancellation, destruction, save/load, and insufficient-resource cases are tested.

## Milestone 4: deterministic power grid

### Goal

Make connected energy infrastructure the main spatial constraint on expansion.

### Work

- Add generators, consumers, relays, connections, storage, transfer limits, and priorities.
- Build deterministic network discovery and allocation.
- Recalculate only affected network regions after topology changes.
- Define fully powered, underpowered, battery-powered, and offline behavior.
- Add graceful degradation for factories, sensors, chargers, and defenses.
- Add grid placement previews and a dedicated power overlay.
- Show supply, demand, storage, transfer, overload, and disconnected state.
- Emit simulation events for connection, disconnection, shortage, recovery, and shutdown.

### Exit criteria

- Destroying or rebuilding a relay deterministically divides or reconnects networks.
- Priority allocation is stable and understandable.
- Batteries bridge temporary shortages without creating or losing energy.
- Power state survives save/load and participates in world checksums.
- Large representative grids update within the simulation performance budget.

## Milestone 5: modern materials economy

### Goal

Create an enjoyable expansion loop using materials and power before increasing complexity.

### Work

- Replace placeholder wood, stone, and gold semantics with modern material deposits.
- Add loose gathering and powered extraction as distinct economic stages.
- Define carrying, storage, transfer, and spending rules.
- Give each player clear material income, consumption, and capacity information.
- Add resource deposit materials, icons, depletion states, and map indicators.
- Balance initial expansion, second-drone timing, generator timing, and first factory timing.

### Exit criteria

- Materials and power create at least two viable opening strategies.
- Players can understand why income or production has stopped.
- Deposit generation is deterministic and produces fair starting access.
- Resource state, reservations, and transfers survive save/load and replay.

## Milestone 6: first combat slice

### Goal

Create a complete short match with a small combined-arms roster.

### Work

- Add one infantry unit, one scout vehicle, and one combat drone.
- Define health, armor class, damage type, range, cooldown, accuracy, and targeting rules.
- Add attack, attack-move, guard, patrol, retreat, and hold-position commands as needed.
- Make the command hub and power infrastructure valid combat targets.
- Add repairs that consume materials and require an appropriate unit.
- Add unit-specific direct-control behavior and HUD information.
- Add clear projectiles, impacts, selection icons, health bars, and audio events.
- Implement command-hub destruction and match conclusion.

### Exit criteria

- Two players can complete a match from starting drones to victory.
- Every combat unit has a useful role and at least one meaningful counterplay option.
- Strategy control and direct control produce the same authoritative combat rules.
- Combat, destruction, repair, and victory are deterministic and persist correctly.

## Milestone 7: information warfare

### Goal

Make scouting and sensor infrastructure strategically important.

### Work

- Separate visual sight, radar detection, remembered terrain, and stale enemy contacts.
- Add the sensor tower and reconnaissance drone.
- Represent last-known enemy information without leaking current authoritative state.
- Add contact age and sensor-source diagnostics.
- Introduce basic radar signatures for personnel, vehicles, drones, buildings, and aircraft.
- Add an information overlay that remains readable alongside the power-grid overlay.

### Exit criteria

- Players can distinguish visible targets, radar contacts, stale contacts, and unknown space.
- Enemy entities cannot be selected or inspected through hidden information.
- Elevation and sensor types produce predictable detection outcomes.
- Visibility and contact updates remain deterministic and performant.

## Milestone 8: countries and specializations

### Goal

Validate the modular faction system with a small but strategically distinct selection.

### Work

- Implement two countries using shared units and infrastructure where appropriate.
- Implement at least two specializations per country.
- Add country and specialization selection to match setup.
- Add faction-specific icons, descriptions, modifiers, and upgrade choices.
- Ensure production speed resolves from the unit, producer, country, specialization, upgrades,
  and current power state.
- Add match-summary diagnostics for applied modifiers and economic outcomes.

### Exit criteria

- Each country supports a distinct strategy without a universally stronger economy.
- A unit produced in different buildings receives the correct production time.
- Modifier resolution is inspectable, deterministic, and covered by tests.
- Current-schema saves fail clearly when their version or authoritative fields are invalid; no legacy
  save migration is planned before public release.

## Milestone 9: logistics expansion

### Goal

Add components and fuel only after the foundational economy and combat loop are stable.

### Work

- Add components for advanced electronics, units, and upgrades.
- Add fuel for suitable vehicles and aircraft.
- Add production, storage, delivery, and consumption rules.
- Introduce cargo and logistics units where they create tactical choices.
- Add warnings and automation controls that prevent avoidable micromanagement.
- Rebalance existing recipes and operating costs.

### Exit criteria

- Components and fuel create new decisions rather than duplicate materials and power.
- Logistics failures are visible, recoverable, and strategically exploitable.
- Automation handles routine delivery while allowing deliberate player intervention.

## Milestone 10: advanced combined arms

### Goal

Expand the roster after the core match has proven stable.

### Work

- Add tanks, armored transports, mobile air defense, artillery, and support vehicles.
- Add aircraft production, launch, landing, fuel, repair, and rearming.
- Add electronic-warfare and counter-sensor capabilities.
- Add weapon restrictions and readable anti-air interactions.
- Extend direct control only where it adds a distinct experience.
- Improve formations, local avoidance, and command-group tools.

### Exit criteria

- Ground, air, drone, sensor, logistics, and power systems interact coherently.
- No single unit category invalidates the others.
- Strategy-mode automation remains sufficient for players who seldom use direct control.

## Milestone 11: networked 1v1 multiplayer

### Goal

Connect two players without weakening deterministic authority or diagnostics.

### Work

- Add transport-independent match sessions and player command streams.
- Define lobby, readiness, seed, faction, specialization, and version negotiation.
- Add command delay, buffering, acknowledgement, and disconnect handling.
- Exchange periodic checksums and capture desynchronization reports.
- Add replay recording using the same command stream.
- Define pause, surrender, reconnection, and match-result behavior.
- Add deterministic stress tests across separate processes.

### Exit criteria

- Two machines can complete a match from lobby to result.
- Equivalent clients maintain matching checksums throughout representative matches.
- A mismatch produces a useful diagnostic package rather than silently diverging.
- Replays reproduce the final authoritative checksum.

## Milestone 12: usability, balance, and content production

### Goal

Turn the complete systems into a polished and repeatable game experience.

### Work

- Establish a coherent modern material, texture, icon, animation, and audio direction.
- Complete tutorials, contextual help, tooltips, settings, accessibility, and key rebinding.
- Improve selection, command feedback, alerts, minimap, overlays, and match summary.
- Profile CPU, GPU, memory, asset loading, simulation, navigation, and network behavior.
- Build balance telemetry and repeatable AI or scripted test scenarios.
- Expand maps, factions, units, buildings, upgrades, effects, and sound only from validated needs.

### Exit criteria

- New players can complete a match without external instructions.
- Important state changes are communicated visually and audibly.
- Target hardware meets the agreed frame, simulation, loading, and memory budgets.
- Balance testing shows multiple viable openings and late-game compositions.

## Cross-cutting tracks

These tracks continue throughout all milestones rather than waiting for a final polish phase.

### Visual language

- Modern, readable silhouettes for units and buildings
- Consistent team-color masks
- Clear construction, damage, power, and sensor states
- Scalable icons for resources, commands, units, upgrades, and warnings
- Terrain materials that remain readable at strategy and direct-control distances

### Audio

- UI interaction feedback
- Unit acknowledgement and order feedback
- Construction, production, power, warning, combat, and destruction events
- Separate menu, ambient, interface, and effects controls
- Placeholder clips replaced without changing simulation code

### Diagnostics

- F3 inspection of authoritative and resolved component state
- Navigation, power-network, sensor, production, and modifier overlays
- Structured logs with rate limiting
- Frame, render-pass, simulation-system, loading, and network timings
- Replay and desynchronization capture

### Testing

- Unit tests for deterministic systems
- Save/load round trips for all authoritative components
- Replay checksum tests
- Invalid-data and missing-asset tests
- Hidden OpenGL integration tests
- Long-running match and performance scenarios

### Interface architecture

- The shared `UiDocument` path now covers menus, loading, editor controls, the in-game HUD,
  entity actions, queues, the minimap, and the power overlay.
- Responsive layout primitives provide anchors, padding, rows, columns, minimum sizes, and
  resolution-aware UI scaling. Reference-layout screens are fitted and centered within a safe area.
- Keyboard and gamepad focus navigation supports Tab/D-pad movement, Enter/Space/South
  activation, and Escape/East back behavior.
- The shared UI theme defines disabled states and minimum text contrast. Text is clipped with
  ellipses, and action tooltips use a consistent hover delay.

## Near-term execution order

The immediate order of work is:

1. Complete milestone 0 navigation and visual stabilization.
2. Implement the milestone 2 construction drone and battery loop.
3. Build milestone 3 construction around that drone.
4. Add the milestone 4 power grid.
5. Balance the milestone 5 materials-and-power economy.
6. Produce the milestone 6 first complete combat match.

Work outside this sequence should be limited to defects, architectural blockers, and reusable
assets needed by the active milestone.
