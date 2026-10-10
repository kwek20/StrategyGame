"""Rebuild original, untextured low-poly conifers; no third-party assets required."""
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def generate(variant):
    binary = bytearray()
    views, accessors, primitives = [], [], []

    def attribute(values, kind):
        offset = len(binary)
        binary.extend(struct.pack("<" + "f" * len(values), *values))
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(binary) - offset})
        components = 3
        item = {"bufferView": len(views) - 1, "componentType": 5126,
                "count": len(values) // components, "type": kind}
        item["min"] = [min(values[i::3]) for i in range(3)]
        item["max"] = [max(values[i::3]) for i in range(3)]
        accessors.append(item)
        return len(accessors) - 1

    def shell(radius, bottom, top, tip_radius, material, phase):
        positions, normals = [], []
        segments = 9
        for i in range(segments):
            a, b = 2 * math.pi * i / segments + phase, 2 * math.pi * (i + 1) / segments + phase
            ra = radius * (1 + .09 * math.sin(i * 2.4 + variant))
            rb = radius * (1 + .09 * math.sin((i + 1) * 2.4 + variant))
            low_a = (math.cos(a) * ra, bottom, math.sin(a) * ra)
            low_b = (math.cos(b) * rb, bottom, math.sin(b) * rb)
            high_a = (math.cos(a) * tip_radius, top, math.sin(a) * tip_radius)
            high_b = (math.cos(b) * tip_radius, top, math.sin(b) * tip_radius)
            for tri in ((low_a, high_a, low_b), (low_b, high_a, high_b),
                        ((0, bottom, 0), low_a, low_b)):
                u = [tri[1][j] - tri[0][j] for j in range(3)]
                v = [tri[2][j] - tri[0][j] for j in range(3)]
                n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
                length = math.sqrt(sum(x*x for x in n))
                if length < 1e-8:
                    continue
                for point in tri:
                    positions.extend(point)
                    normals.extend(x / length for x in n)
        primitives.append({"attributes": {"POSITION": attribute(positions, "VEC3"),
                                            "NORMAL": attribute(normals, "VEC3")},
                           "material": material})

    height = 5.4 if variant == 1 else 6.3
    shell(.18, 0, height * .8, .10, 0, 0)
    for tier, (bottom, top, radius) in enumerate(((.20, .67, 1.55), (.39, .84, 1.18), (.59, 1, .78))):
        shell(radius, height * bottom, height * top, .025, tier + 1, variant * .27 + tier * .32)
    materials = [{"pbrMetallicRoughness": {"baseColorFactor": color + [1],
                   "metallicFactor": 0, "roughnessFactor": 1}} for color in
                 ([.38, .27, .15], [.25, .38, .18], [.31, .44, .21], [.38, .50, .25])]
    document = {"asset": {"version": "2.0", "generator": "StrategyGame original conifer generator"},
                "buffers": [{"byteLength": len(binary)}], "bufferViews": views,
                "accessors": accessors, "materials": materials, "meshes": [{"primitives": primitives}],
                "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0}
    encoded = json.dumps(document, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    binary += b"\0" * (-len(binary) % 4)
    path = ROOT / f"assets/models/environment/terrain_v2/conifer_{variant}.glb"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(encoded) + 8 + len(binary)) +
                     struct.pack("<II", len(encoded), 0x4E4F534A) + encoded +
                     struct.pack("<II", len(binary), 0x004E4942) + binary)


if __name__ == "__main__":
    for variant in (1, 2):
        generate(variant)
