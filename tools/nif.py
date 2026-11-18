"""A small writer (and raw reader) for Skyrim Special Edition nif files (20.2.0.7, user version 12, BS version 100).

Only the block types the ship needs: nodes, BSTriShape (plain and skinned), lighting shader properties, texture sets,
alpha properties, skinning (NiSkinInstance / NiSkinData / NiSkinPartition), Havok collision (bhkCollisionObject,
bhkRigidBody copied from a vanilla file, bhkListShape, bhkConvexVerticesShape).

Blocks are plain objects; File.add() numbers them, refs are the objects themselves (None = -1). File.write() collects
the strings and block types and writes the header, the blocks and the footer.

Vertex data follows Bethesda's convention: the vertex's "tangent" is the direction of increasing V and its
"bitangent" the direction of increasing U (checked against vanilla meshes).
"""
import struct

import numpy as np

HAVOK_SCALE = 1.0 / 69.99125  # game units -> Havok units (bhkWorld::GetWorldScale)

# BSVertexDesc attribute flags (bits 44..)
VF_VERTEX, VF_UV, VF_UV2, VF_NORMAL, VF_TANGENT, VF_COLORS, VF_SKINNED = 1, 2, 4, 8, 16, 32, 64


def vertex_desc(skinned):
    """vertex format: position (+ bitangent x), uv, normal (+ bitangent y), tangent (+ bitangent z), colors[, skin]"""
    attrs = VF_VERTEX | VF_UV | VF_NORMAL | VF_TANGENT | VF_COLORS | (VF_SKINNED if skinned else 0)
    size = 11 if skinned else 8  # in 4 byte units
    low = size | (4 << 8) | (5 << 16) | (6 << 20) | (7 << 24) | ((8 << 28) if skinned else 0)
    return (attrs << 44) | low, size * 4


class Block:
    type_name = "NiObject"

    def payload(self, f):  # -> bytes
        raise NotImplementedError


def _ref(f, block):
    return struct.pack("<i", -1 if block is None else f.index(block))


def _str(f, text):
    return struct.pack("<i", -1 if text is None else f.string(text))


def _sized(text):
    data = text.encode("cp1252")
    return struct.pack("<I", len(data)) + data


def _matrix(rot):
    """3x3 rotation (numpy, column vectors: world = R @ local) as the 9 floats of a nif Matrix33"""
    r = np.asarray(rot, dtype=np.float64)
    # nif Matrix33 is stored m11 m21 m31 m12 m22 m32 m13 m23 m33 with m_rc = row r, column c of the TRANSPOSE
    # of the column-vector matrix (Gamebryo stores rows of the matrix that multiplies row vectors). In practice:
    # the file holds R row by row, as NiMatrix3::entry[row][col] in memory.
    return struct.pack("<9f", *r.reshape(-1))


class NiObjectNET(Block):
    def __init__(self, name=None):
        self.name = name
        self.extra = []
        self.controller = None

    def net(self, f):
        out = _str(f, self.name) + struct.pack("<I", len(self.extra))
        out += b"".join(_ref(f, e) for e in self.extra)
        return out + _ref(f, self.controller)


class NiAVObject(NiObjectNET):
    def __init__(self, name=None, translation=(0, 0, 0), rotation=None, scale=1.0, flags=0x8000E):
        super().__init__(name)
        self.flags = flags
        self.translation = translation
        self.rotation = np.eye(3) if rotation is None else rotation
        self.scale = scale
        self.collision = None

    def av(self, f):
        return (self.net(f) + struct.pack("<I", self.flags) + struct.pack("<3f", *self.translation) +
                _matrix(self.rotation) + struct.pack("<f", self.scale) + _ref(f, self.collision))


class NiNode(NiAVObject):
    type_name = "NiNode"

    def __init__(self, name=None, **kw):
        super().__init__(name, **kw)
        self.children = []

    def payload(self, f):
        out = self.av(f) + struct.pack("<I", len(self.children)) + b"".join(_ref(f, c) for c in self.children)
        return out + struct.pack("<I", 0)  # effects


class BSFadeNode(NiNode):
    type_name = "BSFadeNode"


class BSValueNode(NiNode):
    """a node with a value: named "AddOnNode..." the game attaches the addon node (ADDN) with that index to it"""
    type_name = "BSValueNode"

    def __init__(self, name=None, value=0, value_flags=0, **kw):
        super().__init__(name, **kw)
        self.value, self.value_flags = value, value_flags

    def payload(self, f):
        return super().payload(f) + struct.pack("<iB", self.value, self.value_flags)


