# Strategy Game

A clean C++20 restart of the original Visual Studio 2013 strategy-game prototype.
The foundation retains its game-state loop and WASD camera controls, while replacing
raw ownership, global state, hard-coded library paths, and legacy fixed-function
OpenGL with a modern CMake-based structure.

## Software to install

Nothing is downloaded by this repository or by CMake configuration. Install:

1. Visual Studio 2022 Build Tools or Community with **Desktop development with C++**
   - MSVC v143 x64 compiler
   - Windows 10/11 SDK
   - CMake
   - Ninja
2. Visual Studio Code
   - Microsoft C/C++ extension
   - CMake Tools extension
3. vcpkg
4. Libraries through vcpkg
   - SDL3 — windows, input, events, timing
   - glad — modern OpenGL function loading
   - GLM — vectors and matrices
   - Assimp — model importing

The included `vcpkg.json` declares the library list but does not install it by itself.

## Configure dependencies

After installing vcpkg, set `VCPKG_ROOT` to its absolute directory. From a Developer
PowerShell for Visual Studio:

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset windows-debug
cmake --build --preset debug
```

Run `build/debug/strategy_game.exe`, or select the Debug Strategy Game launch profile
in VS Code.

## Controls

- Main menu: click **Start** or **Exit**
- Click the seed field and enter a decimal terrain seed before starting
- `Enter`/`Space`: start from the main menu
- `W`, `A`, `S`, `D`: pan the RTS camera relative to its current yaw
- Hold right mouse button and drag: orbit the camera
- Mouse wheel: zoom
- `Escape`: open or close the in-game pause menu
- Pause menu: click **Resume** or **Exit**; Exit returns to the main menu
- `F5`: save the current terrain seed and world to the configured save slot
- `F9`: load the configured save slot
- `P`: possess or release the local player's first directly controllable unit
- While possessing a unit, `W`, `A`, `S`, `D` send direct-control commands

## Configuration and saves

Runtime configuration is stored in `gamedata/config.json`. Its `configuration`
section selects the project-relative save directory and active save filename:

```json
{
  "configuration": {
    "saveDirectory": "gamedata/saves/",
    "saveFile": "autosave.json"
  }
}
```

Save files use a versioned JSON format and contain the terrain seed plus entity IDs,
names, model keys, positions, rotations, scales, faction ownership, direct controller,
strategic destination, and current direct-control input.

## Multiplayer simulation model

The game simulation is structured for symmetric 1v1 play. `GameSession` is the
authoritative boundary and runs at a fixed 30 ticks per second. Both players can issue
strategic commands and may possess directly controllable units owned by their faction.
Possession temporarily overrides autonomous movement without deleting the unit's queued
strategic destination. Commands carry per-player sequence numbers; unknown players,
duplicate sequences, cross-faction possession, and cross-faction orders are rejected.

The current client calls this session in-process. A future network server can call the
same command API without linking SDL, input handling, or rendering into command logic.

## Model assets

Place models anywhere under `assets/models/`. The resource manager scans folders
recursively and supports Assimp formats including OBJ, FBX, DAE, 3DS, glTF/GLB,
PLY, and STL. The filename without its extension is usable as an entity model key.

For example, `assets/models/buildings/house.obj` can be requested as `house` or
`buildings/house`. Assimp node transforms, normals, and diffuse, ambient, specular,
emissive, and shininess material values are retained. Texture loading is not yet
enabled, so material colors are used even when a model refers to image textures.

The on-screen debug HUD reports FPS, camera position, entity count, and successfully
loaded model count.

Terrain keeps a 512x512-cell CPU heightfield over the original world size. Each GPU
chunk has full-, half-, and quarter-density index buffers; zooming close selects the
full-resolution mesh, medium zoom selects half resolution, and distant zoom selects
quarter resolution. Global LOD switching prevents cracks between adjacent chunks, and
4x MSAA smooths close-view triangle edges.

## Why these libraries

- SDL3 replaces Allegro and provides a small, actively maintained platform layer.
- OpenGL 4.5 replaces the old immediate/fixed-function rendering path.
- glad explicitly loads modern OpenGL entry points.
- GLM provides tested graphics math instead of maintaining custom vector code.
- Assimp remains appropriate for supporting multiple model formats.
