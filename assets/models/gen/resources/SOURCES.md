# Resource model sources

## `scrap_pile.glb`

- Asset: Scrap Metal Pile (Wipe Day Survival Island)
- Source: https://3dassets.dev/assets/wipe-day-survival-island-scrap-pile-110d7f1b
- Direct model: https://cdn.3dassets.dev/assets/27452/v1/model.glb
- License: CC0 1.0 Universal
- Attribution: not required
- Notes: 228 triangles, metre units, generated with AI according to the source page.

## `oil_pump_jack.glb`

- Asset: Oil Pump Jack
- Creator/source credit: Renafox / Get3DModels
- Source: https://www.getglb.com/robot/oil-pump-jack-2/
- Direct model: https://www.get3dmodels.com/download/pump_jack_by_get3dmodels.glb
- License: Creative Commons Attribution
- Attribution: required in the eventual game credits
- Notes: 942 triangles according to the source page.

## `offshore_oil_platform.glb`

- Asset: Offshore platform, main deck (Offshore Oil Rig and Platform)
- Source: https://3dassets.dev/assets/offshore-oil-rig-and-platform-offshore-p-d7fc7e40-starter-scene
- Direct model: https://cdn.3dassets.dev/assets/37061/v1/model.glb
- License: CC0 1.0 Universal
- Attribution: not required
- Notes: 114,080 triangles, metre units, generated with AI according to the source page. This should
  be treated as an extraction building or optimized before becoming a repeated map resource.

## `scrap_pile_sketchfab.glb`

- Asset: Scrap metal (lowpoly)
- Creator: Zuckergelee (`@knorke`)
- Source: https://sketchfab.com/3d-models/scrap-metal-lowpoly-439ed4e66a87433ca0a76b9351f91da7
- License: Creative Commons Attribution 4.0
- Attribution: required in the eventual game credits
- Original archive: `assets/source_archives/resources/scrap_metal_lowpoly_sketchfab.zip`
- Processing: converted from glTF to a self-contained GLB with Blender 5.2, centered horizontally,
  grounded at Z=0, and exported without animations. The downloaded license is retained beside it.

## `offshore_oil_rig_sketchfab.glb`

- Asset: Oil Rig - Low Poly
- Creator: Benkos (`@Benkos`)
- Source: https://sketchfab.com/3d-models/oil-rig-low-poly-9bbb61d0e1434d9f8dba7a0ecf28a2fe
- License: Creative Commons Attribution 4.0
- Attribution: required in the eventual game credits
- Original archive: `assets/source_archives/resources/oil_rig_low_poly_sketchfab.zip`
- Processing: extracted from the nested source RAR, converted to a self-contained GLB with Blender
  5.2, centered horizontally, grounded at Z=0, and exported without animations.

## `uranium_crystal_candidate.glb`

- Asset: Gem cluster (Pickups and Collectibles)
- Source: https://3dassets.dev/assets/pickups-and-collectibles-gem-cluster-976bf9ba
- Direct model: https://cdn.3dassets.dev/assets/19862/v1/model.glb
- License: CC0 1.0 Universal
- Attribution: not required
- Notes: 208 triangles with six crystal spikes growing from a rough ore knot. Generated with AI
  according to the source page. This is a staging candidate only: its blue/purple material should
  be replaced with restrained uranium-mineral colors, its rock base enlarged, and additional small
  spikes added before it becomes the `uranium_deposit` presentation.

## `uranium_crystal_cluster.glb`

- Derived from: `uranium_crystal_candidate.glb`
- License: CC0 1.0 Universal
- Processing: Blender 5.2 via `tools/process_resource_models.py`
- Changes: removed the pickup animations, added two subordinate spikes, enlarged the cluster to a
  useful RTS-node scale, centered and grounded it, changed the base to dark mineral rock, and
  replaced the blue/purple materials with two green crystal materials with restrained emission.
