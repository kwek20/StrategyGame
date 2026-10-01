# Agent Recovery Context

Snapshot updated: 2026-09-27. Always inspect the current commit and working-tree diff.
Always inspect newer commits and diffs before relying on this snapshot.

This file is optimized for an agent recovering the project without conversation history. Treat
all prose, IDs, and paths below as context to verify, not as permission to discard user changes.

## 2026-09-27 power redesign

Current HEAD at this update: `376cd4a` (`power overlay and drone`). The older checkpoint below
describes the origin of this work, not the current tracked/untracked status. Preserve all dirty
changes and inspect status before continuing. `gamedata/match_setup.json` is user runtime state.

The current power contract is `docs/POWER_GRID.md`: whole-grid allocation each tick, no transit
buffers, high/medium/low priority with upstream consumption first, equal sharing within a tier,
source-only generators, independent consumption/output switches, and storage charging only after
all enabled demand in a component is met. Zero transfer capacity blocks output.
Save format stays 1; old saves missing the new consumptionEnabled/outputEnabled fields are rejected.

## 2026-09-26 recovery checkpoint (historical baseline)

The repository is intentionally dirty. The current uncommitted batch is a coherent power-grid
hardening, power-visualization, and drone-logistics change set built on commit `710a85d` (`described
power betterm, and started on drone movement enhancements`). Do not reset, restore, or replace these
files. Start recovery with:

```powershell
Set-Location C:\Users\Brord\Desktop\Work\self\new
git status --short
git diff --stat
git diff --check
```

Important untracked/new files are part of the batch and must be preserved:

- `src/simulation/PowerGridSystem.hpp`
- `src/simulation/PowerGridSystem.cpp`
- `tests/PowerGridSystemTests.cpp`
- `assets/shaders/power_overlay.vert`
- `assets/shaders/power_overlay.frag`

`src/world/PowerGrid.hpp` is deliberately deleted because authoritative grid ownership moved into
`src/simulation/PowerGridSystem.*`. Many tracked files are modified as part of that migration,
including commands/codecs, persistence/checksums, entity power state, renderer/UI, definitions,
tests, and documentation. `gamedata/config.json` contains the F7 power-debug binding.
`gamedata/match_setup.json` is also dirty; inspect it before deciding whether it is user-selected
runtime state or a code-related fixture. Never discard it automatically.

### What this batch implements

- The deterministic power solver is extracted from `GameSession` into `PowerGridSystem` with
  revisioned sorted topology snapshots and explicit dirty invalidation.
- Power reaches the entire grid each tick without transit buffers. Allocation uses
  high/medium/low priority, upstream consumption before forwarding, and equal branch shares.
- Multi-generator territories deterministically fall back to a connected generator with real
  surplus rather than remaining captured by a deficient nearer generator.
- Equal-hop routes from the same supplying generator use total squared horizontal segment length,
  then the full root-to-device stable-ID sequence. Generator selection still uses hop count,
  direct generator distance, and generator ID before comparing routes within the selected root.
- Storage charges only after all enabled consumer demand is met, and never after discharge in the
  same tick. Consumption, output, and stored-power discharge have independent authoritative toggles.
- Connection validation/mutation is centralized and reports explicit failure reasons. Destruction
  removes reciprocal links immediately.
- F7 shows power diagnostics, directional last-tick flow, topology/priority/control data, and a
  translucent terrain-following green range disc for every local power device, including poles.
- Placing any power device draws only one green proposed link to the nearest operational compatible
  endpoint with a free slot and overlapping ranges. It draws nothing when no endpoint is valid.
- A dedicated `power_overlay` shader supplies translucent world overlays without altering terrain
  shader alpha behavior.
- The power stress test simulates two identical 257-device grids for 600 ticks, compares allocation
  fingerprints every tick, verifies per-device conservation, performs an active split/reconnect,
  and enforces a broad 30-second Debug regression ceiling. It measured roughly 10.2 seconds on the
  development machine during this session.

### Latest drone/logistics behavior

- Every flying battery drone at exactly zero charge enters charger recovery, including an otherwise
  idle drone. Manual movement remains blocked until charging succeeds.
- A drone at its construction or repair target also enters recovery when its positive battery
  remainder cannot pay for the next work step. The existing suspended-task path preserves the
  order through unavailable chargers and resumes it after charging succeeds.