class BSXFlags(Block):
    type_name = "BSXFlags"

    def __init__(self, value):
        self.value = value

    def payload(self, f):
        return _str(f, "BSX") + struct.pack("<I", self.value)


class BSShaderTextureSet(Block):
    type_name = "BSShaderTextureSet"

    def __init__(self, diffuse, normal, extra=()):
        self.textures = [diffuse, normal] + list(extra)
        self.textures += [""] * (9 - len(self.textures))

    def payload(self, f):
        return struct.pack("<i", len(self.textures)) + b"".join(_sized(t) for t in self.textures)


# BSLightingShaderProperty flags
SLSF1_SPECULAR = 1 << 0
SLSF1_SKINNED = 1 << 1
SLSF1_CAST_SHADOWS = 1 << 9
SLSF1_RECEIVE_SHADOWS = 1 << 8
SLSF1_OWN_EMIT = 1 << 22
SLSF1_REMAPPABLE = 1 << 25
SLSF1_ZBUFFER_TEST = 1 << 31
SLSF2_ZBUFFER_WRITE = 1 << 0
SLSF2_DOUBLE_SIDED = 1 << 4
SLSF2_VERTEX_COLORS = 1 << 5
SLSF2_ENVMAP_LIGHT_FADE = 1 << 15


class BSLightingShaderProperty(Block):
    type_name = "BSLightingShaderProperty"

    def __init__(self, textures, skinned=False, double_sided=False, specular=0.3, glossiness=40.0, emissive=None,
                 uv_scale=(1.0, 1.0), lighting_effect=(0.3, 2.0)):
        self.textures = textures
        self.flags1 = (SLSF1_RECEIVE_SHADOWS | SLSF1_CAST_SHADOWS | SLSF1_REMAPPABLE | SLSF1_ZBUFFER_TEST |
                       (SLSF1_SPECULAR if specular > 0 else 0) | (SLSF1_SKINNED if skinned else 0) |
                       (SLSF1_OWN_EMIT if emissive else 0))
        self.flags2 = SLSF2_ZBUFFER_WRITE | SLSF2_VERTEX_COLORS | SLSF2_ENVMAP_LIGHT_FADE | (SLSF2_DOUBLE_SIDED if double_sided else 0)
        self.specular = specular
        self.glossiness = glossiness
        self.emissive = emissive  # (r, g, b, multiple)
        self.uv_scale = uv_scale
        self.lighting_effect = lighting_effect

    def payload(self, f):
        em = self.emissive or (0.0, 0.0, 0.0, 1.0)
        out = struct.pack("<I", 0)  # shader type: default
        out += _str(f, "") + struct.pack("<I", 0) + _ref(f, None)  # name, no extra data, no controller
        out += struct.pack("<II", self.flags1, self.flags2)
        out += struct.pack("<2f", 0.0, 0.0) + struct.pack("<2f", *self.uv_scale)
        out += _ref(f, self.textures)
        out += struct.pack("<3f", *em[:3]) + struct.pack("<f", em[3])
        out += struct.pack("<I", 3)  # wrap S, wrap T
        out += struct.pack("<f", 1.0)  # alpha
        out += struct.pack("<f", 0.0)  # refraction
        out += struct.pack("<f", self.glossiness)
        out += struct.pack("<3f", 1.0, 1.0, 1.0) + struct.pack("<f", self.specular)
        out += struct.pack("<2f", *self.lighting_effect)
        return out


class BSEffectShaderProperty(Block):
    """an effect shader (the game's rapids use one for their white water): source texture tinted through a greyscale
    palette, vertex alpha, soft edges against what is behind"""
    type_name = "BSEffectShaderProperty"

    def __init__(self, source, greyscale, base_color=(1.0, 1.0, 1.0, 0.8), base_color_scale=0.7, soft_depth=50.0,
                 flags1=0xE0000078, flags2=0x40000020, falloff=(0.0871558, -4.37e-08, 1.0, 0.0), lighting_influence=191):
        self.source, self.greyscale = source, greyscale
        self.base_color, self.base_color_scale, self.soft_depth = base_color, base_color_scale, soft_depth
        self.flags1, self.flags2, self.falloff, self.influence = flags1, flags2, falloff, lighting_influence

    def payload(self, f):
        out = _str(f, None) + struct.pack("<I", 0) + _ref(f, None)
        out += struct.pack("<II", self.flags1, self.flags2) + struct.pack("<4f", 0.0, 0.0, 1.0, 1.0)
        out += _sized(self.source) + bytes([3, self.influence, 0, 0])
        out += struct.pack("<4f", *self.falloff) + struct.pack("<4f", *self.base_color)
        out += struct.pack("<2f", self.base_color_scale, self.soft_depth) + _sized(self.greyscale)
        return out


