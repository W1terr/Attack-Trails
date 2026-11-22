r"""Makes the trail's meshes and textures.

res/meshes/AttackTrails/Particles.nif: one particle batch, the plugin's unit of particles. A root node
"PowerAttackParticles" with SHAPES skinned shapes "Particles0".. of PER_SHAPE quads each, every quad on its own bone
"PAPart<shape>_<nn>": a round dot when its index % 8 is 0, else a dash (see aspect()). The plugin puts each bone where
its particle is, X along the way it points, Y across facing the camera, scaled by its length. A swing takes as many
batches as its particles need and gives each back when all its particles burned out.

res/meshes/AttackTrails/Line.nif: one swing's thin line. A root node "PowerAttackLine" with LINE_BONES bones
"PALine<nn>" and one skinned strip "Line" over them, two vertices per bone at (-1,0,0) and (1,0,0): the plugin puts the
bones along the path the weapon's edge took, X across it (facing the camera), scaled by the half width.

ParticlesSolid.nif, LineSolid.nif: the same with a blended NiAlphaProperty (4333, threshold 128) instead of the additive
one: painted over the picture instead of added as light, so dark colors and black show. The plugin uses them for dark
trail colors.

All shapes: BSEffectShaderProperty (unlit, vertex alpha, double sided, no depth write) and an additive NiAlphaProperty,
each its own (never shared: the renderer keeps its passes in the shader property); the plugin gives each copy its own
materials (color, brightness, opacity).

res/textures/AttackTrails/Particle.dds, Line.dds: white with the shape in the alpha channel, uncompressed BGRA with
mips.

usage: python make_assets.py
"""
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nif  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MESHES = ROOT / "res" / "meshes" / "AttackTrails"
TEXTURES = ROOT / "res" / "textures" / "AttackTrails"

SHAPES = 4        # shapes per particle batch, each its own draw call and brightness
PER_SHAPE = 64    # particles per shape, one bone each (a skin partition holds at most 80)
LINE_BONES = 64

# effect shader flags
SLSF1_SKINNED = 1 << 1
SLSF1_VERTEX_ALPHA = 1 << 3
SLSF1_ZBUFFER_TEST = 1 << 31
SLSF2_DOUBLE_SIDED = 1 << 4
SLSF2_VERTEX_COLORS = 1 << 5
SLSF2_NO_FADE = 1 << 3

ALPHA_ADDITIVE = 0x000D  # blend on, src = SRC_ALPHA, dst = ONE
ALPHA_SOLID = 4333       # blend on, src = SRC_ALPHA, dst = INV_SRC_ALPHA (test func GREATER, test off)


def aspect(k):
    """a particle quad's width against its length: a round dot at every 8th index, the rest dashes (the plugin knows)"""
    return 1.0 if k % 8 == 0 else 0.26


# ---------------------------------------------------------------- textures

def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def particle_texture(w=64, h=32):
    """one particle: U along it (1 = the way it points), V across; a hot thin core in a soft glow, soft rounded ends,
    the front end a little brighter. On a square quad it is a round dot."""
    u = np.linspace(0, 1, w)[None, :]
    v = np.linspace(0, 1, h)[:, None]
    across = 0.85 * np.exp(-((v - 0.5) / 0.15) ** 2) + 0.3 * np.exp(-((v - 0.5) / 0.3) ** 2)
    along = smoothstep(0.0, 0.42, u) * (1 - smoothstep(0.72, 1.0, u)) * (0.75 + 0.25 * u)
    return np.clip(across * along * 1.35, 0, 1)


def line_texture(w=256, h=32, seed=5):
    """the thin line: U along it, V across; a hot thin core in a faint halo, glinting here and there along it"""
    rng = np.random.default_rng(seed)
    u = np.linspace(0, 1, w)[None, :]
    v = np.linspace(0, 1, h)[:, None]
    core = np.exp(-((v - 0.5) / 0.09) ** 2)
    halo = 0.3 * np.exp(-((v - 0.5) / 0.25) ** 2)
    glints = np.zeros_like(u)
    for _ in range(14):
        c = rng.random()
        glints += (0.35 + 0.4 * rng.random()) * np.exp(-((u - c) / (0.004 + 0.006 * rng.random())) ** 2)
    along = 0.8 + glints
    return np.clip((core + halo) * along, 0, 1)


def mips(alpha):
    levels = [alpha]
    cur = alpha
    while cur.shape[0] > 1 or cur.shape[1] > 1:
        if cur.shape[0] > 1:
            cur = (cur[0::2] + cur[1::2]) / 2
        if cur.shape[1] > 1:
            cur = (cur[:, 0::2] + cur[:, 1::2]) / 2
        levels.append(cur)
    return levels


