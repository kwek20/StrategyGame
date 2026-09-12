# Modern RTS Icon Generation Brief

## Purpose

Create the production-ready icon family for the resource HUD, entity action panel,
construction palette, production queue, upgrades, selection summaries, and future status
badges. Gameplay IDs remain stable; definitions will be changed from `ui/placeholder`.

## Current implementation findings

- `assets/icons_atlas.json` defines a 512x512 atlas with 64x64 regions.
- The renderer owns `IconAtlas`, loads its texture through the resource manager, and uses semantic
  region IDs for live UI icons.
- The September 2026 refresh provides distinct source art and atlas regions for all active units,
  buildings, resources, upgrades, commands, processor states, and planned connected-grid actions.
- Compatibility aliases remain for old presentation keys, but resolve to the refreshed semantic
  artwork rather than retaining a second visual style.
- Resources and upgrades have definition-level `icon` fields. Unit and building archetypes resolve
  their icon through distinct presentation definitions.
- Dynamic recipe buttons should normally inherit the product icon. Research should inherit
  its upgrade icon.
- Hovered, disabled, selected, queued, and team-colored states belong in UI rendering and
  should not be baked into duplicate image files.

## Shared art direction

- modern brutalist, with a hint of steampunk and cyberpunk RTS: grounded, functional, and readable rather than fantasy
  or highly futuristic. 
- Bold silhouette and limited interior detail.
- buildings that consume power need to have an electricity power mast that is logical to the size of the building.
- Consistent three-quarter orthographic view for physical subjects; flat frontal symbols for
  abstract actions and status badges.
- The three dominant base colors are:
  Warm charcoal / gunmetal: #34302B
  Dusty taupe / earth brown: #756759
  Weathered concrete gray: #8B8176

  The amber/orange #C87B35 and solar-panel blue #35465B work better as accent colors rather than base colors.
- Neutral colors must remain readable under future team-color tinting.
- One centered subject with at least 8% clear space around it.
- Validate physical icons at 24, 36, 48, and 64 pixels. Validate status badges at 16 and 24.
- No text, letters, numbers, flags, country emblems, watermark, scenery, baked background,
  baked button frame, or shadow outside the silhouette.
- Use true transparency. Avoid details thinner than two pixels at 64x64.

## Deliverable and atlas format

Generate each icon separately as a 256x256 RGBA PNG under `assets/icons/source/`. These are
the editable production sources. Do not generate the sprite sheet as the source artwork.

After approval, downsample and pack the reviewed sources deterministically:

- Runtime size: 64x64 per icon.
- Target atlas: 512x512, eight columns by eight rows, capacity 64 icons.
- Runtime texture: `assets/textures/icons/icons_atlas.png`.
- Metadata: `assets/icons_atlas.json`.
- Preserve transparent padding and extrude edge pixels during packing to prevent neighboring
  regions bleeding under linear filtering.
- Use stable semantic region IDs, never numeric indexes in gameplay code.

## Set A: required by active code

### Units

| Region ID / source file | Active ID | Visual brief |
|---|---|---|
| `unit_worker` | `worker` | Human field engineer with hard hat and compact tool pack; strong human silhouette. |
| `unit_construction_drone` | `construction_drone` | Small quad-rotor utility drone with an articulated construction tool. It must not look armed. |

### Buildings

