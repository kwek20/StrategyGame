# V2 compound deposits

Rebuild with Blender:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe' --background --python tools/build_resource_clusters.py
```

These static GLBs compose the existing resource meshes into overlapping lobes, large cores and
scattered smaller piles. Small/medium/large have 22/38/58 base placements inside radii 2.8/3.8/5 world
units; large Scrap additionally receives a broken-frame landmark. Oil uses a separate modular
composition described below rather than these repeated base placements.
Each GLB is one harvest target with the same inventory as its original node, rather than dozens of
new inventories or simultaneously harvestable sources. Their collider and selection bounds cover
the composition. Pieces disappear with the owning deposit when depleted; individual piece removal
and inventory-dependent erosion are future source/node work.

Meshes are joined and the repeated source geometry is reduced before duplication. Current outputs
stay below 15,000 triangles per deposit. This avoids adding one draw command per satellite; it does
not add a new runtime transform cache or saved decoration state.

Sources and licensing are inherited from [the resource source register](../SOURCES.md), including
the Zuckergelee scrap model and Renafox oil pump attribution. No new third-party assets were fetched.
V1 continues to use the original node models and footprints.

The 2026-10-21 appearance pass adds six procedural Scrap silhouettes (beams, plates, broken frames,
pipes, wheels/struts and machinery casings), subdued material palettes, low Oil seep geometry and
Uranium host rock. These are baked into the compounds and do not create additional simulation nodes.
The shared irregular site ground is generated at runtime by `ResourceSiteMap`, not baked as a flat
disc into these models. It follows the existing terrain and is cached independently of the models.

## Oil field kit

`tools/resource_oil_assets.py` creates original wellheads, valve wheels, disconnected pipes,
horizontal tanks and idle/broken pump jacks. Small deposits contain two low wellheads; medium
deposits add one idle pump and a tank; large deposits use a collapsed pump, a tank and two wellheads.
Seven/eleven/sixteen irregular flat seep or crust surfaces provide low scatter. There are no upright
barrels, imported oil meshes, moving machines or new runtime entities in these V2 models.
V1 oil models and their inherited attribution remain unchanged.

Rebuild only Oil with:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe' --background --python tools/build_resource_clusters.py -- oil
```

Outputs contain 1,128 / 1,596 / 2,148 triangles, five shared materials and remain bounded by their
existing 2.8 / 3.8 / 5 selection/collision radii. Ground shading supplies brown stained margins,
dark crust and restrained wet highlights through the existing cached site mask. It requires no
additional texture allocation, water pass or per-frame geometry updates.
