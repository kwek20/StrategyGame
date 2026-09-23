"""Build normalized resource-node GLBs from licensed source assets.

Run with Blender in background mode from the repository root:
  blender --background --python tools/process_resource_variants.py
"""

import math
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/sources/resources"
OUTPUT = ROOT / "assets/models/gen/resources"


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_gltf(path):
    bpy.ops.import_scene.gltf(filepath=str(path))
    # Work in world space after import. Some glTFs wrap meshes in transformed node parents;
    # retaining those parents would make later grounding offsets operate in an unexpected axis.
    for obj in list(bpy.context.scene.objects):
        if obj.type != "MESH" or obj.parent is None:
            continue
        world = obj.matrix_world.copy()
        obj.parent = None
        obj.matrix_world = world
    for animation in list(bpy.data.actions):
        bpy.data.actions.remove(animation)


def meshes():
    return [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]


def world_bounds(objects):
    points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    minimum = Vector((min(point.x for point in points),
                      min(point.y for point in points),
                      min(point.z for point in points)))
    maximum = Vector((max(point.x for point in points),
                      max(point.y for point in points),
                      max(point.z for point in points)))
    return minimum, maximum


def normalize(objects, target_footprint):
    minimum, maximum = world_bounds(objects)
    size = maximum - minimum
    scale = target_footprint / max(size.x, size.y)
    center = (minimum + maximum) * 0.5
    for obj in objects:
        obj.location.x = (obj.location.x - center.x) * scale
        obj.location.y = (obj.location.y - center.y) * scale
        obj.location.z = (obj.location.z - minimum.z) * scale
        obj.scale *= scale
    bpy.context.view_layer.update()


def material(name, color, metallic=0.0, roughness=0.65, emission=None):
    result = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    result.diffuse_color = (*color, 1.0)
    result.use_nodes = True
    shader = result.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1.0)
    shader.inputs["Metallic"].default_value = metallic
    shader.inputs["Roughness"].default_value = roughness
    if emission:
        emission_input = shader.inputs.get("Emission Color") or shader.inputs.get("Emission")
        if emission_input:
            emission_input.default_value = (*emission, 1.0)
        strength = shader.inputs.get("Emission Strength")
        if strength:
            strength.default_value = 0.35
    return result


def replace_materials(objects, replacement):
    for obj in objects:
        obj.data.materials.clear()
        obj.data.materials.append(replacement)


def export(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB",
                              export_apply=True, export_animations=False)


def convert(source, output, footprint):
    reset()
    import_gltf(source)
    objects = meshes()
    normalize(objects, footprint)
    export(output)


def build_medium_scrap():
    reset()
    import_gltf(SOURCE / "scrap_medium_source.glb")
    rust = material("salvage_rust", (0.34, 0.12, 0.045), metallic=0.55, roughness=0.78)
    steel = material("salvage_steel", (0.26, 0.31, 0.33), metallic=0.72, roughness=0.62)
    tarp = material("salvage_tarp", (0.10, 0.13, 0.09), roughness=0.94)
    for obj in meshes():
        original = obj.data.materials[0].name.lower() if obj.data.materials else ""
        if "cloth" in original:
            replace_materials([obj], tarp)
        elif "metal" in original:
            replace_materials([obj], steel)
        else:
            replace_materials([obj], rust)
    normalize(meshes(), 2.35)
    export(OUTPUT / "scrap_pile_medium.glb")


