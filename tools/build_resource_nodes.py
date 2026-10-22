"""Bake individual V2 harvest targets using the authored field/node definitions.

Run with Blender --background --python tools/build_resource_nodes.py.
Field inventories remain the budget; these small targets replace compound mini-fields.
"""
import json
import math
from pathlib import Path
import sys
import argparse

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
from build_resource_clusters import salvage_shapes, library
from resource_oil_assets import compose_oil

OUTPUT = ROOT / 'assets/models/gen/resources/nodes'
OUTPUT.mkdir(parents=True, exist_ok=True)


def export(objects, name, radius):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    obj = bpy.context.object
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    maximum = max(math.hypot(v.co.x, v.co.y) for v in obj.data.vertices)
    scale = radius * .94 / maximum
    minimum = min(v.co.z for v in obj.data.vertices)
    for vertex in obj.data.vertices:
        vertex.co = Vector((vertex.co.x*scale, vertex.co.y*scale, (vertex.co.z-minimum)*scale))
    # Flat seep surfaces still need separation from the terrain surface.
    if max(v.co.z for v in obj.data.vertices) < .03:
        for vertex in obj.data.vertices:
            vertex.co.z += .035
    bpy.ops.export_scene.gltf(filepath=str(OUTPUT / f'{name}.glb'), export_format='GLB',
                             use_selection=True, export_apply=True, export_animations=False)
    return max(.25, max(v.co.z for v in obj.data.vertices))


nodes_path = ROOT / 'assets/gameplay/resource_nodes.json'
fields_path = ROOT / 'assets/gameplay/resource_fields.json'
nodes = json.loads(nodes_path.read_text())['resourceNodes']
fields = json.loads(fields_path.read_text())['resourceFields']
names = [name for field in fields.values()
         for name in field['terrainV2Composition']['harvestNodes']]
parser = argparse.ArgumentParser()
parser.add_argument('--oil-only', action='store_true')
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
for name in names:
    node = nodes[name]
    family = node['resourceType']
    variant = name.removeprefix(f'v2_{family}_')
    if name == 'v2_oil_seep' or (args.oil_only and family != 'oil'):
        continue
    radius = node['collisionRadius']
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if family == 'scrap':
        groups = salvage_shapes()
        objects = groups[['beams', 'plates', 'frame', 'pipes', 'wheels', 'casing'].index(variant)]
    elif family == 'oil':
        objects = compose_oil('node_'+variant, radius, 9237+names.index(name))
    else:
        groups = library('uranium')
        objects = groups[3] + groups[0 if variant != 'vein' else 1]
        for obj in objects:
            if obj in groups[3]:
                obj.scale *= 1.35 if variant == 'rock' else 1
    height = export(objects, name, radius)
    print(f'NODE {name}: radius={radius}, height={height:.3f}', flush=True)