class NiAlphaProperty(Block):
    type_name = "NiAlphaProperty"

    def __init__(self, flags=0x12EC, threshold=128):
        self.flags = flags
        self.threshold = threshold

    def payload(self, f):
        return _str(f, "") + struct.pack("<I", 0) + _ref(f, None) + struct.pack("<HB", self.flags, self.threshold)


def _half(values):
    return np.asarray(values, dtype=np.float16)


def _byte_vec(v):
    return np.clip(np.round((np.asarray(v) + 1.0) * 127.5), 0, 255).astype(np.uint8)


def pack_vertices(mesh, skin=None):
    """mesh: dict of numpy arrays P (n,3) UV (n,2) N (n,3) T (n,3: d/dV) B (n,3: d/dU) C (n,4 floats 0..1);
    skin: (weights (n,4), indices (n,4)) -> bytes in the BSVertexDataSSE layout"""
    n = len(mesh["P"])
    rec = [("pos", "<f4", 3), ("bx", "<f4"), ("uv", "<f2", 2), ("nrm", "u1", 3), ("by", "u1"),
           ("tan", "u1", 3), ("bz", "u1"), ("col", "u1", 4)]
    if skin is not None:
        rec += [("w", "<f2", 4), ("bi", "u1", 4)]
    data = np.zeros(n, dtype=np.dtype(rec))
    data["pos"] = mesh["P"]
    data["bx"] = mesh["B"][:, 0]
    data["uv"] = _half(mesh["UV"])
    data["nrm"] = _byte_vec(mesh["N"])
    data["by"] = _byte_vec(mesh["B"][:, 1])
    data["tan"] = _byte_vec(mesh["T"])
    data["bz"] = _byte_vec(mesh["B"][:, 2])
    data["col"] = np.clip(np.round(np.asarray(mesh["C"]) * 255.0), 0, 255).astype(np.uint8)
    if skin is not None:
        data["w"] = _half(skin[0])
        data["bi"] = np.asarray(skin[1], dtype=np.uint8)
    return data.tobytes()


def bounding_sphere(points):
    p = np.asarray(points, dtype=np.float64)
    if len(p) == 0:
        return (0.0, 0.0, 0.0), 0.0
    center = (p.min(0) + p.max(0)) / 2
    return tuple(center), float(np.sqrt(((p - center) ** 2).sum(1)).max())


class BSTriShape(NiAVObject):
    type_name = "BSTriShape"

    def __init__(self, name, mesh, tris, shader, alpha=None):
        super().__init__(name)
        self.mesh = mesh
        self.tris = np.asarray(tris, dtype=np.uint16).reshape(-1, 3)
        self.shader = shader
        self.alpha = alpha
        self.skin = None  # NiSkinInstance: then the vertices live in its partition

    def payload(self, f):
        skinned = self.skin is not None
        desc, size = vertex_desc(skinned)
        out = self.av(f)
        if skinned:
            out += struct.pack("<4f", 0, 0, 0, 0)
        else:
            c, r = bounding_sphere(self.mesh["P"])
            out += struct.pack("<3ff", *c, r)
        out += _ref(f, self.skin) + _ref(f, self.shader) + _ref(f, self.alpha)
        out += struct.pack("<Q", desc)
        if skinned:
            return out + struct.pack("<HHI", 0, 0, 0) + struct.pack("<I", 0)
        n = len(self.mesh["P"])
        if n > 0xFFFF or len(self.tris) > 0xFFFF:
            raise ValueError(f"{self.name}: too many vertices / triangles ({n}, {len(self.tris)})")
        out += struct.pack("<HH", len(self.tris), n)
        out += struct.pack("<I", n * size + len(self.tris) * 6)
        out += pack_vertices(self.mesh) + self.tris.tobytes()
        return out + struct.pack("<I", 0)  # no particle data


class NiSkinInstance(Block):
    type_name = "NiSkinInstance"

    def __init__(self, data, partition, root, bones):
        self.data, self.partition, self.root, self.bones = data, partition, root, bones

    def payload(self, f):
        out = _ref(f, self.data) + _ref(f, self.partition) + _ref(f, self.root)
        return out + struct.pack("<I", len(self.bones)) + b"".join(_ref(f, b) for b in self.bones)


def _transform(rot, trans, scale=1.0):
    return _matrix(rot) + struct.pack("<3f", *trans) + struct.pack("<f", scale)