- Charging temporarily suspends and later resumes valid work. A deliberate non-gather order is
  different: move, construct, repair, attack, and stop clear the gather loop's source, destination,
  processor preference/output, waiting state, and repeat flag while retaining any cargo already
  carried. This prevents `gather -> build -> unexpectedly resume gather`.
- When a resource node is depleted before cargo is full, the drone deterministically targets the
  nearest remaining node of the same raw resource type, including nodes in another field. Search
  uses horizontal distance from the depleted node, bounded by twice its field definition's maximum
  radius, with stable IDs breaking equal-distance ties. No field definition or no nearby eligible
  node means normal delivery/idle behavior continues. This rule is identical in new and loaded
  matches: transient `ResourceLayout` geometry is diagnostic-only and never controls retargeting.
- Cargo delivery keeps one committed processor and one stable approach point. `routeToInteraction`
  no longer recalculates the closest boundary point every few simulation ticks as the drone moves;
  it reroutes when the target changes or the current route is exhausted.

### Validation at handoff (2026-09-27)

- Subsequent C1 conversion-phase change: full Debug build succeeded; resource delivery (104.49s),
  drone reliability (42.80s), and game systems (214.85s) passed on the final implementation.
  Delivery tests cover both entity creation orders and power loss/recovery; existing capacity
  tests now expect immediate output for the accepted portion. No save-schema change was needed.

- Subsequent F7 repair-definition change: full Debug build and targeted drone reliability,
  definition validation, and persistence tests passed (3/3, 42.48 seconds). The full-suite result
  below is from the preceding power redesign; it was not repeated for this focused repair change.

- `strategy_game` builds successfully after all changes.
- `renderer_integration_tests` passes with the new translucent range discs and placement-link paths;
  no OpenGL errors were emitted. NVIDIA shader-state recompilation warnings remain informational.
- `power_grid_system_tests` passes, including the large-grid stress/profile probe.
- `resource_delivery_routing` and `drone_reliability` both pass after the latest drone fixes.
- After the whole-grid power redesign and drone fixes, the full Debug build succeeded and all
  18 tests passed in one run: `ctest --test-dir build/debug -C Debug --output-on-failure -j 4`.
  Total elapsed time was 255.51 seconds; power-grid tests took 15.89 seconds, including the
  257-device/two-world/600-tick determinism and conservation probe. Renderer integration,
  independent power controls/command codecs/checksums, persistence, and HUD layout tests passed.
- `git diff --check` passes; PowerShell reports only expected LF-to-CRLF warnings.

Recommended recovery validation:

```powershell
cmake --build build/debug --config Debug -j 8
ctest --test-dir build/debug -C Debug --output-on-failure
```

The latest parallel Debug run took approximately 63 seconds for terrain queries and
256 seconds for game systems. Do not interpret a quiet test process during those intervals as a
hang. Tests install handlers that suppress Visual C++ abort popups and report failures to stderr.

### Safest next work

1. Manually verify F7 range discs on hubs, generators, processors, chargers, and electricity poles;
   verify power-device placement shows one nearest green link and no link outside range.
2. Manually exercise a drone through gather, adjacent-node retarget, delivery, explicit build order,
   battery exhaustion, recharge, and task resumption. Confirm markers remain stable.
3. Run the complete test suite and commit the coherent batch once visual behavior is accepted.
4. Resume the active roadmap with repeatable 10-15 minute opening-economy validation, followed by
   10x10/15x15/20x20 generation/simulation/memory/render profiling. Water and vegetation refinement
   remain intentionally paused unless the user explicitly changes direction.

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
The save schema is format 1 and must remain format 1 until the user explicitly changes this
policy. Update format 1 in place during development; do not add migrations, compatibility readers,
fallback fields, or increment the format number. Existing local saves may be deleted when the
schema changes.

## Current terrain priority

Water refinement is paused. Preserve the existing optional `TerrainWaterGenerator`, layout-level
`hydrologyEnabled` switch, water semantics, and F6 diagnostics, but do not select river smoothing
or water presentation as the next task unless the user explicitly resumes it. Known raw-water gaps
are attributed to reach/confluence ownership, vertex-grid rasterization, and coverage/depth cutoff;
future work must repair the generated field rather than masking it in the shader.

