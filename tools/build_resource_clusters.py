"""Bake V2 compound deposits from the existing licensed resource meshes.

Run from any directory with Blender --background --python tools/build_resource_clusters.py.
The baked meshes are static: one harvest target/collider/inventory per compound deposit.
Joining meshes keeps satellite count out of the renderer's draw-command count.
"""
import math
import random
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
from resource_oil_assets import compose_oil
OUTPUT = ROOT / "assets/models/gen/resources/clusters"
SOURCES = {
    "scrap": [("scrap_pile_small", 1.5), ("scrap_pile_medium", 2.4), ("scrap_pile_sketchfab", 3.6)],
    "oil": [],  # Original modular kit in resource_oil_assets.py.
    "uranium": [("uranium_crystal_small", 1.5), ("uranium_crystal_cluster", 2.4), ("uranium_crystal_large", 3.2)],
}


def salvage_shapes():
    """Six distinct low salvage silhouettes, reused inside the baked compounds."""
    palette = []
    for name, color in (("weathered_steel", (.24, .27, .26)),
                        ("muted_rust", (.27, .15, .085)), ("worn_paint", (.14, .20, .18))):
        mat = bpy.data.materials.new(name)
        mat.diffuse_color = (*color, 1)
        mat.use_nodes = True
        shader = mat.node_tree.nodes.get("Principled BSDF")
        shader.inputs['Base Color'].default_value = (*color, 1)
        shader.inputs['Roughness'].default_value = .88
        palette.append(mat)
    shapes = []
    for variant in range(6):
        objects = []
        def box(position, size, angle=0, color=0):
            bpy.ops.mesh.primitive_cube_add(size=1, location=position)
            obj = bpy.context.object
            obj.scale = size
            obj.rotation_euler.z = angle
            obj.data.materials.append(palette[color])
            objects.append(obj)
        if variant == 0:  # irregular beam bundle
            for i in range(6):
                box((0, (i-3)*.14, .08+(i%2)*.12), (1.6-i*.09, .12, .13), (i-2)*.13, i%3)
        elif variant == 1:  # layered torn plates
            for i in range(5):
                box(((i%2)*.25-.1, (i-2)*.15, .06+i*.07), (.95, .7, .055), i*.6, i%3)
        elif variant == 2:  # collapsed frame, broken uprights
            for y in (-.45,.45):
                box((0,y,.1),(1.8,.10,.13),.08)
                for x in (-.7,.7):
                    box((x,y,.35),(.12,.12,.7 if x<0 else .4),.2,1)
            box((0,0,.1),(.13,1,.13),.15)
            box((0,0,.42),(1.6,.1,.1),.35,1)
        elif variant == 3:  # short, broken pipes
            for i in range(4):
                bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=.13, depth=1.3-i*.17,
                                                   location=(0,(i-2)*.27,.17+(i%2)*.08),
                                                   rotation=(0,math.pi/2,.12*i))
                obj=bpy.context.object; obj.data.materials.append(palette[i%2]); objects.append(obj)
        elif variant == 4:  # discarded wheels and struts
            for x in (-.4,.3):
                bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=.32,depth=.13,location=(x,.1,.1))
                obj=bpy.context.object; obj.data.materials.append(palette[0]); objects.append(obj)
            box((0,-.3,.16),(1.3,.13,.16),.3,1)
        else:  # stripped machinery casing
            box((-.25,0,.23),(.65,.65,.45),.15,2)
            box((.35,.15,.15),(.65,.4,.3),-.4,0)
            box((0,0,.5),(.17,.23,.5),.2,1)
            box((.2,-.4,.05),(1.0,.45,.07),-.3,0)
        bpy.context.view_layer.update()
        shapes.append(objects)
    return shapes


def bounds(objects):
    return [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]