class NiSkinData(Block):
    type_name = "NiSkinData"

    def __init__(self, bone_rest, weights, indices, points, shape_offset=(0.0, 0.0, 0.0)):
        """bone_rest: (b,3) rest positions of the bones (identity rotation) in the shape's space;
        weights/indices: (n,4) per vertex; points: (n,3) vertex positions; shape_offset: where the shape sits
        relative to the skeleton root (the overall skin transform is its inverse, as in vanilla files)"""
        self.shape_offset = np.asarray(shape_offset, dtype=np.float64)
        self.bone_rest = np.asarray(bone_rest)
        self.weights = np.asarray(weights)
        self.indices = np.asarray(indices)
        self.points = np.asarray(points)

    def payload(self, f):
        out = _transform(np.eye(3), -self.shape_offset) + struct.pack("<I", len(self.bone_rest)) + struct.pack("<B", 1)
        for b, rest in enumerate(self.bone_rest):
            mask = (self.indices == b) & (self.weights > 0)
            verts = np.nonzero(mask.any(1))[0]
            w = (self.weights * mask).sum(1)[verts]
            local = self.points[verts] - rest if len(verts) else np.zeros((1, 3))
            lo, hi = local.min(0), local.max(0)
            center = (lo + hi) / 2
            radius = float(np.sqrt(((local - center) ** 2).sum(1)).max()) if len(verts) else 0.0
            out += _transform(np.eye(3), -rest)
            out += struct.pack("<3ff", *center, radius)
            out += struct.pack("<H", len(verts))
            out += b"".join(struct.pack("<Hf", int(v), float(x)) for v, x in zip(verts, w))
        return out


class NiSkinPartition(Block):
    type_name = "NiSkinPartition"

    def __init__(self, mesh, tris, weights, indices, num_bones):
        self.mesh, self.weights, self.indices, self.num_bones = mesh, np.asarray(weights), np.asarray(indices), num_bones
        self.tris = np.asarray(tris, dtype=np.uint16).reshape(-1, 3)

    def payload(self, f):
        desc, size = vertex_desc(True)
        n = len(self.mesh["P"])
        if n > 0xFFFF or len(self.tris) > 0xFFFF or self.num_bones > 80:
            raise ValueError("skin partition too big")
        out = struct.pack("<I", 1) + struct.pack("<II", n * size, size) + struct.pack("<Q", desc)
        out += pack_vertices(self.mesh, (self.weights, self.indices))
        # the one partition: every vertex, every bone
        out += struct.pack("<5H", n, len(self.tris), self.num_bones, 0, 4)
        out += np.arange(self.num_bones, dtype=np.uint16).tobytes()
        out += struct.pack("<B", 1) + np.arange(n, dtype=np.uint16).tobytes()  # vertex map
        out += struct.pack("<B", 1) + self.weights.astype("<f4").tobytes()
        out += struct.pack("<B", 1) + self.tris.tobytes()  # has faces, triangles
        out += struct.pack("<B", 1) + self.indices.astype(np.uint8).tobytes()
        out += struct.pack("<BB", 0, 0)  # LOD level, global VB
        out += struct.pack("<Q", desc) + self.tris.tobytes()
        return out


class bhkCollisionObject(Block):
    type_name = "bhkCollisionObject"

    def __init__(self, target, body):
        self.target, self.body = target, body

    def payload(self, f):
        return _ref(f, self.target) + struct.pack("<H", 0x81) + _ref(f, self.body)


class RawBlock(Block):
    """a block copied from another file as it was: refs (byte offset -> block or None) and string indices (byte
    offset -> text or None) patched in for this file"""

    def __init__(self, type_name, data, refs=None, strings=None):
        self.type_name = type_name
        self.data = bytes(data)
        self.refs = refs or {}
        self.strings = strings or {}

    def payload(self, f):
        out = bytearray(self.data)
        for offset, block in self.refs.items():
            out[offset:offset + 4] = _ref(f, block)
        for offset, text in self.strings.items():
            out[offset:offset + 4] = _str(f, text)
        return bytes(out)


class RawRigidBody(Block):
    """a bhkRigidBody copied from a vanilla file; its shape ref (the first 4 bytes) is ours"""
    type_name = "bhkRigidBody"

    def __init__(self, raw, shape):
        self.raw, self.shape = raw, shape

    def payload(self, f):
        return _ref(f, self.shape) + self.raw[4:]


SKY_HAV_MAT_WOOD = 500811281


class bhkListShape(Block):
    type_name = "bhkListShape"

    def __init__(self, shapes, material=SKY_HAV_MAT_WOOD):
        self.shapes, self.material = shapes, material

    def payload(self, f):
        out = struct.pack("<I", len(self.shapes)) + b"".join(_ref(f, s) for s in self.shapes)
        out += struct.pack("<I", self.material)
        out += struct.pack("<3I", 0, 0, 0x80000000) * 2
        return out + struct.pack("<I", len(self.shapes)) + struct.pack("<I", 0) * len(self.shapes)