The active terrain TODO order is resource-field statistical and stream-isolation coverage,
navigation/start/fairness diagnostics, generation reports and map-size performance budgets,
layout/biome definition extensibility, custom-map hardening, and broader deterministic seam and
quality tests. Vegetation expansion and tuning remain deferred as well.

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
- The main menu, Settings, Match Setup, pause menu, Build Mode palette, and loading overlay use RmlUi documents under
  `assets/ui/`.
  `RmlUiManager` connects them to the SDL 3/OpenGL render loop through one descriptor-driven
  screen API. It owns a screen stack, shared focus/action routing, text and attribute updates,
  rendering, and teardown. Do not add state-specific `open...Menu` renderer functions; push a
  `RmlUiScreenDefinition`, retain its returned handle, and remove that exact handle when the state
  is destroyed. Handle-based removal is required because replacement states are constructed before
  the previous state is destroyed. Remaining gameplay screens still use `UiDocument` while they
  are migrated.
- Loading uses `assets/ui/loading.rml` and `loading.rcss`. `drawLoadingScreen` creates one overlay
  handle lazily, updates its status/progress properties, and `finishLoadingScreen` removes it before
  normal state rendering resumes.
- Play owns the `pause_menu.rml` handle while paused. Settings temporarily covers that document;
  closing Settings reveals it again. Replacing Play removes the exact pause handle after the new
  state has been constructed.
- Build Mode owns `build_menu.rml`. Its palette buttons are generated from
  `matchRules.buildPalette`; do not duplicate the building list in RML. RmlUi pointer capture blocks
  placement clicks behind the menu.
  Entity menus derive actions, recipes, upgrades, state, and icons from definitions.
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
- Player starts use assigned opposite corners, independently drawing 2–5 inward chunk offsets
  from the same initial seed, then seed + 1 on failure (256 attempts/player). First valid starts
  remain fixed; terrain, usable land, connectivity and half-map-side separation are checked.
  Resource suitability is a separate pass: ordinary fields first, then top up nearby reachable
  Scrap to 600 within radius/travel cost 55, adding nodes at distances 16–55 if needed.
  See docs/TERRAIN_GENERATION.md section 7 for limits and failure behavior.
- Terrain is seeded and chunked, with LOD and blended materials.
- Resources and presentation-only vegetation use named deterministic streams.
- Decorative grass, flowers, weeds, reeds, and stones are configured in
  `assets/gameplay/rules.json` and archetyped in `decorations.json`. They use deterministic smooth
  density fields, clustered placement, and family spacing groups. Decorative vegetation lives in
  chunked `VegetationField` render data rather than authoritative `World` entities, is GPU-instanced,
  and is cleared by accepted building placement. Further vegetation work is intentionally deferred.

## Immediate active backlog

Do not begin additional vegetation work unless the user explicitly resumes it. Before Milestone 6,
work through this order:

Resource path-cost fairness, deterministic compensation, and resource-stream retry are complete.
The next resource-generation revision uses explicit non-entity resource fields. Fields may overlap
one another and cross biome boundaries; their deterministically generated resource-node entities
may not overlap. Node variants own presentation, collision, and capacity, while fairness continues
to measure reachable resulting capacity rather than field count.
Accepted `ResourceLayout` data is retained by `GameSession` only as transient diagnostics: it is not
saved or checksummed. F4 renders accepted field/node geometry and reports cursor field membership.
Focused multi-seed tests cover exact repeatability, field membership, variant validity, and global
node non-overlap.
Resource node presentations now use distinct model assets at each size: Scrap pile/salvage variants,
Oil seep/barrel/pump-jack variants, and small/medium/large Uranium crystal clusters. Raw CC0 sources
live under `assets/sources/resources`; `tools/process_resource_variants.py` rebuilds normalized GLBs.
The active order is now:

1. Resource-field/node-variant generation realignment.
2. Navigation congestion, arrival slots, local avoidance, and F3 path/stuck diagnostics.
3. ~~Drone charger-loss/stranded recovery and deterministic task resumption.~~ Focused reliability
   coverage is complete; extended soak testing can continue with normal regression work.
