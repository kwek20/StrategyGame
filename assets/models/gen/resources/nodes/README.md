# Individual V2 resource nodes

These 12 models dress small, individually harvestable targets composed at field scale by generation
revision 7. Gameplay capacities and collision radii live in `assets/gameplay/resource_nodes.json`;
field recipes select the allowed `harvestNodes` in `assets/gameplay/resource_fields.json`.

Rebuild models without rewriting definitions:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe' --background --python tools/build_resource_nodes.py
```

Scrap silhouettes and the Oil equipment kit are original procedural geometry. Uranium derives
from the existing mineral assets; see `../SOURCES.md` for provenance. The original compound assets
remain available but are no longer used for generated V2 field targets. V1 presentations are unchanged.

All nodes are bounded inside 94% of their authored collision radius. A node carries its own supply;
its visual fragments do not have separate inventories. The renderer instances repeated models and
updates the owning field's buffer range on depletion. Individual sub-fragment erosion is not implemented.

V2 Oil seeps have no model: the terrain shader paints their wet footprint, avoiding coplanar polygons
and clipping on slopes. Their gameplay entities retain their supply and selection footprint. Wellhead
models also omit flat spill geometry. Rebuild only Oil equipment with `-- --oil-only` after the script
argument above. The original V1 seep assets remain unchanged.