def library(family):
    result = []
    for source_index, (source, diameter) in enumerate(SOURCES[family]):
        bpy.ops.object.select_all(action="DESELECT")
        bpy.ops.import_scene.gltf(filepath=str(OUTPUT.parent / f"{source}.glb"))
        objects = [obj for obj in bpy.context.selected_objects if obj.type == "MESH"]
        for obj in objects:
            world = obj.matrix_world.copy()
            obj.parent = None
            obj.matrix_world = world
        points = bounds(objects)
        low = Vector(tuple(min(p[axis] for p in points) for axis in range(3)))
        high = Vector(tuple(max(p[axis] for p in points) for axis in range(3)))
        scale = diameter / max(high.x - low.x, high.y - low.y)
        center = (low + high) * 0.5
        for obj in objects:
            obj.location = (obj.location - Vector((center.x, center.y, low.z))) * scale
            obj.scale *= scale
        triangles = sum(sum(len(p.vertices) - 2 for p in obj.data.polygons) for obj in objects)
        budget = (220, 650, 2000)[source_index]
        if triangles > budget:
            for obj in objects:
                bpy.context.view_layer.objects.active = obj
                modifier = obj.modifiers.new("Repeated deposit mesh budget", "DECIMATE")
                modifier.ratio = budget / triangles
                bpy.ops.object.modifier_apply(modifier=modifier.name)
        bpy.context.view_layer.update()
        result.append(objects)
    if family == "scrap":
        # Old procedural piles were dominated by bright rust; retain restrained accents.
        for obj in result[0] + result[1]:
            for material_index, mat in enumerate(obj.data.materials):
                color = ((.22,.25,.24,1),(.27,.15,.085,1),(.15,.21,.19,1))[material_index%3]
                mat.diffuse_color=color
                if mat.use_nodes:
                    shader=mat.node_tree.nodes.get('Principled BSDF')
                    if shader: shader.inputs['Base Color'].default_value=color
        result.extend(salvage_shapes())
    if family == "uranium":
        for group in result:
            for obj in group:
                for mat in obj.data.materials:
                    mat.diffuse_color=(.28,.33,.12,1)
                    if mat.use_nodes:
                        shader=mat.node_tree.nodes.get('Principled BSDF')
                        if shader:
                            for link in list(shader.inputs['Base Color'].links):
                                mat.node_tree.links.remove(link)
                            shader.inputs['Base Color'].default_value=(.28,.33,.12,1)
                            if shader.inputs.get('Emission Strength'): shader.inputs['Emission Strength'].default_value=0
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=.65)
        obj=bpy.context.object; obj.scale=(1.2,.85,.65); obj.location.z=.4
        mat=bpy.data.materials.new('mineral_host_rock'); mat.diffuse_color=(.24,.26,.25,1)
        mat.use_nodes=True
        shader=mat.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value=(.24,.26,.25,1)
        shader.inputs['Roughness'].default_value=1
        obj.data.materials.append(mat); result.append([obj])
        bpy.context.view_layer.update()
    return result


def build(family, size, radius, count, seed):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    templates = [] if family == 'oil' else library(family)
    rng = random.Random(seed)
    pieces = []
    if family == 'oil':
        pieces = compose_oil(size, radius, seed)
    # Overlapping lobes avoid circular ore discs or a regular ring of equally large piles.
    lobes = [Vector((-radius * .28, -radius * .12, 0)),
             Vector((radius * .28, radius * .12, 0)), Vector((0, radius * .28, 0))]
    for index in range(0 if family == 'oil' else count):
        core = index < 3
        kind = (2 if size == "large" and index == 0 else 1) if core else 0
        if family == "scrap" and index == 0:
            kind = 2
        elif family == "scrap":
            kind = 1 if core else (3 + rng.randrange(6))
        if family == "uranium" and not core and index % 4 != 0:
            kind = 3
        template = templates[kind]
        angle = rng.random() * math.tau
        distance = radius * (.32 if core else .82) * math.sqrt(rng.random())
        position = lobes[index % len(lobes)] + Vector((math.cos(angle) * distance, math.sin(angle) * distance, 0))
        rotation = rng.random() * math.tau
        scale = rng.uniform(.85, 1.2) if core else rng.uniform(.50, 1.05)
        for original in template:
            obj = original.copy()
            obj.data = original.data
            bpy.context.collection.objects.link(obj)
            # Compose in world space so imported parents/axes cannot displace satellites.
            obj.matrix_world = (Matrix.Translation(position) @ Matrix.Rotation(rotation, 4, "Z") @
                                Matrix.Scale(scale, 4) @ original.matrix_world)
            pieces.append(obj)
    if family == 'scrap' and size == 'large':
        # One tall, broken industrial frame gives a large field a readable landmark.
        for original in templates[5]:
            obj=original.copy(); obj.data=original.data
            bpy.context.collection.objects.link(obj)
            obj.matrix_world=Matrix.Diagonal((1.8,1.5,2.8,1.0)) @ original.matrix_world
            pieces.append(obj)
    for group in templates:
        for obj in group:
            bpy.data.objects.remove(obj, do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in pieces:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = pieces[0]
    bpy.ops.object.join()
    combined = bpy.context.object
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Bound the actual geometry by the gameplay/selection radius, not an approximate AABB.
    maximum = max(math.hypot(v.co.x, v.co.y) for v in combined.data.vertices)
    factor = radius * .95 / maximum
    minimum_z = min(v.co.z for v in combined.data.vertices)
    for vertex in combined.data.vertices:
        vertex.co = Vector((vertex.co.x * factor, vertex.co.y * factor, (vertex.co.z - minimum_z) * factor))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(OUTPUT / f"{family}_{size}.glb"), export_format="GLB",
                             use_selection=True, export_apply=True, export_animations=False)
    triangles = sum(len(poly.vertices) - 2 for poly in combined.data.polygons)
    height = max(v.co.z for v in combined.data.vertices)
    print(f"CLUSTER {family}/{size}: radius={radius}, height={height:.3f}, components={len(pieces)}, triangles={triangles}", flush=True)


if __name__ == '__main__':
    for family_index, family in enumerate(SOURCES):
        if '--' in sys.argv and family not in sys.argv[sys.argv.index('--') + 1:]:
            continue
        for size_index, (size, radius, count) in enumerate((('small', 2.8, 22), ('medium', 3.8, 38), ('large', 5.0, 58))):
            build(family, size, radius, count, 9137 + family_index * 100 + size_index)
