# Refined V2: engine captures

The [terrain oil spills](oil-terrain-spills/README.md) replace clipping polygons with soft wet
stains painted directly into the terrain.

The [connected resource rubble](resource-links/README.md) joins nearby nodes within each field
with sparse decorative fragments.

The [sparse resource fringe](resource-fringe/README.md) adds tiny decorative fragments around a
minority of harvest nodes.

The latest [Oil ground blend](oil-ground-blend/README.md) shows lighter staining and cracked earth
blended with the existing terrain.

The latest resource system is shown in the [field-node gallery](field-nodes/README.md): many
individually harvestable targets composed at field scale, generation revision 7.

See the [oil-field-kit review](oil-field-kit/README.md) for the latest Oil assets, including
starting supplies, expansion close-ups and gameplay zoom.

These are historical captures from the earlier refinement. See the
[resource-site gallery](resource-sites/README.md) for the latest appearance pass, the
[cliffs-and-clusters gallery](cliffs-and-clusters/README.md) for the preceding revision, or the
[seeded-landform gallery](seeded-landforms/README.md) for the preceding 2026-10-14 implementation.

These are actual renderer captures, not generated concept illustrations. They show seed 7,
Small (240 by 240 units), two players, and the recipes in
[terrain/v2.json](../../../../assets/gameplay/terrain/v2.json).

The overview images show geometry and water. The Central Water play views additionally include
starting buildings, resources and visual vegetation. Current recipes use 1.5-unit cells and
half-unit elevation steps, mixed broadleaf/conifer forest groups and separate lake-bank widths. Routes reserve clear traversable land; no
roads are generated. V1 remains selectable separately.

![Central Water whole map](central-water-play-5.png)

![Central Water in play](central-water-play-0.png)

![Central Water map edge](central-water-play-2.png)

| Layout | Terrain overview |
|---|---|
| Open Plains | [Capture](open_plains.png) |
| Rolling Hills | [Capture](rolling_hills.png) |
| Central Hill | [Capture](central_hill.png) |
| Central Water | [Capture](central_water.png) |
| Continental (dry V2 recipe) | [Capture](continental.png) |

See [implementation and verification](../../../TERRAIN_GENERATION_V2_IMPLEMENTATION.md) for current
scope and limitations. These captures verify the current geometry direction; they do not imply
finished salvage art, water effects, Shore buildings or naval gameplay.

## Comparison

The previous six-unit terrain cells produced large blocky banks and peaks:

![Before refinement](central-water-before-refinement.png)

The current play-view captures use the same seed, map size and camera positions. Terrain is
articulated more finely; the basin has shallow margins and small islets; forest groups contain
both broadleaf and original conifer models. Resource field inventories are unchanged.