def build_oil_seep():
    reset()
    oil = material("crude_oil", (0.018, 0.024, 0.018), metallic=0.05,
                   roughness=0.18)
    ground = material("oil_soaked_ground", (0.055, 0.045, 0.028), roughness=0.9)

    segments = 20
    radii = [1.0, 0.86, 1.08, 0.91, 1.04, 0.82, 1.12, 0.89, 1.03, 0.84,
             1.09, 0.93, 1.00, 0.87, 1.06, 0.90, 1.11, 0.85, 1.02, 0.92]
    vertices = [(0.0, 0.0, 0.035)]
    for index, radius in enumerate(radii):
        angle = math.tau * index / segments
        vertices.append((math.cos(angle) * radius,
                         math.sin(angle) * radius * 0.72, 0.035))
    faces = [(0, index + 1, ((index + 1) % segments) + 1) for index in range(segments)]
    mesh = bpy.data.meshes.new("oil_seep_pool")
    mesh.from_pydata(vertices, [], faces)
    pool = bpy.data.objects.new("oil_seep_pool", mesh)
    bpy.context.collection.objects.link(pool)
    pool.data.materials.append(oil)

    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.34, depth=0.12,
                                        location=(0.0, 0.0, 0.0))
    rim = bpy.context.object
    rim.name = "oil_soaked_vent"
    rim.scale.y = 0.78
    rim.data.materials.append(ground)

    # The CC0 Drops mesh supplies the airborne globules. A low-poly splash crown keeps the
    # deposit readable from the RTS camera without requiring an animated fluid simulation.
    import_gltf(SOURCE / "drops/glTF/scene.gltf")
    drop_objects = [obj for obj in meshes() if obj not in {pool, rim}]
    drop = drop_objects[0]
    drop.name = "oil_droplet_main"
    drop.rotation_euler.x = math.radians(90.0)
    bpy.context.view_layer.objects.active = drop
    drop.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    drop.scale *= 0.28 / drop.dimensions.z
    bpy.context.view_layer.update()
    minimum, maximum = world_bounds([drop])
    center = (minimum + maximum) * 0.5
    drop.location += Vector((-center.x, -center.y, 0.62 - minimum.z))
    replace_materials([drop], oil)
    for index, location in enumerate(((-0.23, 0.08, 0.42), (0.20, -0.10, 0.34))):
        copy = drop.copy()
        copy.data = drop.data.copy()
        copy.name = f"oil_droplet_{index + 1}"
        copy.scale *= 0.55 if index == 0 else 0.42
        copy.location = location
        bpy.context.collection.objects.link(copy)
    for index, angle in enumerate((0.0, math.pi * 0.5, math.pi, math.pi * 1.5)):
        location = (math.cos(angle) * 0.18, math.sin(angle) * 0.14, 0.24)
        bpy.ops.mesh.primitive_cone_add(vertices=8, radius1=0.13, radius2=0.025,
                                        depth=0.48, location=location)
        splash = bpy.context.object
        splash.name = f"oil_splash_{index + 1}"
        splash.rotation_euler = (math.sin(angle) * 0.34,
                                 -math.cos(angle) * 0.34, angle)
        splash.data.materials.append(oil)
    normalize(meshes(), 2.0)
    export(OUTPUT / "oil_seep_small.glb")


def build_small_uranium():
    reset()
    import_gltf(OUTPUT / "uranium_crystal_cluster.glb")
    for obj in list(meshes()):
        if obj.name not in {"body/iron", "uranium_secondary_spikes"}:
            bpy.data.objects.remove(obj, do_unlink=True)
    uranium = material("uranium_crystal", (0.18, 0.78, 0.08), roughness=0.28,
                       emission=(0.12, 0.55, 0.04))
    for obj in meshes():
        if "spike" in obj.name:
            replace_materials([obj], uranium)
    normalize(meshes(), 1.25)
    export(OUTPUT / "uranium_crystal_small.glb")


def build_large_uranium():
    reset()
    import_gltf(SOURCE / "uranium_large_source.glb")
    uranium = material("uranium_crystal", (0.16, 0.82, 0.07), roughness=0.24,
                       emission=(0.10, 0.62, 0.03))
    rock = material("uranium_host_rock", (0.10, 0.14, 0.08), roughness=0.92)
    for obj in list(meshes()):
        if "crystal-spire" not in obj.name:
            bpy.data.objects.remove(obj, do_unlink=True)
    spire = meshes()[0]
    spire.scale *= 2.15
    spire.location.z = 0.58
    replace_materials([spire], uranium)
    for index, (location, scale) in enumerate((
        ((-0.52, 0.02, 0.34), (1.25, 1.05, 0.48)),
        ((0.48, -0.18, 0.31), (1.15, 0.92, 0.44)),
        ((0.03, 0.42, 0.28), (1.08, 0.88, 0.40)),
    )):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=1.0, location=location)
        host = bpy.context.object
        host.name = f"uranium_host_rock_{index + 1}"
        host.scale = scale
        host.data.materials.append(rock)
    normalize(meshes(), 3.35)
    export(OUTPUT / "uranium_crystal_large.glb")


convert(SOURCE / "scrap_small_source.glb", OUTPUT / "scrap_pile_small.glb", 1.55)
build_medium_scrap()
convert(SOURCE / "oil_barrel_source.glb", OUTPUT / "oil_barrel_medium.glb", 1.35)
build_oil_seep()
build_small_uranium()
build_large_uranium()
