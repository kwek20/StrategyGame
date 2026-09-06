# Strategy Game

A modern C++20 rebuild of the original Visual Studio strategy-game prototype. It is
currently a playable architecture prototype for a deterministic 1v1 RTS with optional
third-person control of individual units.

## Current features

- Seeded, chunked terrain with distance-based level of detail
- Deterministically generated wood, stone, and gold resources
- Two players with independent countries, resources, and fog of war
- Unit selection, movement, gathering, combat, and direct control
- Buildings with health, vision, production queues, and upgrades
- Build mode, versioned JSON saves, menus, settings, HUD, and diagnostics
- Assimp model loading with glTF/GLB, FBX, OBJ, and other supported formats

## Requirements

Install:

1. Visual Studio 2022 with **Desktop development with C++**
   - MSVC v143 x64 toolchain
   - Windows 10 or 11 SDK
2. CMake 3.25 or newer
3. Visual Studio Code with Microsoft C/C++ and CMake Tools
4. vcpkg

The checked-in `vcpkg.json` declares SDL3, glad, GLM, Assimp, stb, FreeType, and
RapidJSON. CMake manifest mode installs missing declared packages when configured with
the vcpkg toolchain. This project currently uses:

```text
C:\Users\Brord\Desktop\Work\self\vcpkg
```

## Configure and build

From a Developer PowerShell for Visual Studio:

```powershell
$env:VCPKG_ROOT = 'C:\Users\Brord\Desktop\Work\self\vcpkg'
cmake --preset windows-debug
cmake --build --preset debug
```

The executable is `build/debug/Debug/strategy_game.exe`. For Release:

```powershell
cmake --preset windows-release
cmake --build --preset release
```

In VS Code, select the Visual Studio 2022 x64 kit through CMake Tools. Run with the
project root as the working directory so `assets/` and `gamedata/` can be found.

## Tests

```powershell
ctest --test-dir build/debug -C Debug --output-on-failure
```

Tests cover terrain, gameplay systems, deterministic replay and checksums, persistence,
CPU asset imports, state transitions, events, UI interaction, and diagnostics. A hidden
OpenGL integration test renders terrain, asynchronously uploaded models, outlines, UI,
and text while checking for driver errors.

## Controls

### Menus

- Enter a decimal terrain seed in the seed field.
- Choose countries using the arrows or `Q`/`E` and `Z`/`C`.
- The main menu provides Start, Build, Load, Settings, and Exit.
- `Escape` opens the in-game pause menu.
- Pause provides Resume, Settings, and Exit to Main Menu.

### Strategy view

- `W`, `A`, `S`, `D`: move the camera relative to its yaw
- Middle mouse drag: orbit
- Right mouse drag: pan and rotate
- Mouse wheel: zoom
- Left click: select an entity
- Left drag: box-select units and buildings
- Shift-click a selected type: remove that type from the selection
- Double-click a controllable unit: enter third-person control
- Right click: issue a movement or contextual order
- `F3`: detailed diagnostics
- `F5` / `F9`: save / load

### Unit control

- Mouse movement rotates the character and camera.
- `W`, `A`, `S`, `D`: move the character
- `Escape`: return to strategy view

### Build mode

- `1` / `2`: worker / town center
- `T`: switch Team A/B
- Left click: place
- `Backspace`: undo latest placement
- `F5`: save map
- `Escape`: main menu

Gameplay keybindings shown in Settings can be rebound.

## Configuration and saves

`gamedata/config.json` contains the save location, video settings, master/music/effects
volume, mute state, and keybindings. Saves go under `gamedata/saves/` by default.

The versioned save format stores authoritative player, terrain, entity, component,
production, upgrade, and discovered-world state. Rebuildable navigation caches are not
persisted.

## Architecture

The executable contains only the entry point. CMake divides the implementation into:

- `strategy_core` and `strategy_diagnostics`
- `strategy_localization`, `strategy_players`, and `strategy_terrain`
- `strategy_world`, `strategy_gameplay`, and `strategy_simulation`
- `strategy_persistence`, `strategy_audio`, `strategy_asset_data`, and `strategy_assets`
- `strategy_camera`, `strategy_render`, `strategy_states`, and `strategy_app`

Application-owned services are injected through `StateContext`. A deferred state stack
supports push, pop, replace, and clear transitions. Settings can be opened over the main
menu or a paused match without recreating the underlying state.

The typed event bus supports immediate and queued delivery. UI/game states emit audio
events without owning the audio backend. Render passes explicitly establish and restore
their OpenGL state. UI documents batch panels and labels, while legacy HUD text is queued
and rendered once at the end of each frame.

`ShaderManager` owns every OpenGL shader program used by terrain, models, outlines, HUD,
UI, and font rendering. Callers retain typed, generational `ShaderHandle` values rather
than raw program IDs. Programs are compiled and linked through one diagnostic path,
lookups by name are cached, uniform locations are resolved once, and all programs are
released by the manager while the OpenGL context is still active.

## Deterministic multiplayer boundary

`GameSession` is the authoritative simulation boundary and runs at 30 integer ticks per
second without rendering or audio dependencies. Commands carry player and sequence IDs
and use a versioned binary codec.

Resource generation has named deterministic random streams. State checksums sort players
and entities by ID and exclude presentation/navigation caches. Tests verify that equal
seeds, serialized commands, and tick counts produce equal checksums.

Movement still uses floating point. Strict cross-architecture lockstep may eventually
require fixed-point simulation or authoritative server correction.

## Assets and localization

- Models: `assets/models/`
- Blender sources: `assets/sources/`
- Model catalogue: `assets/entities.json`
- Gameplay data: `assets/gameplay/`
- English text: `assets/text/en_us.json`
- Audio cues: `assets/audio/audio.json`
- Match asset groups: `assets/asset_manifest.json`

Each menu, match, or build-mode asset group explicitly declares its required models,
textures, sounds, and definitions in `asset_manifest.json`. Starting or loading a map
preloads its declared model set and reports queued/importing/uploading progress on the
loading screen. Models requested later remain non-blocking: a yellow marker is rendered
while loading and a magenta marker identifies an import failure.

The resource manager recursively indexes models. `strategy_asset_data` imports Assimp
meshes, materials, animations, and decoded texture pixels without OpenGL. Up to two CPU
imports run concurrently; completed assets enter a render-thread queue that creates the
GPU buffers and textures.

Rendering retains typed, generational `ModelHandle` values instead of repeatedly looking
up model paths by string. The audio service similarly exposes `AudioHandle`; distinct
`TextureHandle` support is reserved for standalone UI/environment textures, while model
material textures remain owned by their model asset. Handles carry both a slot index and
generation so stale handles are rejected, and model slots expose queued, importing,
uploading, ready, and failed states. Entity `modelKey` strings remain authoritative
archetype IDs for saves and deterministic gameplay rather than GPU resource references.

Runtime UI text belongs in the language JSON rather than gameplay or renderer code.

## Logs and diagnostics

Logs are written to `gamedata/logs/strategy-game.log` and echoed to the console. Entries
contain a timestamp, severity, category, source file, and line. Startup records video and
OpenGL system information. Repeated driver messages are rate-limited.

`F3` shows camera, world, selected-entity component data, and last/rolling-average/peak
CPU timings for simulation, total rendering, terrain, models, asset uploads, UI, and
text. Frames slower than 50 ms are logged at most once every two seconds. The logger
retains its latest 256 entries for a future in-game diagnostic console.