def dds(levels):
    """uncompressed B8G8R8A8 dds: white, the levels' values as alpha"""
    h, w = levels[0].shape
    pixel_format = struct.pack("<II4sIIIII", 32, 0x41, b"\0\0\0\0", 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack("<IIIIIII", 124, 0x2100F, h, w, w * 4, 0, len(levels)) + b"\0" * 44 + pixel_format
    header += struct.pack("<IIIII", 0x401008, 0, 0, 0, 0)
    body = bytearray()
    for lv in levels:
        px = np.full(lv.shape + (4,), 255, dtype=np.uint8)
        px[..., 3] = np.clip(np.round(lv * 255), 0, 255).astype(np.uint8)
        body += px.tobytes()
    return b"DDS " + header + bytes(body)


# ---------------------------------------------------------------- meshes

def mesh_of(P, UV):
    n = len(P)
    return {
        "P": np.asarray(P, float),
        "UV": np.asarray(UV, float),
        "N": np.tile([0.0, 0.0, 1.0], (n, 1)),
        "T": np.tile([0.0, 1.0, 0.0], (n, 1)),
        "B": np.tile([1.0, 0.0, 0.0], (n, 1)),
        "C": np.ones((n, 4)),
    }


def particles():
    """PER_SHAPE quads, quad k on bone k, centered on it, length 1 along X, width aspect(k) along Y"""
    n = PER_SHAPE * 4
    P = np.zeros((n, 3))
    UV = np.zeros((n, 2))
    indices = np.zeros((n, 4), dtype=np.uint8)
    tris = []
    for k in range(PER_SHAPE):
        half = aspect(k) / 2
        base = 4 * k
        for j, (x, y) in enumerate(((-0.5, -half), (0.5, -half), (-0.5, half), (0.5, half))):
            P[base + j] = (x, y, 0)
            UV[base + j] = (x + 0.5, 0.5 + y / (2 * half))
            indices[base + j, 0] = k
        a, b, c, d = base, base + 1, base + 2, base + 3
        tris += [(a, c, b), (b, c, d)]
    weights = np.zeros((n, 4))
    weights[:, 0] = 1.0
    return mesh_of(P, UV), np.array(tris), weights, indices


def strip():
    """LINE_BONES pairs of vertices, pair i on bone i at (-1,0,0) / (1,0,0); U along, V across"""
    n = LINE_BONES * 2
    P = np.zeros((n, 3))
    P[0::2, 0] = -1.0
    P[1::2, 0] = 1.0
    UV = np.zeros((n, 2))
    UV[:, 0] = np.repeat(np.arange(LINE_BONES) / (LINE_BONES - 1), 2)
    UV[1::2, 1] = 1.0
    tris = []
    for i in range(LINE_BONES - 1):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        tris += [(a, c, b), (b, c, d)]
    weights = np.zeros((n, 4))
    weights[:, 0] = 1.0
    indices = np.zeros((n, 4), dtype=np.uint8)
    indices[:, 0] = np.repeat(np.arange(LINE_BONES), 2)
    return mesh_of(P, UV), np.array(tris), weights, indices


class SkinnedShape(nif.BSTriShape):
    """a skinned BSTriShape with a bounding sphere of its own (the writer leaves skinned ones at zero)"""

    def __init__(self, name, mesh, tris, shader, alpha, bound):
        super().__init__(name, mesh, tris, shader, alpha)
        self.bound = bound

    def payload(self, f):
        data = super().payload(f)
        head = len(self.av(f))
        return data[:head] + struct.pack("<3ff", *self.bound) + data[head + 16:]


def add_shape(f, root, name, texture, bones, mesh, tris, weights, indices, solid=False):
    """a skinned shape with its own effect shader and alpha property"""
    shader = f.add(nif.BSEffectShaderProperty(
        texture, "", base_color=(1.0, 1.0, 1.0, 1.0), base_color_scale=1.0, soft_depth=0.0,
        flags1=SLSF1_SKINNED | SLSF1_VERTEX_ALPHA | SLSF1_ZBUFFER_TEST,
        flags2=SLSF2_DOUBLE_SIDED | SLSF2_VERTEX_COLORS | SLSF2_NO_FADE,
        falloff=(1.0, 1.0, 1.0, 1.0), lighting_influence=0))
    alpha = f.add(nif.NiAlphaProperty(flags=ALPHA_SOLID, threshold=128) if solid else nif.NiAlphaProperty(flags=ALPHA_ADDITIVE, threshold=0))
    shape = f.add(SkinnedShape(name, mesh, tris, shader, alpha, (0.0, 0.0, 0.0, 512.0)))
    # always drawn (the plugin hides what shows nothing)
    shape.flags |= 1 << 11
    data = f.add(nif.NiSkinData(np.zeros((len(bones), 3)), weights, indices, mesh["P"]))
    partition = f.add(nif.NiSkinPartition(mesh, tris, weights, indices, len(bones)))
    shape.skin = f.add(nif.NiSkinInstance(data, partition, root, bones))
    root.children.append(shape)


def write_particles(path, solid=False):
    f = nif.File()
    root = f.add(nif.NiNode("PowerAttackParticles"))
    f.roots.append(root)
    mesh, tris, weights, indices = particles()
    for s in range(SHAPES):
        bones = [f.add(nif.NiNode(f"PAPart{s}_{i:02d}")) for i in range(PER_SHAPE)]
        root.children += bones
        add_shape(f, root, f"Particles{s}", r"textures\AttackTrails\Particle.dds", bones, mesh, tris, weights, indices, solid)
    print(f"wrote {path} ({f.write(path)} bytes)")


def write_line(path, solid=False):
    f = nif.File()
    root = f.add(nif.NiNode("PowerAttackLine"))
    f.roots.append(root)
    bones = [f.add(nif.NiNode(f"PALine{i:02d}")) for i in range(LINE_BONES)]
    root.children += bones
    mesh, tris, weights, indices = strip()
    add_shape(f, root, "Line", r"textures\AttackTrails\Line.dds", bones, mesh, tris, weights, indices, solid)
    print(f"wrote {path} ({f.write(path)} bytes)")


def main():
    MESHES.mkdir(parents=True, exist_ok=True)
    TEXTURES.mkdir(parents=True, exist_ok=True)
    (TEXTURES / "Particle.dds").write_bytes(dds(mips(particle_texture())))
    (TEXTURES / "Line.dds").write_bytes(dds(mips(line_texture())))
    print("wrote Particle.dds, Line.dds")
    write_particles(MESHES / "Particles.nif")
    write_line(MESHES / "Line.nif")
    write_particles(MESHES / "ParticlesSolid.nif", solid=True)
    write_line(MESHES / "LineSolid.nif", solid=True)


if __name__ == "__main__":
    main()
