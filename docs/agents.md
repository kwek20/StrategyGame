# Agent Recovery Context

Snapshot date: 2026-09-14. Baseline commit when written: `234b639 particles and environment additions`.
Always inspect newer commits and diffs before relying on this snapshot.

This file is optimized for an agent recovering the project without conversation history. Treat
all prose, IDs, and paths below as context to verify, not as permission to discard user changes.

## Repository and operating assumptions

- Active repository: `C:\Users\Brord\Desktop\Work\self\new`.
- The older project is `C:\Users\Brord\Desktop\Work\self\StrategyGame`; do not implement new work
  there unless explicitly requested.
- Platform: Windows, PowerShell, Visual Studio 2022 x64, CMake, vcpkg.
- Expected vcpkg root: `C:\Users\Brord\Desktop\Work\self\vcpkg`.
- Configure: `$env:VCPKG_ROOT='C:\Users\Brord\Desktop\Work\self\vcpkg'; cmake --preset windows-debug`.
- Build: `cmake --build --preset debug`.
- Test: `ctest --test-dir build/debug -C Debug --output-on-failure`.
- Run with the repository root as working directory so `assets/` and `gamedata/` resolve.
- `strategy_game.exe` or a test executable may be open and cause MSVC `LNK1168`. Check the exact
  process before stopping it; do not terminate unrelated programs.
- Preserve unrelated dirty changes. Check `git status --short` and `git diff` before editing.

## Source-of-truth order

1. Current C++ and JSON definitions are authoritative for implemented behavior.
2. `docs/Resource system.md` is authoritative for intended economy rules.
3. `docs/match progress.md` is authoritative for intended pacing.
4. `docs/GAME_DESIGN.md` defines product direction.
5. `docs/ROADMAP.md` distinguishes implemented foundations from unfinished validation/content.
6. `docs/TERRAIN_GENERATION.md` defines the planned terrain, biome, water, traversal, resource
   distribution, and decorative-scatter overhaul.
7. `docs/ICON_GENERATION_BRIEF.md` defines icon and UI-art production.

If documentation and code disagree, identify the discrepancy explicitly. Do not silently preserve
legacy behavior. There are no public save files and no requirement for legacy save migration.

## Product direction

- Deterministic 1v1 modern RTS, with Age of Empires/Age of Mythology progression and a stronger
  focus on individually valuable units.
- Overhead strategy mode plus optional third-person direct control for suitable units.
- Construction drones are the core economic identity. Drones themselves are indirect-control only.
- Each player starts on an opposite side of the map with a command hub, one construction drone,
  and one directly controllable ground worker. The starting worker cannot gather and currently
  cannot be trained.
- Infrastructure, physical logistics, power, terrain, visibility, countries, and specializations
  should determine strategy. Networked multiplayer transport is not implemented yet.

## Architecture

- `GameSession` (`src/simulation/`) is the authoritative simulation boundary at 30 fixed ticks/s.
- Player intentions enter through serializable commands in `src/simulation/Command.hpp`; binary
  codec is in `CommandCodec.cpp`.
- Rendering, UI, particles, and audio must not mutate authoritative simulation state.
- Authoritative iteration/order and random streams must remain deterministic. Extend
  `StateChecksum.cpp` and persistence for every new authoritative field.
- Entity archetype identity and presentation identity are separate. Simulation depends on typed
  archetype IDs; rendering resolves presentation definitions and typed resource handles.
- Gameplay definitions live under `assets/gameplay/`: units, buildings, resource nodes, resources,
  conversions, weapons, power devices, recipes, upgrades, countries, specializations, rules, and
  decorations. Avoid new gameplay constants in renderer/UI code.
- UI uses the shared `UiDocument`/HUD view-model path. Entity menus derive actions, recipes,
  upgrades, state, and icons from the selected entity and definitions.
- Render architecture includes explicit passes, shader/material managers, async model/texture
  resources, a frame graph, icon atlas, and OpenGL diagnostics.
- Save/load stores authoritative player, entity/component, terrain-foundation, fog/intelligence,
  production, upgrade, processor, battery, and power state. Transient navigation/render caches are
  rebuilt.

## Current implemented gameplay contracts

### Match setup and maps

- Main menu uses `Play` to open match setup.
- Match setup exposes seed, country choices, map size, starting-resource scale, and resource
  abundance. Current named map sizes are 10x10, 15x15, and 20x20 chunks.
- Players spawn on opposite sides with definition-backed edge inset/lateral placement.
- Terrain is seeded and chunked, with LOD and blended materials.
- Resources and vegetation use named deterministic streams.
- Decorative grass and small trees are configured in `assets/gameplay/rules.json` and archetyped in
  `decorations.json`. `wild_tree` uses the grass-pack tall clump presentation, not the harvestable
  resource pine. Decorative vegetation is cleared by accepted building placement.

