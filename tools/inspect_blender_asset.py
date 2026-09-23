import bpy
import json
import sys


source = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=source)

objects = []
for obj in bpy.context.scene.objects:
    if obj.type != "MESH":
        continue
    objects.append({
        "name": obj.name,
        "vertices": len(obj.data.vertices),
        "polygons": len(obj.data.polygons),
        "materials": [slot.material.name if slot.material else None for slot in obj.material_slots],
        "dimensions": list(obj.dimensions),
        "location": list(obj.location),
    })

materials = []
for material in bpy.data.materials:
    materials.append({
        "name": material.name,
        "diffuse": list(material.diffuse_color),
        "nodes": [node.name for node in material.node_tree.nodes] if material.use_nodes else [],
    })

print("ASSET_INSPECTION=" + json.dumps({
    "objects": objects,
    "materials": materials,
    "animations": [action.name for action in bpy.data.actions],
}))
