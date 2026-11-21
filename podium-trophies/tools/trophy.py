# Podium trophy models for F1 25: read a trophy (or any static model) from a game archive into an OBJ, and write an
# OBJ (or another game model) into a podium trophy's slot as raw fragments for the launcher's replace-resource ops.
#
#   python tools/trophy.py export <archive.erp> <model .idf name or part of it> <out.obj> [--instance trophy_1st]
#   python tools/trophy.py build <donor> <in.obj | archive.erp:model> <outdir> [--height H | --scale S]
#                                [--material SLOT=eaid://...material ...]
#   python tools/trophy.py selftest
#
# Vertex format (every trophy here, read from the meshes' vertex elements):
#   vb=0  float3 position (metres, Y up, base of the trophy at y = 0)
#   vb=1  half4  normal (x y z, w = 0)
#   vb=2  half4  tangent (x y z, w = handedness: bitangent = w * cross(normal, tangent), V up)
#   vb=3  half4  two UV sets (u0 v0 u1 v1), V down (Direct3D)
#   vb=4  rgba8  vertex colour, on some trophies only (classic_a, closed_b, modern_b, monaco, open_b)
#   ib=0  u16 triangle list, counter-clockwise front faces seen with the normals
# The OBJ is in game space with V flipped to V up (OBJ's convention): it opens the right way round in Blender with the
# default OBJ import (Y up). UV1 and vertex colours ride along as '#vt1 u v' and '#vc r g b a' lines, one a vertex,
# which other tools ignore; without them UV1 = UV0 and the colour is white. '#vtg x y z w' lines keep the game's own
# tangents, and are used only while the file is still i/i/i with one line a vertex (a 3D program re-exporting the OBJ
# drops them); otherwise tangents are computed from the UVs.
#
# A podium trophy draws four instances of its geometry, one a model node: trophy_1st, trophy_ENG (the constructors'
# trophy), trophy_2nd and trophy_3rd. Some trophies shrink 2nd and 3rd with the node's scale (closed_a), others with
# smaller geometry (classic_a, f2_a); build keeps whichever the donor does. Each instance has one submesh a material.
import argparse
import glob
import json
import os
import re
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import erp  # noqa: E402
import gfxmesh  # noqa: E402

GAME = os.environ.get('F1_25', r'E:\Steam\steamapps\common\F1 25')
PODIUM = '2025_asset_groups/animation_package/scenes/cinematics/cinematicspodium.erp'
TROPHY_DIR = 'eaid://environment_package/core_assets/objects/idf/trophy/'