4. ~~Construction concurrency and terrain-foundation profiling.~~ Reliability pass complete;
   broader recipe/content validation remains.
5. ~~Power topology/storage/priority/limit stress tests and large-grid profiling.~~ The automated
   probe now covers two deterministic 257-device grids over 600 ticks, conservation, topology
   split/reconnect, and a broad performance regression budget. Continue increasing scale only when
   a measured gameplay grid warrants it.
6. Repeatable 10–15 minute opening-economy validation and balance measurements.
7. 10x10, 15x15, and 20x20 generation/simulation/memory/render profiling.

Then proceed to Milestone 6, the first combat slice.

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
- Repair capability is an optional typed `RepairDefinition` on the unit archetype, loaded from
  `repair.healthPerTick` and `repair.batteryPerTick` in units.json. Current drone balance is 2 HP
  for 1 battery per tick; finite positive values and full-battery affordability are validated.
  Commands and restored work orders require the definition. Partial final healing costs a full
  configured step; fully healed targets cost nothing. No new per-entity state or save fields.
- Automatic charging return triggers at zero power or when a construction/repair step at the
  target is unaffordable, including a positive fractional remainder; it is not a reserve threshold.
  Exhaustion is checked authoritatively for every flying drone, including idle drones. Zero-power
  drones cannot accept manual movement until recharged.
- Chargers are power-device definitions with different `chargePerTick` and `chargingSlots` values.
  A drone finds a powered, reachable charger with a free slot, recharges, and deterministically
  resumes a suspended task. Destroyed, disconnected, unreachable, full, and absent chargers have
  explicit recovery behavior and visible stranded reasons; stranded drones rescan automatically.
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
  Conversion runs in its own phase after all unit deliveries and destruction cleanup, using
  the current tick's power allocation. Eligible deliveries convert that same tick regardless of
  entity creation order; there is no processing timer or new saved state.
- When the committed target is full, commitment is cleared, including an explicit preference for
  that full target; the drone selects the nearest proper powered destination and then the hub.
- Destroyed, incompatible, or inaccessible targets are lost and invoke replacement/failure logic.
- Partial delivery fills available capacity and retains remaining cargo aboard the drone.
- Drones can repeat gather → deliver → return. Cargo and destination markers are rendered.
- A depleted node retargets the nearest remaining same-resource node within twice its definition's
  maximum field radius while cargo space remains; crossing field boundaries is allowed. Field
  geometry does not affect the choice before or after loading. Non-gather orders explicitly clear
  the gather loop, and delivery retains one committed target/approach point instead of oscillating
  several times per second.
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
- Multiple construction contributions are resolved before one terrain-foundation rebuild per tick.
  Rebuilds restore only affected old/new regions rather than copying the complete heightfield. F3
  reports last/peak rebuild time, reset vertices, and rebuild count.

### Power grid

- Serializable authoritative commands: connect, disconnect, set priority, enable/disable, and
  independently enable/disable stored-power discharge.
- Devices define generation, demand, storage, connection range, transfer limit,
  maximum connections, charge rate, and low/medium/high priority in `assets/gameplay/power.json`.
- Simulation reuses sorted topology and allocates the whole grid every tick with no transit buffers.
  High/medium/low priority controls shortage allocation. Upstream use precedes forwarding.
  Equal-priority requesting branches share output equally; unused shares are redistributed.
  `docs/POWER_GRID.md` is authoritative. Generators accept no incoming power, and zero transfer
  capacity blocks forwarding.
- The authoritative solver is extracted into `src/simulation/PowerGridSystem.*`. It owns sorted,
  revisioned per-player node/edge/component snapshots. Connection commands, enabled state,
  operational completion, destruction, structural world changes, and world replacement invalidate
  topology explicitly; clean grids reuse their snapshots.
- Power connection creation/removal is centralized in `PowerGridSystem`. Invalid targets, enemy or
  non-operational/disabled endpoints, duplicates, zero/full connection capacity, range failures,
  and missing links return explicit reasons. `World::destroyEntity` removes reciprocal links
  immediately; rebuilding an entity does not restore destroyed links.
- Placing a connectable power building creates one reciprocal link immediately to the nearest
  operational, enabled, same-owner device with a free slot inside both endpoints' range. The new
  planned building may be unfinished; topology activates it on completion. Equal distances use
  the lower stable entity ID. Cancellation/destruction removes the reciprocal link normally.
