"""Normalize downloaded resource models and export self-contained GLBs with Blender."""

import argparse
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector


def clear_scene() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_gltf(path: Path) -> None:
    bpy.ops.import_scene.gltf(filepath=str(path))


def mesh_objects() -> list[bpy.types.Object]:
    return [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]


def clear_animation() -> None:
    for obj in bpy.context.scene.objects:
        obj.animation_data_clear()
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)


def center_and_ground() -> None:
    objects = mesh_objects()
    if not objects:
        raise RuntimeError("Imported resource contains no mesh objects")
    corners = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    minimum = Vector((min(point.x for point in corners),
                      min(point.y for point in corners),
                      min(point.z for point in corners)))
    maximum = Vector((max(point.x for point in corners),
                      max(point.y for point in corners),
                      max(point.z for point in corners)))
    offset = Vector((-(minimum.x + maximum.x) * 0.5,
                     -(minimum.y + maximum.y) * 0.5,
                     -minimum.z))
    for obj in objects:
        obj.location += offset


def principled(material: bpy.types.Material) -> bpy.types.ShaderNodeBsdfPrincipled:
    material.use_nodes = True
    node = next((item for item in material.node_tree.nodes
                 if item.type == "BSDF_PRINCIPLED"), None)
    if node is None:
        node = material.node_tree.nodes.new("ShaderNodeBsdfPrincipled")
        output = next((item for item in material.node_tree.nodes
                       if item.type == "OUTPUT_MATERIAL"), None)
        if output is None:
            output = material.node_tree.nodes.new("ShaderNodeOutputMaterial")
        material.node_tree.links.new(node.outputs["BSDF"], output.inputs["Surface"])
    return node


def set_material(material_name: str, color: tuple[float, float, float, float],
                 metallic: float, roughness: float, emission: tuple[float, float, float, float],
                 emission_strength: float) -> None:
    material = bpy.data.materials.get(material_name)
    if material is None:
        raise RuntimeError(f"Expected material '{material_name}' was not imported")
    material.diffuse_color = color
    shader = principled(material)
    shader.inputs["Base Color"].default_value = color
    shader.inputs["Metallic"].default_value = metallic
    shader.inputs["Roughness"].default_value = roughness
    emission_input = shader.inputs.get("Emission Color") or shader.inputs.get("Emission")
    if emission_input:
        emission_input.default_value = emission
    strength_input = shader.inputs.get("Emission Strength")
    if strength_input:
        strength_input.default_value = emission_strength


def prepare_uranium() -> None:
    clear_animation()
    set_material("gem", (0.18, 0.72, 0.12, 1.0), 0.05, 0.30,
                 (0.06, 0.50, 0.025, 1.0), 0.28)
    set_material("arcane", (0.48, 0.90, 0.12, 1.0), 0.03, 0.25,
                 (0.22, 0.68, 0.035, 1.0), 0.38)
    set_material("iron", (0.12, 0.16, 0.11, 1.0), 0.08, 0.82,
                 (0.0, 0.0, 0.0, 1.0), 0.0)

    # Add two subordinate spikes without modifying the downloaded candidate in place.
    source = bpy.data.objects.get("body/arcane")
    if source is None:
        raise RuntimeError("Expected crystal object 'body/arcane' was not imported")
    extra = source.copy()
    extra.data = source.data.copy()
    extra.name = "uranium_secondary_spikes"
    extra.scale = source.scale * 0.58
    extra.rotation_euler.z += math.radians(112.0)
    extra.location += Vector((-0.105, 0.080, -0.012))
    bpy.context.collection.objects.link(extra)

    # Bring the cluster to a useful RTS resource-node size while retaining metre units.
    for obj in mesh_objects():
        obj.scale *= 7.0
        obj.location *= 7.0
    center_and_ground()


def export_glb(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(path),
        export_format="GLB",
        export_animations=False,
        export_apply=True,
        export_image_format="AUTO",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("normalize", "uranium"))
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])

    clear_scene()
    import_gltf(arguments.source.resolve())
    clear_animation()
    if arguments.mode == "uranium":
        prepare_uranium()
    else:
        center_and_ground()
    export_glb(arguments.destination.resolve())


if __name__ == "__main__":
    main()