def stock_check(rel):
    """Stops when the game's copy of rel is not Steam's: while the game runs, the launcher has rewritten
    archives (this addon's own build included) into the game folder, and building from that copy would leave
    out the materials it already carries. The sizes come from the launcher's baseline records."""
    path = os.path.join(GAME, rel)
    state = os.environ.get('SWOFTY_STATE', os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Swofty'))
    sizes = set()
    for b in glob.glob(os.path.join(state, 'baseline', '*', 'baseline.json')):
        try:
            f = json.load(open(b, encoding='utf-8'))['files'].get(rel)
        except (OSError, ValueError, KeyError):
            continue
        if f:
            sizes.add(f['size'])
    if sizes and os.path.getsize(path) not in sizes:
        raise SystemExit(f'{rel} is not the stock file (size {os.path.getsize(path)}, stock {sorted(sizes)}): '
                         'the launcher has written a modded copy into the game folder. Close the game so the '
                         'launcher puts the stock files back (or run "swofty restore"), then build again.')
    return path


def podium_idf(donor):
    """trophy_closed_a -> its podium model name (trophy_monaco is stored as trophy_monaco_a_01)."""
    if donor.startswith('eaid://'):
        return donor
    base = {'trophy_monaco': 'trophy_monaco_a'}.get(donor, donor)
    return f'{TROPHY_DIR}{base}_01.idf'


# ---- archives

class Archive:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        self.res = {}
        for name, rtype, frags in erp.entries(self.data):
            self.res[name] = (rtype, frags)

    def frags(self, name):
        return [erp.fragment(self.data, fr) for fr in self.res[name][1]]

    def find_model(self, part):
        hits = sorted({n.split('?')[0] for n in self.res if n.endswith('?model') and part in n})
        if len(hits) != 1:
            raise SystemExit(f'{part}: {len(hits)} models match in {self.path}: {hits[:10]}')
        return hits[0]


def parse_emb(b):
    n, nm, mo = struct.unpack_from('<3I', b, 0)
    pairs = [struct.unpack_from('<2I', b, 12 + 8 * i) for i in range(n)]
    names = 12 + 8 * n
    mats = 12 + mo
    cs = lambda o: b[o:b.index(b'\0', o)].decode('latin1')
    return {cs(names + a): cs(mats + c) for a, c in pairs}


def build_emb(entries):
    """GfxMatBindRes: (slot, material) pairs; the same layout texture.binding writes in Driver Display."""
    names, mats, no, mo, pairs = b'', b'', {}, {}, []
    for nm, mt in entries:
        if nm not in no:
            no[nm] = len(names)
            names += nm.encode('ascii') + b'\0'
        if mt not in mo:
            mo[mt] = len(mats)
            mats += mt.encode('ascii') + b'\0'
        pairs.append((no[nm], mo[mt]))
    return (struct.pack('<3I', len(entries), len(mo), 8 * len(entries) + len(names))
            + b''.join(struct.pack('<2I', *p) for p in pairs) + names + mats)


class Model:
    """A static model read from an archive: its buffers decoded, its mesh parsed."""

    def __init__(self, arc, idf):
        self.arc, self.idf = arc, idf
        mesh_names = [n for n in arc.res if n.startswith(idf + '?lod=') and n.endswith('&mesh')]
        self.mesh_name = sorted(mesh_names)[0]  # LOD 0
        self.mesh_raw = arc.frags(self.mesh_name)[0]
        self.mesh = gfxmesh.parse(self.mesh_raw)
        self.model_name = idf + '?model'
        self.model_raw = arc.frags(self.model_name)[0]
        self.emb_name = idf[:-4] + '.emb?context=default'
        self.emb = parse_emb(arc.frags(self.emb_name)[0]) if self.emb_name in arc.res else {}
        self.node_names = re.findall(rb'([^\0]+)\0', self.model_raw[self.model_raw.index(b'&mesh\0') + 6:])
        self.node_names = [x.decode('latin1') for x in self.node_names]
        self.buffers = {}
        for s, name in enumerate(self.mesh['vb_names']):
            head, data = arc.frags(name)
            self.buffers[s] = (name, head, data)
        self.ib_head, self.ib_data = arc.frags(self.mesh['ib_name'])
        kind, stride, count, fmt = gfxmesh.read_buffer(self.ib_head, self.ib_data)
        assert kind == 10 and stride == 2, ('only u16 index buffers', stride)
        self.indices = np.frombuffer(self.ib_data, '<u2').astype(np.int64)
        self.attr = {}
        for st, off, sem, idx, fmt in self.mesh['elements']:
            name, head, data = self.buffers[st]
            dt, n, _, stride = gfxmesh.VB_FORMATS[fmt]
            assert off == 0, 'one element a stream'
            self.attr[(sem, idx)] = np.frombuffer(data, dt).reshape(-1, n)
        self.nverts = len(self.attr[(1, 0)])

    def instances(self):
        """[(model node name, [submesh index, ...])], in the mesh's node record order."""
        out = []
        for n in self.mesh['nodes']:
            nm = self.node_names[n['node']] if n['node'] < len(self.node_names) else f'node{n["node"]}'
            out.append((nm, list(range(n['first'], n['first'] + n['count']))))
        return out


# ---- OBJ

def fmt(x):
    return repr(float(x))


def export_obj(model, out, only=None):
    """Every vertex of the buffers once, in buffer order (so f i/i/i), and one object a model node with one usemtl
    group a submesh. only: a node name to write just that instance (its vertices still numbered as in the buffer)."""
    P = model.attr[(1, 0)].astype(np.float64)
    N = model.attr[(2, 0)].astype(np.float64)
    UV = model.attr[(5, 0)].astype(np.float64)
    C = model.attr.get((6, 0))
    mats = model.mesh['mat_names']
    offs = dict(zip(gfxmesh.mat_offsets(mats), mats))
    mtl = os.path.splitext(out)[0] + '.mtl'
    lines = [f'# {model.idf}', f'# from {os.path.basename(model.arc.path)} by podium-trophies/tools/trophy.py',
             f'mtllib {os.path.basename(mtl)}']
    for i in range(model.nverts):
        lines.append('v %s %s %s' % tuple(fmt(c) for c in P[i]))
    for i in range(model.nverts):
        lines.append('vt %s %s' % (fmt(UV[i, 0]), fmt(1.0 - UV[i, 1])))
        lines.append('#vt1 %s %s' % (fmt(UV[i, 2]), fmt(1.0 - UV[i, 3])))
    T = model.attr[(3, 0)].astype(np.float64)
    for i in range(model.nverts):
        lines.append('#vtg %s %s %s %s' % tuple(fmt(c) for c in T[i]))
    for i in range(model.nverts):
        lines.append('vn %s %s %s' % tuple(fmt(c) for c in N[i, :3]))
    if C is not None:
        for i in range(model.nverts):
            lines.append('#vc %d %d %d %d' % tuple(int(c) for c in C[i]))
    for node, subs in model.instances():
        if only and node != only:
            continue
        lines.append(f'o {node}')
        for si in subs:
            s = model.mesh['subs'][si]
            lines.append(f'usemtl {offs[s["mat_off"]]}')
            tri = model.indices[s['start']:s['start'] + s['count']].reshape(-1, 3) + 1
            for a, b, c in tri:
                lines.append(f'f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}')
    open(out, 'w', newline='\n').write('\n'.join(lines) + '\n')
    ml = []
    tex = export_textures(model, os.path.splitext(out)[0] + '_textures')
    for slot in mats:
        ml += [f'newmtl {slot}', f'# game material: {model.emb.get(slot, "?")}', 'Kd 0.8 0.8 0.8']
        for kind, key in (('colour', 'map_Kd'), ('normal', 'map_Bump'), ('surface', '# surface map:')):
            p = tex.get(model.emb.get(slot), {}).get(kind)
            if p:
                ml.append(f'{key} ' + os.path.relpath(p, os.path.dirname(os.path.abspath(mtl))).replace('\\', '/'))
        ml.append('')
    open(mtl, 'w', newline='\n').write('\n'.join(ml))
    return out


def export_textures(model, folder):
    """The model's materials' textures as PNGs (<name>.game.png; the build tool ignores a texture with that ending,
    so an unedited export keeps the game's materials): {material: {'colour'|'normal'|'surface': path}}. The top mip
    inside the archive only (256 x 256 for the trophies); the larger ones are in Oodle-packed .mipmaps files. Normal
    maps are written OpenGL style (green up), as Blender expects."""
    import texture
    from PIL import Image
    out = {}
    for mat in sorted(set(model.emb.values())):
        if mat not in model.arc.res:
            continue
        for v in material_deps(model.arc, mat):
            if not v.endswith('.tif.image'):
                continue
            kind = {'_d': 'colour', '_n': 'normal', '_s': 'surface'}.get(v[-12:-10])
            if not kind:
                continue
            head, pixels = model.arc.frags(v)[:2]
            try:
                im = texture.decode(head, pixels)
            except (KeyError, OSError):
                continue
            if kind == 'normal':
                a = np.array(im)
                a[..., 1] = 255 - a[..., 1]
                im = Image.fromarray(a)
            os.makedirs(folder, exist_ok=True)
            p = os.path.join(folder, v.rsplit('/', 1)[1][:-len('.tif.image')] + '.game.png')
            im.save(p)
            out.setdefault(mat, {})[kind] = p
    return out


class Geometry:
    """Vertices (pos, nrm, tan, uv0, uv1, col) and, a material slot each, triangle lists into them; objects keeps
    each OBJ object's [(slot, triangles)]."""

    def __init__(self):
        self.objects = []  # [(name, [(slot, tri array)])]


def read_obj(path):
    vs, vts, vt1s, vns, vcs, vtgs = [], [], [], [], [], []
    objects = []
    cur_obj, cur_mat = None, 'default'
    faces = {}  # (obj, mat) -> list of corner triples
    order = []
    for line in open(path, encoding='utf-8', errors='replace'):
        t = line.split()
        if not t:
            continue
        k = t[0]
        if k == 'v':
            vs.append([float(x) for x in t[1:4]])
        elif k == 'vt':
            vts.append([float(x) for x in t[1:3]] if len(t) > 2 else [float(t[1]), 0.0])
        elif k == '#vt1':
            vt1s.append([float(x) for x in t[1:3]])
        elif k == 'vn':
            vns.append([float(x) for x in t[1:4]])
        elif k == '#vtg':
            vtgs.append([float(x) for x in t[1:5]])
        elif k == '#vc':
            vcs.append([int(x) for x in t[1:5]])
        elif k in ('o', 'g'):
            cur_obj = ' '.join(t[1:]) or None
        elif k == 'usemtl':
            cur_mat = ' '.join(t[1:])
        elif k == 'f':
            corners = []
            for c in t[1:]:
                p = c.split('/')
                ix = [int(p[0]), int(p[1]) if len(p) > 1 and p[1] else 0, int(p[2]) if len(p) > 2 and p[2] else 0]
                for j, n in ((0, len(vs)), (1, len(vts)), (2, len(vns))):
                    if ix[j] < 0:
                        ix[j] += n + 1
                corners.append(tuple(ix))
            key = (cur_obj, cur_mat)
            if key not in faces:
                faces[key] = []
                order.append(key)
            for i in range(1, len(corners) - 1):  # fan-triangulate polygons
                faces[key].append((corners[0], corners[i], corners[i + 1]))
    return {'v': np.array(vs, np.float64).reshape(-1, 3), 'vt': np.array(vts, np.float64).reshape(-1, 2),
            'vt1': np.array(vt1s, np.float64).reshape(-1, 2), 'vn': np.array(vns, np.float64).reshape(-1, 3),
            'vc': np.array(vcs, np.uint8).reshape(-1, 4), 'vtg': np.array(vtgs, np.float64).reshape(-1, 4), 'faces': faces, 'order': order}


def obj_geometry(o):
    """Unique (v, vt, vn) corners become vertices. When every corner is i/i/i (an exported OBJ), the vertices keep the
    OBJ's numbering, so an unedited export writes the same buffers back."""
    allc = [c for k in o['order'] for f in o['faces'][k] for c in f]
    identity = (allc and len(o['vt']) == len(o['v']) and len(o['vn']) == len(o['v'])
                and all(a == b == c for a, b, c in allc))
    if identity:
        keys = list(range(1, len(o['v']) + 1))
        remap = None
    else:
        remap = {}
        keys = []
        for c in allc:
            if c not in remap:
                remap[c] = len(keys)
                keys.append(c)
    g = Geometry()
    n = len(keys)
    if identity:
        g.pos = o['v'].copy()
        g.uv0 = o['vt'].copy()
        g.nrm = o['vn'].copy()
        g.uv1 = o['vt1'].copy() if len(o['vt1']) == n else g.uv0.copy()
        g.col = o['vc'].copy() if len(o['vc']) == n else None
    else:
        vi = np.array([k[0] - 1 for k in keys])
        ti = np.array([k[1] - 1 for k in keys])
        ni = np.array([k[2] - 1 for k in keys])
        g.pos = o['v'][vi]
        g.uv0 = o['vt'][ti] if (ti >= 0).all() and len(o['vt']) else np.zeros((n, 2))
        g.uv1 = o['vt1'][ti] if len(o['vt1']) == len(o['vt']) and (ti >= 0).all() and len(o['vt1']) else g.uv0.copy()
        g.nrm = o['vn'][ni] if (ni >= 0).all() and len(o['vn']) else None
        g.col = o['vc'][vi] if len(o['vc']) == len(o['v']) else None
    # OBJ V up -> game V down
    g.uv0 = np.stack([g.uv0[:, 0], 1.0 - g.uv0[:, 1]], 1)
    g.uv1 = np.stack([g.uv1[:, 0], 1.0 - g.uv1[:, 1]], 1)
    objs = {}
    for (ob, mat) in o['order']:
        tris = np.array([[(c[0] - 1) if identity else remap[c] for c in f] for f in o['faces'][(ob, mat)]],
                        np.int64).reshape(-1, 3)
        objs.setdefault(ob, []).append((mat, tris))
    g.objects = list(objs.items())
    alltri = np.concatenate([t for _, sl in g.objects for _, t in sl])
    if g.nrm is None:
        g.nrm = smooth_normals(g.pos, alltri)
    if identity and len(o['vtg']) == n:  # an unedited export: its own tangents
        g.tan = o['vtg'].copy()
    else:
        g.tan = tangents(g.pos, g.nrm, g.uv0, alltri)
    return g


def model_geometry(model, instance=None):
    """A game model as Geometry: one instance of it (default: the first node record's), its submeshes by slot."""
    inst = model.instances()
    node, subs = inst[0] if instance is None else next(x for x in inst if x[0] == instance)
    offs = dict(zip(gfxmesh.mat_offsets(model.mesh['mat_names']), model.mesh['mat_names']))
    used = []
    parts = []
    for si in subs:
        s = model.mesh['subs'][si]
        tri = model.indices[s['start']:s['start'] + s['count']].reshape(-1, 3)
        used.append(tri.ravel())
        parts.append((offs[s['mat_off']], tri))
    keep = np.unique(np.concatenate(used))
    remap = np.full(model.nverts, -1, np.int64)
    remap[keep] = np.arange(len(keep))
    g = Geometry()
    g.pos = model.attr[(1, 0)][keep].astype(np.float64)
    g.nrm = model.attr[(2, 0)][keep, :3].astype(np.float64)
    g.tan = model.attr[(3, 0)][keep].astype(np.float64)
    uv = model.attr[(5, 0)][keep].astype(np.float64)
    g.uv0, g.uv1 = uv[:, :2], uv[:, 2:]
    g.col = model.attr[(6, 0)][keep] if (6, 0) in model.attr else None
    g.objects = [(node, [(slot, remap[t]) for slot, t in parts])]
    g.materials = {slot: model.emb.get(slot) for slot, _ in parts}
    return g


def smooth_normals(p, tri):
    fn = np.cross(p[tri[:, 1]] - p[tri[:, 0]], p[tri[:, 2]] - p[tri[:, 0]])
    acc = np.zeros_like(p)
    for k in range(3):
        np.add.at(acc, tri[:, k], fn)
    return acc / np.maximum(np.linalg.norm(acc, axis=1), 1e-20)[:, None]


def tangents(p, n, uv, tri):
    """Per-vertex tangents (x y z w) from game-space UVs (V down): T along +u, w = sign of the V-up bitangent against
    cross(normal, T), which is the game's convention (checked against the stock trophies' vb=2)."""
    a, b, c = p[tri[:, 0]], p[tri[:, 1]], p[tri[:, 2]]
    ua, ub, uc = uv[tri[:, 0]], uv[tri[:, 1]], uv[tri[:, 2]]
    e1, e2, d1, d2 = b - a, c - a, ub - ua, uc - ua
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    ok = np.abs(det) > 1e-20
    r = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)[:, None]
    t = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) * r
    bt = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) * r
    acc, accb = np.zeros_like(p), np.zeros_like(p)
    for k in range(3):
        np.add.at(acc, tri[:, k], t)
        np.add.at(accb, tri[:, k], bt)
    t = acc - n * (acc * n).sum(1)[:, None]
    ln = np.linalg.norm(t, axis=1)
    bad = ln < 1e-12
    if bad.any():  # no UV gradient: any direction across the normal
        alt = np.where(np.abs(n[:, 0:1]) < 0.9, [[1.0, 0, 0]], [[0, 1.0, 0]])
        t2 = alt - n * (alt * n).sum(1)[:, None]
        t[bad] = t2[bad]
        ln = np.linalg.norm(t, axis=1)
    t /= ln[:, None]
    w = np.where((np.cross(n, t) * -accb).sum(1) < 0, -1.0, 1.0)
    return np.concatenate([t, w[:, None]], 1)


