"""Render a simple studio preview of a glTF/GLB asset with Blender."""

import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector


source = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
destination = Path(sys.argv[sys.argv.index("--") + 2]).resolve()

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(source))
meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
corners = [obj.matrix_world @ Vector(corner) for obj in meshes for corner in obj.bound_box]
minimum = Vector((min(point.x for point in corners), min(point.y for point in corners),
                  min(point.z for point in corners)))
maximum = Vector((max(point.x for point in corners), max(point.y for point in corners),
                  max(point.z for point in corners)))
center = (minimum + maximum) * 0.5
size = max(maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z)

bpy.ops.mesh.primitive_plane_add(size=size * 5.0, location=(center.x, center.y, minimum.z - 0.01))
ground = bpy.context.object
ground_material = bpy.data.materials.new("preview_ground")
ground_material.diffuse_color = (0.055, 0.065, 0.055, 1.0)
ground.data.materials.append(ground_material)

bpy.ops.object.camera_add(location=(center.x + size * 1.65,
                                    center.y - size * 2.15,
                                    center.z + size * 1.35))
camera = bpy.context.object
direction = center - camera.location
camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
camera.data.lens = 55
bpy.context.scene.camera = camera

for location, energy, radius in (
    ((center.x - size, center.y - size, center.z + size * 2.5), 950, size * 2.0),
    ((center.x + size * 1.8, center.y, center.z + size), 500, size * 1.5),
):
    bpy.ops.object.light_add(type="AREA", location=location)
    light = bpy.context.object
    light.data.energy = energy
    light.data.shape = "DISK"
    light.data.size = radius
    light.rotation_euler = (math.radians(18), 0.0, math.radians(145))

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x = 640
scene.render.resolution_y = 640
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(destination)
scene.render.film_transparent = False
if scene.world is None:
    scene.world = bpy.data.worlds.new("preview_world")
scene.world.color = (0.018, 0.022, 0.018)
bpy.ops.render.render(write_still=True)