| Region ID / source file | Active IDs | Visual brief |
|---|---|---|
| `building_town_center` | `town_center`, `town_center_level_2`, `town_center_level_3` | Established civic/industrial headquarters with antenna mast. Reuse initially; level is shown by UI. |
| `building_command_hub` | `command_hub` | Deployable command hub with communications array and visible drone-charging pad. Clearly distinct from town center. |
| `building_alloy_processor` | `alloy_processor` | Compact scrap crusher and induction refinery with a raw-input hopper and stacked-alloy output. |
| `building_fuel_processor` | `fuel_processor` | Sealed modern refinery with pipework, a raw-input connection, and an amber processed-fuel tank. |
| `building_synthetic_mine` | `synthetic_mine` | Powered ground-processing rig with a compact excavator, enclosed feedstock hopper, and prominent electrical mast. |
| `building_basic_generator` | `basic_generator` | Compact turbine or alternator housing with cyan energized core. Do not depict solar panels; the active definition is generic. |
| `building_outpost` | `outpost` | Small fortified observation/production post with sensor mast. |
| `building_storage_silo` | `storage_silo` | Modular cylindrical storage tanks with loading manifold and access platform. |
| `building_electricity_pole` | `electricity_pole` | Compact grid relay with insulators, cable attachment points, and one restrained cyan energized element. |
| `building_charging_pad` | `charging_pad` | Low-profile drone landing and induction-charging pad. |
| `building_resource_extractor` | `resource_extractor` | Rugged modular extraction rig with intake head, conveyor housing, and a correctly scaled power mast. |
| `building_drone_factory` | `drone_factory` | Enclosed drone assembly bay with roof gantry, launch aperture, and industrial power mast. |
| `building_sensor_tower` | `sensor_tower` | Reinforced compact tower with a large directional sensor array and equipment shelter. |

### Resources

| Region ID / source file | Resource ID | State | Visual brief |
|---|---|---|---|
| `resource_scrap` | `scrap` | raw cargo | Irregular recoverable machinery, plate fragments, cable, and structural metal; visibly salvaged rather than refined. |
| `resource_oil` | `oil` | raw cargo | Dark crude-oil droplet paired with a compact industrial barrel or pump silhouette. |
| `resource_uranium` | `uranium` | raw cargo | Shielded mineral container with a restrained yellow-green energy accent; avoid relying only on a radiation glyph. |
| `resource_synthetic` | `synthetic` | raw cargo | Manufactured granular feedstock or neutral industrial slurry container, visually usable by either processing chain. |
| `resource_alloy` | `alloy` | stockpile | Clean stacked structural plates, beams, and machined blocks; unmistakably processed construction output. |
| `resource_fuel` | `fuel` | stockpile | Sealed military fuel canister with restrained amber fluid/energy accent and no lettering. |
| `resource_data` | `data` | strategic stockpile | Encrypted data core or server stack with a cyan signal pattern; distinct from electrical Power. |
| `resource_authority` | `authority` | strategic stockpile | Abstract command insignia or concentric influence marker without a country emblem, crown, or currency symbol. |
| `resource_power` | `power` | network capacity | Cyan generator/grid-node symbol that reads as current supply versus demand, not a battery or spendable item. |

Raw-resource icons primarily appear on deposits, cargo, processor inputs, tooltips, and logistics
overlays. The persistent player HUD uses Alloy, Fuel, Data, Authority, and the separate Power
capacity display. Synthetic uses one raw-cargo icon regardless of its selected destination; the
processor and resulting stockpile icon communicate whether it becomes Alloy or Fuel.

### Upgrades

| Region ID / source file | Upgrade ID | Visual brief |
|---|---|---|
| `upgrade_efficient_training` | `production.efficient_training` | Drone/production silhouette with fast-forward chevrons. Communicate production time, not movement speed. |
| `upgrade_town_center_level_2` | `building.town_center_level_2` | Command-building silhouette with a strong tier-up chevron. Do not bake in a numeral. |

### Commands and operational actions

Train, construct, and process recipe buttons should reuse their product icon. The following
non-product commands need symbols of their own:

| Region ID / source file | Use | Visual brief |
|---|---|---|
| `action_move` | Move order | Directional waypoint with a forward chevron. |
| `action_gather` | Gather order | Utility gripper collecting a small raw-resource cluster. |
| `action_construct` | Assign drones to construction | Drone tool applying energy to a skeletal building frame. |
| `action_stop` | Stop construction/work | Solid stop square combined with a tool motif, not merely a media button. |
| `action_repair` | Repair damaged entity | Wrench with a small restore spark. |
| `action_cancel_queue` | Cancel/refund queue affordance | Simple diagonal cancel cross, preferably drawn over the product icon by UI. |
| `action_power_connect` | Create a grid connection | Two heavy grid nodes joined by one clear energized cyan line. |
| `action_power_disconnect` | Remove a grid connection | Two grid nodes with a clean broken link and restrained warning-orange break. |
| `action_power_priority` | Change consumer priority | Three stacked power nodes with one emphasized upward chevron; no numbers. |

