"""Original, static oil-field scenery; no additional resource entities or inventories."""
import math
import random

import bpy
from mathutils import Vector


def compose_oil(size, radius, seed):
    rng = random.Random(seed)
    pieces = []
    palette = []
    for name, color, roughness in (
        ('oil_weathered_steel', (.12, .15, .14), .85),
        ('oil_muted_rust', (.24, .12, .055), .94),
        ('oil_worn_ochre', (.32, .25, .11), .86),
        ('oil_wet_seep', (.012, .016, .014), .22),
        ('oil_tar_crust', (.055, .045, .032), .98),
    ):
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        mat.diffuse_color = (*color, 1)
        shader = mat.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value = (*color, 1)
        shader.inputs['Roughness'].default_value = roughness
        palette.append(mat)

    def finish(obj, material):
        obj.data.materials.append(palette[material])
        pieces.append(obj)
        return obj

    def box(position, dimensions, material=0):
        bpy.ops.mesh.primitive_cube_add(size=1, location=position)
        obj = finish(bpy.context.object, material)
        obj.scale = dimensions
        return obj

    def rod(a, b, radius=.07, material=0, vertices=10):
        delta = Vector(b) - Vector(a)
        bpy.ops.mesh.primitive_cylinder_add(
            vertices=vertices, radius=radius, depth=delta.length,
            location=(Vector(a) + Vector(b)) * .5)
        obj = finish(bpy.context.object, material)
        obj.rotation_euler = delta.to_track_quat('Z', 'Y').to_euler()
        return obj

    def wheel(position, axis='Y'):
        bpy.ops.mesh.primitive_torus_add(
            major_radius=.20, minor_radius=.035, major_segments=12, minor_segments=5,
            location=position, rotation=(math.pi / 2 if axis == 'Y' else 0, 0, 0))
        finish(bpy.context.object, 1)
        x, y, z = position
        rod((x-.19, y, z), (x+.19, y, z), .025, 1)
        if axis == 'Y':
            rod((x, y, z-.19), (x, y, z+.19), .025, 1)
        else:
            rod((x, y-.19, z), (x, y+.19, z), .025, 1)

    def seep(x, y, scale, variant):
        # Closed, nearly flat surfaces replace the old repeated angular seep chunks.
        segments = 24
        edge = [rng.uniform(.72, 1.15) for _ in range(segments)]
        vertices = [(x, y, .025)]
        for i, jitter in enumerate(edge):
            angle = math.tau * i / segments
            vertices.append((x + math.cos(angle)*scale*jitter,
                             y + math.sin(angle)*scale*.52*jitter, .025))
        faces = [(0, i+1, (i+1) % segments+1) for i in range(segments)]
        mesh = bpy.data.meshes.new('irregular_seep_surface')
        mesh.from_pydata(vertices, [], faces)
        mesh.update()
        obj = bpy.data.objects.new('seep' if variant == 0 else 'tar_crust', mesh)
        bpy.context.collection.objects.link(obj)
        finish(obj, 3 if variant == 0 else 4)

    def wellhead(x, y, variant):
        rod((x, y, .03), (x, y, .78), .11)
        for z in (.16, .43, .69):
            rod((x, y, z-.035), (x, y, z+.035), .19, 1)
        rod((x-.36, y, .56), (x+.42, y, .56), .085)
        wheel((x+.28, y-.09, .56))
        if variant:
            rod((x+.42, y, .56), (x+.56, y, .12), .08, 1)
        else:
            box((x, y, .85), (.28, .28, .12), 1)

    def tank(x, y):
        rod((x-.65, y, .43), (x+.65, y, .43), .34, 1, 16)
        for dx in (-.45, .45):
            box((x+dx, y, .16), (.12, .65, .28))
            rod((x+dx, y, .42), (x+dx+.035, y, .42), .35)
        rod((x, y, .65), (x, y, .91), .07)
        wheel((x, y, .92), 'Z')

    def pump(x, y, broken=False):
        for dy in (-.38, .38):
            box((x, y+dy, .10), (2.4, .14, .18), 1)
            rod((x-.5, y+dy, .18), (x, y+dy, 1.75), .075)
            rod((x+.55, y+dy, .18), (x, y+dy, 1.75), .075)
        rod((x, y-.45, 1.75), (x, y+.45, 1.75), .11, 1)
        if broken:
            beam = box((x+.35, y, .62), (2.5, .20, .20), 2)
            beam.rotation_euler.y = .40
        else:
            beam = box((x+.10, y, 1.86), (2.5, .20, .20), 2)
            beam.rotation_euler.y = -.12
            # Curved horsehead profile extruded across the beam, readable from RTS zoom.
            profile = [(1.18, 2.08), (1.48, 2.02), (1.65, 1.82),
                       (1.69, 1.48), (1.44, 1.44), (1.34, 1.73), (1.18, 1.78)]
            vertices = [(x+px, y+dy, pz) for dy in (-.16, .16) for px, pz in profile]
            n = len(profile)
            faces = [tuple(reversed(range(n))), tuple(range(n, n*2))]
            faces += [(i, (i+1) % n, (i+1) % n+n, i+n) for i in range(n)]
            mesh = bpy.data.meshes.new('horsehead_profile')
            mesh.from_pydata(vertices, [], faces)
            mesh.update()
            obj = bpy.data.objects.new('idle_pump_horsehead', mesh)
            bpy.context.collection.objects.link(obj)
            finish(obj, 1)
            rod((x+1.6, y, .10), (x+1.6, y, 1.5), .025)
            rod((x-.8, y, .42), (x-.65, y, 1.76), .045, 1)
        box((x-.72, y, .35), (.65, .55, .48))

    if size.startswith('node_'):
        kind = size.removeprefix('node_')
        if kind == 'well':
            wellhead(0, 0, seed % 2)
        elif kind == 'tank':
            tank(0, 0)
        elif kind == 'pump':
            pump(0, 0, broken=seed % 2 == 0)
        else:
            raise ValueError('V2 seeps are terrain surfaces and have no model')
        bpy.context.view_layer.update()
        return pieces

    # Separate large equipment from low scatter; do not randomly overlap every prop.
    if size == 'small':
        wellhead(-.5, 0, seed % 2)
        wellhead(.75, .55, 1)
    else:
        pump(-.45, -.25, broken=size == 'large')
        wellhead(radius*.48, -.35, 1)
        tank(-radius*.28, radius*.45)
        if size == 'large':
            wellhead(radius*.25, radius*.55, 0)
    for i in range({'small': 7, 'medium': 11, 'large': 16}[size]):
        angle = rng.random()*math.tau
        distance = math.sqrt(rng.random())*radius*.72
        x, y = math.cos(angle)*distance, math.sin(angle)*distance
        seep(x, y, rng.uniform(.35, .85), i % 3 == 0)
    for i in range({'small': 2, 'medium': 3, 'large': 5}[size]):
        x, y = rng.uniform(-radius*.6, radius*.6), rng.uniform(-radius*.6, radius*.6)
        rod((x, y, .10), (x+.65, y+.18, .10), .055, 1)
        rod((x+.65, y+.18, .10), (x+.65, y+.48, .10), .055)
    bpy.context.view_layer.update()
    return pieces