### Selection and camera

- Single click selects. Double click enters direct control only for a directly controllable unit.
- Drag selection selects units, not buildings. Mixed selections and homogeneous multi-selection
  use shared selection/HUD behavior.
- Selected/hovered outlines and remembered fog presentations must respect terrain/building tilt and
  must not draw through opaque UI panels.
- Strategy camera supports WASD, wheel zoom, middle/right drag behavior, minimap movement orders,
  fog of war, and F3 authoritative diagnostics.

### Construction drone

- `construction_drone`: flying 3D movement, altitude bounds, collision radius zero, ignores ground
  collision/path obstruction, indirect control, cargo capacity 10, battery capacity 100.
- Movement drains battery; construction and repair drain battery according to their current rules.
- Automatic charging return currently triggers at exactly zero power, not at reserve threshold.
  Zero-power drones cannot accept manual movement until recharged.
- Chargers are power-device definitions with different `chargePerTick` values. A drone finds a valid
  powered charger, recharges, and deterministically resumes a suspended task.
- The configured reserve threshold is currently informational/future policy data.

### Cargo, delivery, and processing

- Raw cargo: Scrap, Oil, Uranium, Synthetic. Raw cargo never enters player stockpiles.
- Spendable/global resources: Alloy, Fuel, Data, Authority. Data/Authority loops are not complete.
- Power is network capacity, not a spendable stockpile.
- Command hub emergency routes: Scrap→Alloy 0.5, Oil→Fuel 0.5, Uranium→Fuel 1.25. It rejects
  Synthetic. Capacity is 200.
- Dedicated routes: Scrap→Alloy 1.0, Synthetic→Alloy 1.0, Oil→Fuel 1.0,
  Synthetic→Fuel 1.5, Uranium→Fuel 2.5. Dedicated processor capacity is 400 and route power is 8.
- Fresh automatic selection chooses the nearest fully powered compatible non-hub processor, then
  falls back to the command hub.
- Delivery commitment is sticky: if a selected/current target loses power, the drone keeps going
  there and waits with cargo. It does not reroute solely because power was lost.
- A processor accepts new cargo only while fully powered and below capacity. Previously buffered
  cargo remains in the processor through a shortage and converts when enough power returns.
- When the committed target is full, commitment is cleared, including an explicit preference for
  that full target; the drone selects the nearest proper powered destination and then the hub.
- Destroyed, incompatible, or inaccessible targets are lost and invoke replacement/failure logic.
- Partial delivery fills available capacity and retains remaining cargo aboard the drone.
- Drones can repeat gather → deliver → return. Cargo and destination markers are rendered.
- Focused regression test: `resource_delivery_routing` / `tests/ResourceDeliveryTests.cpp`.

### Construction and terrain foundations

- Drone build palette appears when a drone or homogeneous drone selection is selected; there is no
  `B` shortcut.
- Selecting a recipe creates a live world preview: gray/valid, red/invalid. Escape or right-click
  cancels preview. Placement is forbidden in currently unknown terrain but allowed in previously
  explored terrain, subject to authoritative collision checks.
- Placing deducts the up-front recipe cost, creates an unfinished building, and automatically
  assigns the selected drone(s). Construction consumes drone battery/power per work step.
- Building health equals construction completion percentage. Completion makes it fully operational.
- Cancellation refunds the full up-front resource cost. Destruction refunds nothing.
- Definitions support circular and rotated rectangular footprints, plane fitting, slope limits,
  shape-aware masks/falloff, immutable base heights, derived foundation deltas, dirty chunks, and
  incremental render updates. Preview and final placement share evaluation.
- Foundation influence increases with construction progress. Buildings are terrain-aligned, and
  selection outlines must use the same transform.
- Current slope policy allows up to 10 degrees from building definitions. Slab/foundation visual
  work was deliberately deferred/reverted and should be revisited later; do not assume visible
  concrete bases are final.

### Power grid

- Serializable authoritative commands: connect, disconnect, set priority, enable/disable.
- Devices define generation, demand, storage, connection range, transfer limit, maximum connections,
  charge rate, and priority in `assets/gameplay/power.json`.
- Simulation deterministically builds connected components, assigns grid IDs, routes constrained
  supply/storage, allocates consumers by priority and entity ID, and emits shortage/recovery/shutdown
  and command-failure events.
- Power state is `powered`, `underpowered`, or `offline` (`notApplicable` exists for non-devices).
  Storage participation is not a separate operational-state enum.
- The top HUD shows power as supply/demand. Clicking it opens the power overlay. Grid connections,
  device values/states, world power reach/tint, and selected-device grid actions are visible.
