# Modern RTS Icon Generation Brief

## Purpose

Create the first production-ready icon family for the resource HUD, entity action panel,
construction palette, production queue, upgrades, selection summaries, and future status
badges. Gameplay IDs remain stable; definitions will be changed from `ui/placeholder` only
after the art has been reviewed. This document does not authorize generation yet.

## Current implementation findings

- `assets/icons_atlas.json` defines a 512x256 atlas with 64x64 regions.
- `IconAtlas` loads that metadata, but no live renderer currently owns or uses it.
- The build, entity-action, town-hall, and queue interfaces still draw temporary cyan
  rectangles instead of textured icons.
- The resource HUD is a hard-coded formatted text line and draws no resource icons.
- Resources and upgrades have definition-level `icon` fields. Unit and building archetypes
  still need an icon/presentation field.
- Dynamic recipe buttons should normally inherit the product icon. Research should inherit
  its upgrade icon.
- Hovered, disabled, selected, queued, and team-colored states belong in UI rendering and
  should not be baked into duplicate image files.

## Shared art direction

- Modern near-future industrial RTS: grounded, functional, and readable rather than fantasy
  or highly futuristic.
- Bold silhouette and limited interior detail.
- Consistent three-quarter orthographic view for physical subjects; flat frontal symbols for
  abstract actions and status badges.
- Graphite, steel blue, and cyan base palette. Reserve amber for construction, research,
  energy, and warnings.
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
| `unit_construction_drone` | `construction_drone` | Small ducted-fan or quad-rotor utility drone with an articulated construction tool. It must not look armed. |

### Buildings

| Region ID / source file | Active IDs | Visual brief |
|---|---|---|
| `building_town_center` | `town_center`, `town_center_level_2`, `town_center_level_3` | Established civic/industrial headquarters with antenna mast. Reuse initially; level is shown by UI. |
| `building_command_hub` | `command_hub` | Deployable command hub with communications array and visible drone-charging pad. Clearly distinct from town center. |
| `building_material_processor` | `material_processor` | Compact crusher/refinery with two raw-input hoppers and one refined-output conveyor. |
| `building_basic_generator` | `basic_generator` | Compact turbine or alternator housing with cyan energized core. Do not depict solar panels; the active definition is generic. |
| `building_outpost` | `outpost` | Small fortified observation/production post with sensor mast. |

### Resources

| Region ID / source file | Resource ID | State | Visual brief |
|---|---|---|---|
| `resource_wood` | `wood` | enabled | Bundle of cut timber or engineered wood beams. |
| `resource_stone` | `stone` | enabled | Faceted aggregate/stone pile with cool mineral highlights. |
| `resource_gold` | `gold` | enabled | Gold-bearing ore cluster, not coins or currency. |
| `resource_materials` | `materials` | enabled | Refined construction plates, beams, and composite blocks; clearly manufactured. |
| `resource_power` | `power` | enabled/network | Cyan electrical cell combined with a grid-node symbol. This replaces the obsolete `resource_energy` name. |
| `resource_components` | `components` | defined/disabled | Precision electronics and mechanical modules. Generate now so enabling it needs no new art pass. |
| `resource_fuel` | `fuel` | defined/disabled | Sealed industrial fuel canister with restrained amber fluid/energy accent and no lettering. |

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
| `status_queue` | Production/research active | Gear with a circular progress motif. |
| `status_locked` | Prerequisite/rule blocks action | Minimal padlock silhouette. |

These additions reflect the existing construction, health, battery, charger, power, queue,
and disabled-action systems. Their HUD wiring can follow the core atlas integration.

## Set C: reserved future archetypes

Do not wire these into gameplay until definitions exist.

| Region ID / source file | Reserved concept | Visual brief |
|---|---|---|
| `building_storage_silo` | Storage building | Modular cylindrical tanks with loading manifold and access platform. |
| `building_solar_array` | Solar producer | Angled photovoltaic array, compact inverter, cyan electrical trace. |
| `building_power_pole` | Grid connector | Modern compact pylon with insulators and restrained energized arc. |
| `unit_combat_drone` | Armed drone | Agile drone with an unmistakable weapon/sensor silhouette, distinct from the construction drone. |

## Excluded from this pass

- Main-menu text buttons and settings controls.
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
> unmistakable silhouette. Use graphite and steel-blue materials, restrained cyan functional
> lighting, and amber only for construction or warning details. Grounded industrial design,
> minimal fine detail, no scenery, floor, text, letters, numbers, emblem, watermark, UI frame,
> or external drop shadow. Keep at least 8% transparent margin. It must remain recognizable
> at 24x24 and 64x64 pixels. Output a 256x256 RGBA PNG.

### Abstract action or status

> Create one square transparent-background UI pictogram for a modern RTS: **[ACTION OR
> STATUS]**. Use a flat frontal symbol and heavy readable geometry with a graphite/steel-blue
> base, cyan for active energy, and amber or muted red only where appropriate. No text,
> letters, numbers, scenery, watermark, background, or UI frame. Keep at least 12%
> transparent margin and remain clear at 16x16 and 24x24. Output a 256x256 RGBA PNG.

## Consistency review before approval

1. Compare all entity icons together at 64x64.
2. Compare resources at 24x24 on both the dark HUD panel and a light neutral field.
3. Confirm town center and command hub remain distinguishable without labels.
4. Confirm construction and future combat drones cannot be confused.
5. Confirm materials read as processed output while wood and stone read as inputs.
6. Confirm power, battery, and charging remain distinct.
7. Desaturate the contact sheet and verify core silhouettes remain identifiable.
8. Reject false transparency, baked checkerboards, stray pixels, and cropped edges.

## Runtime icon resolution rules

- Entity portrait: resolve from the entity archetype's presentation icon.
- Unit/building/resource recipe: reuse the product definition icon.
- Research recipe: use the linked upgrade definition icon.
- Queue entry: retain the resolved icon ID when enqueued; draw progress and cancellation as
  overlays.
- Homogeneous multi-selection: shared archetype icon plus a UI-rendered count.
- Mixed selection: one icon per archetype; never generate combined icons.
- Missing icon: use `ui/placeholder` and log the semantic ID once.

## Integration work after art approval

1. Add an icon field to unit/building presentation data.
2. Add recipe-icon resolution using the rules above.
3. Instantiate `IconAtlas` in the UI/render layer and add atlas-textured quad rendering.
4. Replace temporary geometry in the build, town-hall, entity-action, and queue panels.
5. Store a resolved icon ID on production queue orders instead of inferring display from a
   legacy production enum.
6. Replace the hard-coded resource text line with enabled definition-driven icon/value pairs.
7. Point resource and upgrade definitions at semantic icon keys.
8. Add the atlas texture to relevant asset-manifest groups.
9. Expand `assets/icons_atlas.json` to 512x512 and populate the approved region IDs.
10. Render hover, disabled, affordability, queued, selected, and team tint through UI state.
11. Run asset-import, definition-registry, renderer-integration, and complete CTest validation.

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