- Power state is `powered`, `underpowered`, or `offline` (`notApplicable` exists for non-devices).
  Storage participation is not a separate operational-state enum.
- The top HUD shows power as supply/demand. Clicking it opens the power overlay. Grid connections,
  device values/states, world power reach/tint, and selected-device grid actions are visible.
- F7 opens a mutually exclusive power-debug view with directional latest-tick flow, node priority,
  root/parent ownership, consumption/output controls, storage, transfer limits, definition-backed connection
  ranges, and aggregate grid diagnostics. Ranges are translucent green terrain-following discs for
  every device, including poles. A power-device placement preview draws the one green link that
  placement will create to its nearest valid endpoint, or nothing when none is connectable.
- Grid topology and allocation are persisted and checksummed. Network transport is still future work.
- Storage charges only after all enabled consumer demand in its component is supplied, subject to
  finite charge/discharge/storage/output limits. A store that discharged cannot recharge that tick.
  Generators with storage charge from their own surplus. Storage-only devices can receive power.
  Consumption, forwarding, and discharge are independently controlled; flags are saved/checksummed.
  F7 flow and route diagnostics are transient and excluded from saves/checksums.
- A high-priority downstream consumer also requests its upstream consumption prerequisites.
  After sources or output capacity exhaust, deterministic rerouting lets other reachable sources
  supply remaining demand in the same tick; generators never accept power during fallback.
- `power_grid_system_tests` includes the repeatable large-grid hardening/profile probe in addition
  to focused topology, storage, priority, transfer-limit, connection-limit, and fallback coverage.

### Production, upgrades, UI, audio, and particles

- Command hubs produce construction drones, not workers. Production and research use unlimited
  deterministic queues with fixed-size queue icons; clicking an item cancels and fully refunds it.
- Queue hover exposes product/upgrade identity. Disabled actions are visibly disabled and explain
  insufficient resources, prerequisites, limits, or other conditions.
- Entity action tooltips appear after 0.5 seconds, wrap into a popup above the action/queue area, and
  include resource costs, ongoing power demand, descriptions, requirements, and unmet conditions.
  Disabled buttons remain hoverable. The top resource-bar document must not reset the entity
  controller's hover timer. Moving within the same button preserves elapsed hover time.
  Changed tooltip content resets the delay. Free actions omit the resource-cost line.
- All building definitions require localized descriptionKey and requiredUpgrades fields. The
  current requirement arrays are empty by user choice. Future upgrade IDs require level >= 1
  on an owned, living, operational building (queued/enemy/unfinished research does not count).
  GameSession::missingBuildingUpgrades serves UI feedback and authoritative construction gates.
  Loss of the qualifying building affects future placement, not already built structures.
  Requirements reuse persisted entity upgrades; no new save fields or format changes.
- Upgrade definitions support research recipes, max levels, prerequisites, allowed researchers,
  exclusive groups, localization/icons, and deterministic modifier layers.
- Entity HUDs use a compact split layout: left action/upgrade grid, right entity information;
  queues sit above. Homogeneous multi-selection shows the shared menu plus compact per-unit cards
  with health/power bars.
- The main menu uses RmlUi flex layout, percentage insets, min/max dimensions, CSS hover/focus
  states, and existing menu textures. Match setup, pause, settings, loading, editor, HUD, and the
  power overlay still use the shared `UiDocument` path during the incremental migration.
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
- New-match world generation runs as a cancellable CPU background job. Progress is exposed through
  `world/GenerationProgress.hpp`; never call OpenGL from that worker. The accepted `GameSession`
  terrain is staged into `Renderer`, which uploads a bounded number of terrain chunks per loading
  frame on the render thread.
- Shaders: `assets/shaders/`; particle definitions: `assets/presentation/particle_effects.json`.
- Keep third-party licenses and attribution when moving/extracting asset packs.

## Tests and recent validation