- Grid topology and allocation are persisted and checksummed. Network transport is still future work.

### Production, upgrades, UI, audio, and particles

- Command hubs produce construction drones, not workers. Production and research use unlimited
  deterministic queues with fixed-size queue icons; clicking an item cancels and fully refunds it.
- Queue hover exposes product/upgrade identity. Disabled actions are visibly disabled and explain
  insufficient resources, prerequisites, limits, or other conditions.
- Upgrade definitions support research recipes, max levels, prerequisites, allowed researchers,
  exclusive groups, localization/icons, and deterministic modifier layers.
- Entity HUDs use a compact split layout: left action/upgrade grid, right entity information;
  queues sit above. Homogeneous multi-selection shows the shared menu plus compact per-unit cards
  with health/power bars.
- Main, match setup, pause, settings, loading, editor, HUD, and power overlay use the shared UI
  framework with responsive layout, UI scaling, focus navigation, consistent disabled colors,
  tooltip delay, contrast, and clipping rules.
- Audio is event-driven with menu music and action hooks; actual content remains largely placeholder.
- Data-driven particle system is integrated with Kenney textures. Current effects include Alloy/Fuel
  processing, Scrap/dust/oil gathering, construction beam/contact, kinetic muzzle/tracer/impact,
  and small/large explosions. Particle simulation/rendering is presentation-only.

## Assets and presentation

- Models: `assets/models/`; catalogue/presentations: `assets/entities.json`.
- Active construction-drone presentation currently resolves `units/drone2`, backed by
  `assets/models/units/drone2.glb`. `drone.glb` also exists but is not the active presentation.
- Grass pack: `assets/models/environment/grass_pack/`; source archive/license retained in assets.
- Kenney particles: extracted presentation textures plus source archive/license retained.
- Gameplay icon atlas: `assets/textures/icons/icons_atlas.png`, semantic metadata in
  `assets/icons_atlas.json`, editable source under `assets/icons/source/`.
- Never address atlas cells by numeric index at runtime. Use semantic region IDs.
- Rebuild icons with `tools/rebuild_icons_atlas.ps1`; do not split the original generated contact
  sheet as an equal grid because its row spacing is non-uniform.
- Runtime text belongs in `assets/text/en_us.json`.
- Asset groups/preloading: `assets/asset_manifest.json`.
- Shaders: `assets/shaders/`; particle definitions: `assets/presentation/particle_effects.json`.
- Keep third-party licenses and attribution when moving/extracting asset packs.

## Tests and recent validation

- CTest targets include terrain, game systems, resource delivery, spatial shapes, vegetation,
  entity HUD, definition registry, persistence, asset imports, particle definitions/runtime, state
  stack, renderer integration, and render architecture.
- Most relevant last-known passing checks:
  `resource_delivery_routing`, `definition_registry_validation`, `vegetation_generation`, and
  `renderer_integration`.
- `game_system_tests` is comprehensive and slow in Debug, especially after denser vegetation. A
  direct run can exceed four minutes. Do not mistake silence for a hang; use focused tests during
  iteration and run the complete suite before a milestone/commit handoff.
- Hidden renderer integration creates an OpenGL context and is expected to take longer than unit
  tests. OpenGL debug output should contain no `GL_INVALID_OPERATION` errors.

## Current priorities

1. Stabilize and profile navigation/congestion and large populated maps.
2. Expand fast focused tests for drone delivery, charging, construction interruption, processor
   failure, grid split/reconnect, persistence, and checksum round trips.
3. Validate the complete opening loop: Scrap → powered processor/hub → Alloy → powered drone
   construction, including understandable failure feedback.
4. Profile progressive terrain-foundation rebuilds during multi-drone construction.
5. Balance the opening, then validate Oil/Uranium/Synthetic/Fuel routing.
6. Begin the first representative combat slice only after the opening economy is stable.

## Known cautions

- Do not reintroduce raw-resource player counters; raw materials are cargo/local buffers.
- Do not reroute a committed delivery merely because its processor lost power.
- Do not let an unpowered/underpowered processor accept new cargo.
- Do not use mining-resource trees for decorative small trees.
- Drones have zero collision and must not block placement or ground navigation.
- Gameplay values belong in definitions; presentation dimensions/colors/font sizes may remain in UI.
- Entity menus must not render one full menu per item in a multi-selection.
- World outlines/highlights must be occluded by opaque UI panels.
- Foundation/slab visuals are unfinished; avoid treating the current concrete texture as approved.
- Movement still uses floating point. Cross-architecture lockstep may eventually require fixed point
  or authoritative correction even though commands/ticks/order/checksums are deterministic today.