class bhkConvexVerticesShape(Block):
    type_name = "bhkConvexVerticesShape"

    def __init__(self, points, planes, material=SKY_HAV_MAT_WOOD, radius=0.01):
        """points (n,3) and planes (m,4: normal, offset with n.x + d <= 0 inside) in game units"""
        self.points, self.planes, self.material, self.radius = np.asarray(points), np.asarray(planes), material, radius

    def payload(self, f):
        out = struct.pack("<If", self.material, self.radius)
        out += struct.pack("<3I", 0, 0, 0x80000000) * 2
        v = np.zeros((len(self.points), 4), dtype="<f4")
        v[:, :3] = self.points * HAVOK_SCALE
        out += struct.pack("<I", len(v)) + v.tobytes()
        p = np.zeros((len(self.planes), 4), dtype="<f4")
        p[:, :3] = self.planes[:, :3]
        p[:, 3] = self.planes[:, 3] * HAVOK_SCALE
        return out + struct.pack("<I", len(p)) + p.tobytes()


class File:
    def __init__(self):
        self.blocks = []
        self._index = {}
        self._strings = []
        self._string_index = {}
        self.roots = []

    def add(self, block):
        if id(block) not in self._index:
            self._index[id(block)] = len(self.blocks)
            self.blocks.append(block)
        return block

    def index(self, block):
        if id(block) not in self._index:
            raise KeyError(f"block {type(block).__name__} {getattr(block, 'name', '')} was not added")
        return self._index[id(block)]

    def string(self, text):
        if text not in self._string_index:
            self._string_index[text] = len(self._strings)
            self._strings.append(text)
        return self._string_index[text]

    def write(self, path, author="W1ter"):
        payloads = [b.payload(self) for b in self.blocks]
        types = []
        type_of = []
        for b in self.blocks:
            if b.type_name not in types:
                types.append(b.type_name)
            type_of.append(types.index(b.type_name))
        out = b"Gamebryo File Format, Version 20.2.0.7\n"
        out += struct.pack("<IBII", 0x14020007, 1, 12, len(self.blocks))
        out += struct.pack("<I", 100)
        for text in (author, "", ""):
            data = text.encode("cp1252") + b"\0"
            out += struct.pack("<B", len(data)) + data
        out += struct.pack("<H", len(types)) + b"".join(_sized(t) for t in types)
        out += np.array(type_of, dtype="<u2").tobytes()
        out += np.array([len(p) for p in payloads], dtype="<u4").tobytes()
        out += struct.pack("<II", len(self._strings), max([len(s) for s in self._strings] + [0]))
        out += b"".join(_sized(s) for s in self._strings)
        out += struct.pack("<I", 0)  # groups
        out += b"".join(payloads)
        out += struct.pack("<I", len(self.roots)) + b"".join(struct.pack("<i", self.index(r)) for r in self.roots)
        with open(path, "wb") as stream:
            stream.write(out)
        return len(out)


def read_raw(path_or_bytes):
    """-> (block type names, raw block payloads, strings) of a SE nif"""
    data = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else open(path_or_bytes, "rb").read()
    pos = data.index(b"\n") + 1
    version, endian, user, count = struct.unpack_from("<IBII", data, pos)
    pos += 13
    pos += 4  # bs version
    for _ in range(3):
        pos += 1 + data[pos]
    ntypes = struct.unpack_from("<H", data, pos)[0]
    pos += 2
    types = []
    for _ in range(ntypes):
        n = struct.unpack_from("<I", data, pos)[0]
        types.append(data[pos + 4:pos + 4 + n].decode())
        pos += 4 + n
    type_of = struct.unpack_from(f"<{count}H", data, pos)
    pos += 2 * count
    sizes = struct.unpack_from(f"<{count}I", data, pos)
    pos += 4 * count
    nstrings, _ = struct.unpack_from("<II", data, pos)
    pos += 8
    strings = []
    for _ in range(nstrings):
        n = struct.unpack_from("<I", data, pos)[0]
        strings.append(data[pos + 4:pos + 4 + n].decode("cp1252"))
        pos += 4 + n
    ngroups = struct.unpack_from("<I", data, pos)[0]
    pos += 4 + 4 * ngroups
    blocks = []
    for i in range(count):
        blocks.append((types[type_of[i]], data[pos:pos + sizes[i]]))
        pos += sizes[i]
    return blocks, strings