## Set B: status badges

These are simpler overlays designed primarily for 16–24 pixels.

| Region ID / source file | Implemented state | Visual brief |
|---|---|---|
| `status_constructing` | Building incomplete | Amber tool/crane over a partial frame. |
| `status_damaged` | Health below maximum | Cracked shield or plate with muted red-orange accent. |
| `status_low_battery` | Drone below reserve | Nearly empty battery with amber accent. |
| `status_charging` | Drone recharging | Battery with cyan inward energy bolt. |
| `status_powered` | Consumer connected | Compact cyan plug/grid node. |
| `status_unpowered` | Consumer unavailable | Disconnected plug/grid node with muted red accent. |
| `status_underpowered` | Consumer receives insufficient supply | Partially energized node, half cyan and half charcoal, with an amber shortage notch. |
| `status_power_blocked` | Powered operation blocked | Heavy grid node behind a small amber stop bar; distinct from a padlock. |
| `status_grid_island` | Device belongs to an isolated grid | Lone grid node surrounded by a broken connection ring. |
| `status_storage_charging` | Grid storage charging | Industrial capacitor with a cyan energy arrow entering. |
| `status_storage_discharging` | Grid storage discharging | Industrial capacitor with a cyan energy arrow leaving. |
| `status_processing` | Processor actively converted cargo | Interlocking crusher rollers with a short cyan activity arc. |
| `status_input_full` | Processor input capacity exhausted | Filled hopper with a compact amber capacity marker; no text or numerals. |
| `status_queue` | Production/research active | Gear with a circular progress motif. |
| `status_locked` | Prerequisite/rule blocks action | Minimal padlock silhouette. |

These additions reflect the existing construction, health, battery, charger, power, queue,
and disabled-action systems. Their HUD wiring can follow the core atlas integration.

## Set C: reserved future archetypes

Do not wire these into gameplay until definitions exist.

| Region ID / source file | Reserved concept | Visual brief |
|---|---|---|
| `building_solar_array` | Solar producer | Angled photovoltaic array, compact inverter, cyan electrical trace. |
| `unit_combat_drone` | Armed drone | Agile drone with an unmistakable weapon/sensor silhouette, distinct from the construction drone. |

## Set D: menu navigation and settings pictograms

Menu pictograms form a separate family from gameplay icons. They use the same industrial
materials and silhouette language, but are flatter, simpler, and primarily monochrome so they
remain readable beside localized button labels. They must never contain baked text.

Store editable 256x256 RGBA sources under `assets/icons/menu/source/`. Pack approved art into
`assets/textures/ui/menu_icons_atlas.png` with semantic metadata in
`assets/menu_icons_atlas.json`. Do not consume the remaining gameplay-atlas cells for this set.

| Region ID / source file | Screen/use | Visual brief |
|---|---|---|
| `menu_play` | Main menu | Forward-facing play chevron integrated with a compact command marker. |
| `menu_editor` | Main menu/build mode | Terrain tile with a small construction tool; distinguish from ordinary building placement. |
| `menu_load` | Main menu | Open storage container receiving a downward arrow. |
| `menu_settings` | Main and pause menus | Heavy industrial gear with a clean central hub. |
| `menu_exit` | Main and pause menus | Doorway with an outward arrow; avoid a power-button symbol. |
| `menu_back` | Match setup/settings | Broad left arrow with a compact corner return shape. |
| `menu_resume` | Pause menu | Play chevron emerging from two subtle pause bars. |
| `menu_start_match` | Match setup | Two opposing command markers converging on a central map point. |
| `menu_apply` | Settings | Strong confirmation check inside an open technical frame. |
| `menu_cancel` | Settings/dialogs | Simple diagonal cancel cross with rounded industrial geometry. |
| `menu_save` | Editor/game | Storage module receiving a downward data arrow; visually paired with `menu_load`. |
| `menu_delete_save` | Future save browser | Storage module with a restrained removal cross. |
| `settings_display` | Settings category | Monitor outline with a small resolution grid. |
| `settings_audio` | Settings category | Speaker with two bold sound arcs. |
| `settings_controls` | Settings category | Keyboard directional cluster paired with a compact gamepad control. |
| `settings_accessibility` | Future settings category | Open, neutral accessibility figure inside a readable circular marker. |
| `settings_ui_scale` | Settings control | Small and large interface rectangles connected by a scale arrow. |
| `settings_fullscreen` | Settings control | Four outward corner brackets. |
| `settings_restore_defaults` | Settings control | Circular restore arrow around a small gear. |
| `navigation_previous` | Selectors | Heavy left chevron. |
| `navigation_next` | Selectors | Heavy right chevron matching `navigation_previous`. |