- 2026-10-01 gameplay HUD migration: the resource/power strip, minimap chrome, entity details,
  action palette, production queue, mixed-selection controls, alerts, and placement feedback now
  use `assets/ui/gameplay_hud.rml` and `.rcss`. `PlayState` supplies definition-driven markup and
  preserves existing command dispatch. The renderer draws only the black minimap interior plus
  fog/marker data before RmlUi draws the permanent frame. The old `UiDocument`, `UiController`,
  `UiLayout`, `EntityHudLayout`, `GameHudLayout`, and `UiTheme` files were removed. Debug panels
  use direct debug rendering. Focused Debug builds and `entity_hud_model_tests`,
  `state_stack_tests`, and `renderer_integration_tests` passed.
- Image-backed menu buttons share `assets/ui/common_buttons.rcss`. Use the `art-button` class with
  an optional `button-icon` and a label `span`; its RCSS image decorator supplies
  `menu_button.png`, so do not add a decorative image child. The shared flex layout centers the
  icon and label as one group and owns hover/focus/pressed/disabled tinting. Build and gameplay
  action slots use the separate `action_slot_transparent.png` decorator. Screen RCSS should define
  only dimensions, typography, and screen-specific movement.

- Terrain's world-space vertical scale is 60 units. Resource nodes and their debug geometry use
  `Terrain::heightAt`, so they remain seated on the doubled terrain height. Biome and resource
  height ranges remain normalized. Gameplay slope checks and navigation rise costs use the stable
  authored 30-unit slope scale, decoupling availability from visual vertical exaggeration.
- Generation diagnostics are buffered on the generation worker and emitted as one
  `world_generation` log entry when the generation future finishes (success, cancellation or
  failure), in `gamedata/logs/strategy-game.log`. They include candidate chunks/seeds and rejection
  reasons, accepted starts, opening capacity before/after top-ups, resource-layout attempts,
  compensation additions, and final per-player travel-band capacities. No per-tick logging.
- For opening-generation changes, start with `cmake --build build/debug --config Debug --target
  opening_generation_tests -j 8`, then `ctest --test-dir build/debug -C Debug -R
  '^opening_generation$' --output-on-failure`. The focused target reuses one terrain, constructs
  no GameSessions, and takes about 5.4 seconds in Debug. It checks assigned corners, independent
  retry seeds, stable accepted starts, deterministic selection, resource-independent starts,
  reuse of sufficient existing Scrap and filling a forced shortfall. Labels: `fast;generation`;
  timeout: 30 seconds. Full multi-seed and gameplay suites remain broader milestone checks.
- 2026-09-28 corner-start/resource separation: Debug build passed. All 17 other CTest targets
  passed after the compensation update; the game-system executable then passed after replacing
  obsolete start-distance/node-count expectations and a hard-coded 160-Scrap depletion check.
  Multi-seed coverage checks assigned corners, preservation of player one's accepted start,
  deterministic layouts, fixed node capacities, and at least 600 nearby Scrap per player.
- Resource nodes now start with their archetype's fixed capacity, including starting and fairness
  compensation nodes. Random capacity multipliers are removed from field definitions and generation.
  Existing saves retain their remaining resource amounts; new-map layouts can change because the
  generator no longer draws random capacity values.
- 2026-09-28 building/upgrade hover details and requirement support: Debug build passed;
  `ctest --test-dir build/debug -C Debug --output-on-failure -j 4` passed all 18 tests
  in 275.85 seconds. The delayed, wrapped popup was also visually checked with the renderer
  fixture, including a disabled action and missing upgrade requirements.
- CTest targets include terrain, game systems, resource delivery, spatial shapes, vegetation,
  entity HUD, definition registry, persistence, asset imports, particle definitions/runtime, state
  stack, renderer integration, and render architecture.
- Most relevant last-known passing checks:
  `resource_delivery_routing`, `definition_registry_validation`, `vegetation_generation`, and
  `renderer_integration`.
- `game_system_tests` is comprehensive and slow in Debug because it constructs many full sessions.
  A direct run can take several minutes. Do not mistake silence for a hang; use focused tests during
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
# Match setup persistence

Match setup choices are stored independently of save games in `gamedata/match_setup.json`
(configurable through `configuration.matchSetupFile`). The profile stores stable IDs and numeric
values, never UI selector indices. `MatchSetupProfileStore` preserves unknown JSON members when it
writes known options, so future biome and layout controls can add nested settings without losing
them. This profile has no save-game format version and must not change `SaveGame::formatVersion`.
