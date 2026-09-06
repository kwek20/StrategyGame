# Modern RTS Icon Generation Brief

## Purpose

Generate a cohesive icon set for the game's selection panels, resource HUD, construction menu, production queues, and upgrade menus. The current definitions reference `ui/placeholder`; generated icons should replace those references without changing gameplay IDs.

## Shared art direction

- Square PNG icons with true transparent backgrounds (recommended 256x256 source, rendered down to 48–64 px).
- Modern near-future military/industrial RTS aesthetic; clear silhouettes and minimal detail.
- Consistent three-quarter orthographic view, centered subject, identical scale and lighting.
- Steel blue/cyan base palette with amber highlights; team-color tint must remain possible.
- No text, letters, numbers, watermark, scenery, drop shadow outside the silhouette, or baked UI frame.
- Preserve strong contrast at small size and readable shape when desaturated.

## Required assets and filenames

Store final files under `assets/icons/` using these stable names:

### Units

| File | Definition IDs | Visual brief |
|---|---|---|
| `worker.png` | `worker` | Human field engineer with compact tool/equipment silhouette. |
| `construction_drone.png` | `construction_drone` | Small flying construction drone with rotors/thrusters and utility arm. |

### Buildings

| File | Definition IDs | Visual brief |
|---|---|---|
| `town_hall.png` | `town_center`, `command_hub` | Futuristic command center with antenna and central blue core. |
| `material_processor.png` | `material_processor` | Compact industrial processor with input hoppers and glowing output core. |
| `storage_silo.png` | future storage-silo archetype | Cylindrical storage tank with gauge and access platform. |
| `solar_panels.png` | future solar-generator archetype | Folded solar panel array with support frame and energy glow. |
| `electricity_pole.png` | future power-pole archetype | Utility pole with insulators and a small energized transmission arc. |

### Upgrades

| File | Definition IDs | Visual brief |
|---|---|---|
| `upgrade_speed.png` | speed/training-speed upgrades | Motion chevrons combined with a compact lightning accent. |
| `upgrade_level_up.png` | town-hall level upgrades | Upward chevron/arrow with a single bright star or tier marker. |

### Resources

| File | Resource IDs | Visual brief |
|---|---|---|
| `resource_wood.png` | `wood` | Three clean timber logs or a stylized plank bundle. |
| `resource_stone.png` | `stone` | Faceted stone chunk with a cool mineral highlight. |
| `resource_gold.png` | `gold` | Gold ingot/ore cluster with warm metallic highlight. |
| `resource_materials.png` | `materials` | Refined composite plate/crate representing processed construction material. |
| `resource_energy.png` | future energy resource | Bright cyan energy cell or battery symbol, no lettering. |

## Generation prompt template

> Create one square transparent-background modern RTS game icon: **[SUBJECT]**. Use a bold, readable silhouette, three-quarter orthographic view, steel-blue and cyan materials with restrained amber highlights, subtle internal glow, consistent centered scale, no text, no letters, no numbers, no watermark, no background, no UI frame. Designed to remain legible at 64x64 pixels.

Generate each asset separately so alpha, filename, and visual identity can be validated independently. Do not generate a sprite sheet as the production source.

## Integration checklist

1. Place generated PNGs in `assets/icons/`.
2. Add each file to `assets/asset_manifest.json` under the UI/icon manifest.
3. Change `icon` fields in `resources.json` and `upgrades.json` from `ui/placeholder` to the matching icon key.
4. Add icon keys to the relevant unit/building definitions when presentation data supports them.
5. Add localization only for names/tooltips; icons themselves contain no text.
6. Run asset-import, definition-registry, renderer-integration, and full CTest validation.

## Current project alignment

The active IDs are `worker`, `construction_drone`, `town_center`, `command_hub`, `material_processor`, `wood`, `stone`, `gold`, and `materials`. Storage silo, solar panels, electricity pole, and energy are reserved for later definitions and should not be wired into gameplay until their archetypes/resources exist.