Menu icons must be validated at 20, 24, 32, and 48 pixels. Hover, keyboard focus, pressed,
disabled, and destructive states are renderer-applied colors and outlines, not separate images.

## Set E: branding and menu backdrop

Branding should express drone-led industrial expansion, distributed power, logistics, and modern
combined-arms conflict without using a real flag, national emblem, or existing military insignia.
Finalize the game title before producing the wordmark.

Required deliverables:

| Asset | Runtime path | Specification |
|---|---|---|
| Primary emblem | `assets/textures/branding/game_emblem.png` | Transparent square symbol, readable from 32px through 512px. Combine a command node, rotor geometry, and connected-grid motif without becoming a literal drone illustration. |
| Horizontal logo | `assets/textures/branding/game_logo.png` | Transparent wide lockup. Generate the emblem artwork, but construct exact title text from a licensed project font whenever possible. |
| Application icon source | `assets/icons/branding/app_icon.png` | 1024x1024 master with a bold centered emblem and safe edge margins for later platform-specific icon export. |
| Monochrome emblem | `assets/textures/branding/game_emblem_mono.png` | Single-light-color transparent variant for loading, watermark, and small UI contexts. |
| Main-menu background | `assets/textures/menu/main_background.png` | 3840x2160 master, responsive 16:9 crop, industrial command hub and drones with a developing power network, no text or UI. Preserve quiet negative space on the left for the current menu panel and keep critical subjects inside the central 70% safe area. |

The match-setup and settings pages should reuse the main-menu background with renderer-controlled
darkening. The pause menu should retain the live game scene beneath a dim or blurred modal layer.
Do not generate a separate background for every page until the final navigation hierarchy proves
that one is needed.

### Branding prompt template

> Create a vector-friendly transparent emblem for a modern industrial RTS about autonomous
> construction drones, logistics, and connected electrical grids. Combine a compact command-node
> silhouette, restrained rotor geometry, and a clear network connection motif. Use heavy readable
> geometry, warm charcoal #34302B, weathered concrete gray #8B8176, restrained cyan energy, and
> amber #C87B35 only as a secondary accent. Neutral, faction-independent, grounded, and readable
> at 32x32. No text, letters, numbers, flag, national emblem, existing military insignia, scenery,
> watermark, mockup, button frame, or external shadow. True transparent background.

### Main-menu background prompt template

> Create a 3840x2160 cinematic menu background for a modern near-future RTS. Show an established
> industrial command hub, several small unarmed construction drones, raw-resource logistics, and
> a connected electrical network expanding across rugged terrain toward distant combined-arms
> conflict. Grounded engineering, modern brutalist structures, restrained cyberpunk accents,
> atmospheric depth, warm charcoal and weathered concrete materials with limited cyan power light
> and amber work light. Keep the left 34% visually quiet for a menu and all important subjects in
> the central 70% safe area for responsive cropping. No text, logo, flags, recognizable country
> symbols, interface elements, watermark, or frame.

## Set F: loading and multiplayer waiting states

The loading screen already owns localized status text and a real progress bar. Generated imagery
must complement those live values, never replace or fake them.

