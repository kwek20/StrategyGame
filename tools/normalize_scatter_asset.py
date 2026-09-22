"""Normalize a source model into a game-ready, metre-scaled GLB.

Run through Blender:
  blender --background --python tools/normalize_scatter_asset.py -- input.gltf output.glb 1.0 20000
"""

import sys
from pathlib import Path

import bpy
from mathutils import Vector


def arguments():
    values = sys.argv[sys.argv.index("--") + 1 :]
    if len(values) not in (4, 5):
        raise RuntimeError("Expected: input output target_max_dimension max_triangles [rotation_x]")
    return (Path(values[0]), Path(values[1]), float(values[2]), int(values[3]),
            float(values[4]) if len(values) == 5 else 0.0)


source, destination, target_dimension, triangle_budget, rotation_x = arguments()
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
if source.suffix.lower() in (".gltf", ".glb"):
    bpy.ops.import_scene.gltf(filepath=str(source.resolve()))
elif source.suffix.lower() == ".fbx":
    bpy.ops.import_scene.fbx(filepath=str(source.resolve()))
elif source.suffix.lower() == ".obj":
    bpy.ops.wm.obj_import(filepath=str(source.resolve()))
else:
    raise RuntimeError(f"Unsupported source format: {source.suffix}")

meshes = [item for item in bpy.context.scene.objects if item.type == "MESH"]
if not meshes:
    raise RuntimeError(f"No mesh objects imported from {source}")
for item in meshes:
    item.data.calc_loop_triangles()
    world_transform = item.matrix_world.copy()
    item.parent = None
    item.matrix_world = world_transform
    item.rotation_euler.x += rotation_x


def bounds(objects):
    points = [item.matrix_world @ Vector(corner) for item in objects for corner in item.bound_box]
    return (
        Vector((min(point.x for point in points), min(point.y for point in points), min(point.z for point in points))),
        Vector((max(point.x for point in points), max(point.y for point in points), max(point.z for point in points))),
    )


minimum, maximum = bounds(meshes)
largest_dimension = max(maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z)
if largest_dimension <= 0.0:
    raise RuntimeError(f"Degenerate bounds in {source}")
normalization = target_dimension / largest_dimension
for item in meshes:
    item.scale *= normalization
    item.location *= normalization

# Apply transforms, then place the lowest model vertex exactly on the ground plane.
bpy.ops.object.select_all(action="DESELECT")
for item in meshes:
    item.select_set(True)
    bpy.context.view_layer.objects.active = item
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
minimum, maximum = bounds(meshes)
for item in meshes:
    item.location.z -= minimum.z
bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)

triangle_count = sum(len(item.data.loop_triangles) for item in meshes)
if triangle_count > triangle_budget:
    ratio = triangle_budget / triangle_count
    for item in meshes:
        if len(item.data.polygons) < 20:
            continue
        modifier = item.modifiers.new(name="game_lod", type="DECIMATE")
        modifier.ratio = ratio
        modifier.use_collapse_triangulate = True
        bpy.context.view_layer.objects.active = item
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        item.data.calc_loop_triangles()

destination.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action="DESELECT")
for item in meshes:
    item.select_set(True)
bpy.ops.export_scene.gltf(
    filepath=str(destination.resolve()),
    export_format="GLB",
    use_selection=True,
    export_apply=True,
    export_materials="EXPORT",
    export_image_format="AUTO",
)

minimum, maximum = bounds(meshes)
triangle_count = sum(len(item.data.loop_triangles) for item in meshes)
print(
    f"NORMALIZED {source.name}: triangles={triangle_count} "
    f"size=({maximum.x-minimum.x:.3f}, {maximum.y-minimum.y:.3f}, {maximum.z-minimum.z:.3f})"
)