# ---- building a donor slot

def half4(a3, w):
    out = np.zeros((len(a3), 4), np.float16)
    out[:, :3] = a3
    out[:, 3] = w
    return out


def build_slot(donor, g, materials=None, scale=1.0):
    """Raw fragments that put Geometry g into the donor model. Returns {resource: [fragment bytes, ...]} for the
    replaced resources, and notes. materials: {slot: material path} (default: g.materials or the donor's)."""
    m = donor.mesh
    notes = []
    inst = donor.instances()
    slots = []
    for _, sl in g.objects:
        for s, _ in sl:
            if s not in slots:
                slots.append(s)
    mats = dict(donor.emb)
    mats.update(getattr(g, 'materials', {}) or {})
    mats.update(materials or {})
    missing = [s for s in slots if not mats.get(s)]
    if missing:
        raise SystemExit(f'no material for slot(s) {missing}: pass --material SLOT=eaid://...material')

    # one geometry a donor instance: the OBJ's own objects when they are named after the donor's nodes (an export of
    # it), else the one model for all four
    by_name = dict(g.objects)
    per_inst = all(nm in by_name for nm, _ in inst) and len(g.objects) == len(inst)
    # the donor's own instance sizes: radius of each instance's submeshes against the largest
    rad = [max(m['subs'][si]['sphere'][3] for si in subs) for _, subs in inst]
    rel = [round(r / max(rad), 3) for r in rad]  # 1st and the constructors' trophy differ in the last bits
    if per_inst:
        rel = [1.0] * len(inst)
    groups = {}  # relative size -> vertex base in the new buffers
    P, N, T, U, C = [], [], [], [], []
    nv = len(g.pos)
    for f in sorted(set(rel), reverse=True):
        if f in groups or per_inst:
            continue
        groups[f] = len(P) * nv
        P.append(g.pos * scale * f)
        N.append(g.nrm)
        T.append(g.tan)
        U.append(np.concatenate([g.uv0, g.uv1], 1))
        C.append(g.col if g.col is not None else np.full((nv, 4), (255, 255, 255, 0), np.uint8))
    if per_inst:
        P, N, T = [g.pos * scale], [g.nrm], [g.tan]
        U = [np.concatenate([g.uv0, g.uv1], 1)]
        C = [g.col if g.col is not None else np.full((nv, 4), (255, 255, 255, 0), np.uint8)]
        groups = None
    P, N, T, U, C = (np.concatenate(x) for x in (P, N, T, U, C))
    if len(P) > 65535:
        raise SystemExit(f'{len(P)} vertices: over the 65535 a u16 index buffer reaches')
    if len(set(rel)) > 1:
        notes.append(f'instance sizes baked into the geometry as the donor does: {[round(x, 3) for x in rel]}')

    # index buffer and submeshes, in donor node record order, one submesh a slot used by that instance
    idx, subs, nodes, draws, subtable = [], [], [], [], []
    base_sub = m['subs'][0]['raw']
    mat_names = slots
    moff = dict(zip(mat_names, gfxmesh.mat_offsets(mat_names)))
    shared = {}
    pos_at = 0
    for k, ((nm, dsubs), f) in enumerate(zip(inst, rel)):
        node = m['nodes'][k]
        parts = by_name[nm] if per_inst else g.objects[0][1] if len(g.objects) == 1 else \
            [x for _, sl in g.objects for x in sl]
        first = len(subs)
        for slot, tri in parts:
            vb = 0 if per_inst else groups[f]
            key = (vb, slot, id(tri))
            if key in shared and not per_inst:
                start, count = shared[key]
            else:
                start, count = pos_at, tri.size
                idx.append((tri + vb).ravel())
                pos_at += tri.size
                shared[key] = (start, count)
            used = np.unique(tri) + vb
            sph = gfxmesh.sphere(P[used])
            raw = list(base_sub)
            subs.append({'raw': raw, 'start': start, 'count': count, 'sphere': sph, 'mat_off': moff[slot]})
            subtable.append((node['node'], len(subs) - 1))
            draws.append([1, len(subs) - 1, start, count])
        nodes.append(dict(node, first=first, count=len(subs) - first))
    for i, s in enumerate(subs):
        s['raw'][10] = 8 * i
    I = np.concatenate(idx).astype(np.uint16)

    same_layout = (len(subs) == len(m['subs']) and len(mat_names) == len(m['mat_names'])
                   and all(n['count'] == o['count'] and n['first'] == o['first'] for n, o in zip(nodes, m['nodes'])))
    nm_ = dict(m)
    if same_layout:  # the donor's descriptor with new ranges and bounds: same size, same table order
        nm_['subs'] = [dict(o, start=s['start'], count=s['count'], sphere=s['sphere']) for o, s in zip(m['subs'], subs)]
        nm_['draws'] = [[d[0], d[1], nm_['subs'][d[1]]['start'], nm_['subs'][d[1]]['count']] for d in m['draws']]
        nm_['mat_names'] = mat_names
    else:
        nm_.update(subs=subs, nodes=nodes, draws=draws, subtable=subtable, mat_names=mat_names, mat_pad=0)
        notes.append(f'mesh descriptor rebuilt: {len(subs)} submeshes ({len(inst)} instances x {len(mat_names)} '
                     f'material slots), donor had {len(m["subs"])}')
    mesh = gfxmesh.build(nm_)

    # vertex streams in the donor's element order; padded to the donor's size when the new data fits in it
    out = {}
    streams = {}
    for st, off, sem, si, fmt_ in m['elements']:
        if sem == 1:
            data = P.astype('<f4')
        elif sem == 2:
            data = half4(N, 0.0)
        elif sem == 3:
            data = half4(T[:, :3], T[:, 3])
        elif sem == 5:
            data = U.astype('<f2')
        elif sem == 6:
            data = C.astype('u1')
        else:
            raise SystemExit(f'donor has a vertex element this tool does not write: semantic {sem}')
        streams[st] = np.ascontiguousarray(data).tobytes()
    fits = len(P) <= donor.nverts and len(I) <= len(donor.indices)
    for st, raw in streams.items():
        name, head, old = donor.buffers[st]
        kind, stride, count, f = gfxmesh.read_buffer(head, old)
        if fits:
            raw = raw + old[len(raw):]  # unused tail: the donor's own bytes, never referenced
            n = count
        else:
            n = len(raw) // stride
        out[name] = [gfxmesh.buffer_head(kind, stride, n, f), raw]
    ib = I.tobytes()
    if fits:
        ib = ib + b'\0' * (len(donor.ib_data) - len(ib))
    out[m['ib_name']] = [gfxmesh.buffer_head(10, 2, len(ib) // 2, 0x13), ib]
    out[donor.mesh_name] = [mesh]
    out[donor.model_name] = [gfxmesh.model_bounds(donor.model_raw, P)]
    out[donor.emb_name] = [build_emb([(s, mats[s]) for s in mat_names])]
    notes.append(f'{len(P)} vertices, {len(I) // 3} triangles in the buffers (donor {donor.nverts} / '
                 f'{len(donor.indices) // 3}): ' + ('padded to the donor sizes' if fits else
                                                    'buffers grow, so the launcher rewrites the archive'))
    return out, notes, [mats[s] for s in mat_names]


# ---- copying a game model's materials and textures

def material_deps(arc, mat):
    """The texture resources (views and images) a material in arc names."""
    raw = arc.frags(mat)[0]
    tex = sorted({x.decode() for x in re.findall(rb'eaid://[ -~]+?\.tif(?=\0)', raw)})
    out = []
    for t in tex:
        for r in (t + '.image', t):
            if r in arc.res and r not in out:
                out.append(r)
    return out


def write_raw(out, outdir, prefix):
    """Raw fragment files for replace-resource; returns [(resource, fragment, path)]."""
    os.makedirs(outdir, exist_ok=True)
    files = []
    for res, frs in out.items():
        q = res.split('?', 1)[1] if '?' in res else res.rsplit('.', 1)[1]
        tag = re.sub(r'[^a-z0-9]+', '_', q.lower()).strip('_')
        if res.endswith('.emb?context=default'):
            tag = 'emb'
        for i, b in enumerate(frs):
            p = os.path.join(outdir, f'{prefix}.{tag}.{i}.bin')
            open(p, 'wb').write(b)
            files.append((res, i, p))
    return files


# ---- commands

def cmd_export(a):
    arc = Archive(a.erp)
    model = Model(arc, arc.find_model(a.model))
    export_obj(model, a.out, a.instance)
    print(f'{a.out}: {model.nverts} vertices, instances {[n for n, _ in model.instances()]}, '
          f'slots {model.mesh["mat_names"]} -> {model.emb}')


def load_source(src, instance=None):
    if '.erp:' in src:
        path, part = src.split('.erp:', 1)
        arc = Archive(path + '.erp')
        return model_geometry(Model(arc, arc.find_model(part)), instance), arc
    return obj_geometry(read_obj(src)), None


def cmd_build(a):
    donor_arc = Archive(os.path.join(GAME, PODIUM) if not a.donor_erp else a.donor_erp)
    donor = Model(donor_arc, podium_idf(a.donor))
    g, _ = load_source(a.source)
    mats = dict(x.split('=', 1) for x in a.material or [])
    scale = a.scale or 1.0
    if a.height:
        scale = a.height / (g.pos[:, 1].max() - min(0.0, g.pos[:, 1].min()))
    out, notes, _ = build_slot(donor, g, mats, scale)
    files = write_raw(out, a.outdir, a.donor.split('/')[-1])
    for n in notes:
        print(n)
    for r, i, p in files:
        print(r, i, p)


def selftest(a):
    G = os.path.join(GAME, '2025_asset_groups')
    import glob
    import tempfile
    ok = 0
    paths = [os.path.join(GAME, PODIUM)] + sorted(glob.glob(os.path.join(
        G, 'environment_package/core_assets/objects/gen_trophies_agdescs/*.erp')))
    for p in paths:
        arc = Archive(p)
        for n, (rt, frs) in arc.res.items():
            if rt == 'GfxMeshRes':
                raw = arc.frags(n)[0]
                assert gfxmesh.build(gfxmesh.parse(raw)) == raw, n
                ok += 1
    print(f'mesh descriptors: {ok} of {ok} rebuilt byte for byte')
    arc = Archive(os.path.join(GAME, PODIUM))
    tmp = tempfile.mkdtemp()
    for t in ('trophy_closed_a', 'trophy_classic_a', 'trophy_open_b'):
        donor = Model(arc, podium_idf(t))
        obj = export_obj(donor, os.path.join(tmp, t + '.obj'))
        o = read_obj(obj)
        out_t, _, _ = build_slot(donor, obj_geometry(o))
        exact = all(out_t[n] == [h, d] for n, h, d in donor.buffers.values())
        o['vtg'] = o['vtg'][:0]  # as a 3D program would write it back: tangents computed
        g = obj_geometry(o)
        out, notes, _ = build_slot(donor, g)
        rep = []
        for st, (name, head, data) in donor.buffers.items():
            h2, d2 = out[name]
            same = d2 == data
            if not same and st == 2:  # tangents are recomputed: compare directions
                a0 = np.frombuffer(data, '<f2').reshape(-1, 4).astype(np.float64)
                a1 = np.frombuffer(d2, '<f2').reshape(-1, 4).astype(np.float64)
                dot = (a0[:, :3] * a1[:, :3]).sum(1)
                rep.append(f'vb={st} tangents recomputed: median cos {np.median(dot):.4f}, '
                           f'{np.mean(dot > 0.9) * 100:.1f}% within 25 deg, w same {np.mean(a0[:, 3] == a1[:, 3]) * 100:.1f}%')
            else:
                rep.append(f'vb={st} {"identical" if same and h2 == head else "DIFFERS"}')
        rep.append(f'ib {"identical" if out[donor.mesh["ib_name"]][1] == donor.ib_data else "DIFFERS"}')
        mesh1, mesh2 = gfxmesh.parse(donor.mesh_raw), gfxmesh.parse(out[donor.mesh_name][0])
        for mm in (mesh1, mesh2):
            for s in mm['subs']:
                s['sphere'] = (0.0, 0.0, 0.0, 0.0)
        rep.append('mesh ' + ('identical but for the bounding spheres' if gfxmesh.build(mesh1) == gfxmesh.build(mesh2)
                              else 'DIFFERS'))
        rep.append('emb ' + ('identical' if out[donor.emb_name][0] == arc.frags(donor.emb_name)[0] else 'DIFFERS'))
        print(t + ': with the #vtg tangents every buffer ' + ('identical' if exact else 'DIFFERS')
              + '; without them: ' + '; '.join(rep))


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest='cmd', required=True)
    e = sp.add_parser('export')
    e.add_argument('erp')
    e.add_argument('model')
    e.add_argument('out')
    e.add_argument('--instance')
    b = sp.add_parser('build')
    b.add_argument('donor')
    b.add_argument('source')
    b.add_argument('outdir')
    b.add_argument('--donor-erp')
    b.add_argument('--height', type=float)
    b.add_argument('--scale', type=float)
    b.add_argument('--material', action='append')
    sp.add_parser('selftest')
    a = ap.parse_args()
    {'export': cmd_export, 'build': cmd_build, 'selftest': selftest}[a.cmd](a)


if __name__ == '__main__':
    main()