| Region ID / source file | Use | Visual brief |
|---|---|---|
| `loading_assets` | Asset preload/import/upload | Stacked asset blocks entering a compact system node. |
| `loading_terrain` | Terrain generation | Terrain tile resolving from a coarse grid into a smooth surface. |
| `loading_world` | World creation | Map grid with two or three entity markers appearing. |
| `loading_save` | Save read | Storage module sending structured data outward. |
| `loading_finalize` | Finalization | Completed segmented ring around the monochrome game emblem. |
| `waiting_searching` | Future matchmaking | Two distant player nodes scanned by a circular signal. |
| `waiting_ready` | Future lobby | Player node with a confirmation check. |
| `waiting_not_ready` | Future lobby | Inactive player node with an open status ring; do not use an error cross. |
| `waiting_connecting` | Future networking | Two endpoints joined by a partially completed cyan link. |
| `waiting_synchronizing` | Future networking | Mirrored state blocks with opposing transfer arrows. |
| `waiting_reconnecting` | Future networking | Broken link surrounded by a circular restore arrow. |
| `waiting_connection_lost` | Future networking | Separated endpoints with a restrained warning marker. |

Use the monochrome game emblem as the primary loading indicator and rotate or pulse it
procedurally. Do not generate a baked animation strip unless profiling shows procedural animation
is unsuitable. Stage and waiting pictograms belong in the menu atlas and must remain readable at
20–32 pixels.

## Set G: generated UI skin surfaces

The UI skin is not an icon set. Generate clean source surfaces, then convert them into deterministic
nine-slice regions so buttons and panels scale without stretching corners or baking in resolution.

Required regions:

- Standard button surface.
- Primary/action button surface.
- Destructive button surface.
- Panel surface and border.
- Modal surface and stronger border.
- Text-field surface.
- Queue-slot and entity-card frames.
- Tooltip surface.
- Divider and selected-tab marker.

Pack these separately in `assets/textures/ui/ui_skin.png` with metadata in
`assets/ui_skin.json`. Every scalable region must record its fixed corner insets. Keep surfaces
subtle enough that icons and text dominate. The renderer remains responsible for hover, focus,
pressed, disabled, affordability, selected, progress, and team-color feedback.

### UI-surface prompt template

> Create one seamless scalable UI surface for a modern industrial RTS: **[SURFACE TYPE]**.
> Front-facing orthographic interface material, dark warm gunmetal #34302B with restrained dusty
> taupe #756759 and weathered concrete #8B8176, subtle inset bevel, large calm center area, crisp
> corners suitable for nine-slice scaling, and restrained cyan or amber accent only where requested.
> No text, icon, symbol, logo, scenery, watermark, baked hover glow, drop shadow outside the bounds,
> or perspective distortion. Square RGBA source with true transparency outside the frame.

Before integrating Set G, add nine-slice support to `UiElement` and the UI renderer. Until then,
retain the current procedural rectangles rather than stretching generated button images.

## Excluded from this pass

- Crosshair, selection box, minimap dots, progress/health bars, and placement validity tint;
  these should remain procedural and dynamically colored.
- Country flags and specialization emblems; their visual direction is not defined yet.
- Weapon icons. `worker_unarmed` is the only equipped weapon, while `drone_light_weapon` is
  defined but not equipped or exposed by the current menus.
- Separate grayscale, disabled, hovered, selected, team A, or team B bitmaps.

## Prompt templates

### Physical subject

> Create one square transparent-background icon for a modern near-future RTS: **[SUBJECT]**.
> Show one centered subject in a consistent three-quarter orthographic view with a bold,
> unmistakable silhouette. Use warm charcoal/gunmetal #34302B, dusty taupe #756759, and
> weathered concrete gray #8B8176 as the dominant materials. Use restrained cyan functional
> lighting and amber #C87B35 only for construction or warning details. Grounded industrial design,
> minimal fine detail, no scenery, floor, text, letters, numbers, emblem, watermark, UI frame,
> or external drop shadow. Keep at least 8% transparent margin. It must remain recognizable
> at 24x24 and 64x64 pixels. Output a 256x256 RGBA PNG.

### Abstract action or status

> Create one square transparent-background UI pictogram for a modern RTS: **[ACTION OR
> STATUS]**. Use a flat frontal symbol and heavy readable geometry with warm charcoal #34302B,
> dusty taupe #756759, and weathered concrete #8B8176; cyan denotes active energy and amber
> #C87B35 or muted red denotes warnings. No text,
> letters, numbers, scenery, watermark, background, or UI frame. Keep at least 12%
> transparent margin and remain clear at 16x16 and 24x24. Output a 256x256 RGBA PNG.

## Consistency review before approval

1. Compare all entity icons together at 64x64.
2. Compare resources at 24x24 on both the dark HUD panel and a light neutral field.
3. Confirm town center and command hub remain distinguishable without labels.
4. Confirm construction and future combat drones cannot be confused.
5. Confirm Scrap, Oil, Uranium, and Synthetic read as raw cargo while Alloy and Fuel read as
   processed stockpiles.
6. Confirm power, battery, and charging remain distinct.
7. Desaturate the contact sheet and verify core silhouettes remain identifiable.
8. Reject false transparency, baked checkerboards, stray pixels, and cropped edges.
9. Confirm connected, disconnected, underpowered, blocked, and isolated-grid badges remain
   distinguishable without color.
10. Confirm connection actions read as commands while power-state badges read as conditions.
11. Confirm menu actions remain recognizable without their localized labels.
12. Test menu icons and UI surfaces at 75%, 100%, 125%, and 150% UI scale.
13. Test the menu background at 16:9, 16:10, and ultrawide crops without obscuring controls.
14. Verify loading artwork never implies progress different from the live progress bar.

## Runtime icon resolution rules

- Entity portrait: resolve from the entity archetype's presentation icon.
- Unit/building/resource recipe: reuse the product definition icon.
- Research recipe: use the linked upgrade definition icon.
- Queue entry: retain the resolved icon ID when enqueued; draw progress and cancellation as
  overlays.
- Homogeneous multi-selection: shared archetype icon plus a UI-rendered count.
- Mixed selection: one icon per archetype; never generate combined icons.
- Missing icon: use `ui/placeholder` and log the semantic ID once.

## Integrated content and remaining wiring

The atlas loader, texture upload, semantic-region lookup, and 512x512 runtime atlas are integrated.
The refreshed set contains 50 primary regions plus compatibility aliases. Remaining work belongs
to feature wiring as the connected grid is implemented:

1. Use `action_power_connect`, `action_power_disconnect`, and `action_power_priority` in the grid interaction tools.
2. Use the connected, underpowered, blocked, isolated, and storage-flow badges from authoritative grid state.
3. Keep processor and Synthetic Mine presentations on their distinct refreshed keys.
4. Keep recipe and queue icon resolution based on the product or linked upgrade definition.
5. Ensure only Alloy, Fuel, Data, Authority, and Power appear in the persistent resource HUD.
6. Show raw-resource icons on deposits, harvester cargo, processor buffers, tooltips, and
   logistics overlays.
7. Render destination feedback when Synthetic is assigned to Alloy versus Fuel processing.
8. Validate hover, disabled, affordability, queued, selected, and team-tint states.
9. Run asset-import, definition-registry, renderer-integration, and complete CTest validation after grid wiring.

## Menu and branding implementation order

1. Confirm the final game title and emblem concept.
2. Add the separate menu-atlas loader and semantic region lookup.
3. Generate and review Set D menu/settings icons.
4. Add nine-slice metadata and rendering support.
5. Generate and integrate Set G UI skin surfaces.
6. Generate the primary emblem, monochrome variant, and application-icon master.
7. Generate the main-menu background and add responsive safe-area cropping.
8. Animate the monochrome emblem procedurally on the loading screen.
9. Generate and wire the active loading-stage icons.
10. Keep multiplayer waiting icons reserved until lobby/network state definitions exist.

## Proposed deterministic atlas order

1. Active units
2. Active buildings
3. Enabled resources
4. Defined-but-disabled resources
5. Upgrades
6. Commands
7. Status badges
8. Reserved future archetypes

The packer assigns coordinates from this order, but runtime code must always resolve the
stable region ID rather than depending on atlas position.
