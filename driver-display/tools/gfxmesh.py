# F1 25 static model resources: GfxMeshRes (?lod=N&mesh), GfxBufferRes (?lod=all&vb=N / &ib=0) and the bounds in
# GfxModelRes (?model). Read from the podium trophies in cinematicspodium.erp and the career cabinet trophies in
# gen_trophies_agdescs/*.erp; parse() + build() rebuild every GfxMeshRes in those archives byte for byte
# (tools/trophy.py selftest).
#
# GfxBufferRes, resource argument 4. Fragment 0, 16 bytes: kind | 0x1100 (9 vertex, 10 index), stride, count, format
# (vertex: 0x27 float3, 0x23 half4, 0x0e rgba8; index: 0x13 with stride 2 = u16). Fragment 1: count * stride bytes.
#
# GfxMeshRes, resource argument 0x60000, one fragment. A 0x4c-byte head of 19 u32; every offset in it counts from
# the end of the head (0x4c):
#   0 submeshes        1 vertex elements   2 index buffers (1)  3 material slots   4 submeshes again
#   5 -> vertex buffer names (one per stream, NUL-terminated)   6 -> index buffer name   7 -> material slot names
#   8 -> submesh table (8 bytes a submesh: model node, submesh)  9 vertex elements again  10 -> vertex elements
#   11 -> stream table (u32 1, buffer index; one a stream, to the end)                       12 node records
#   13 -> node records   14 draws   15 -> draws   16, 17 zero   18 the LOD
# Then, in this order: submeshes (0x58 each), node records (0x5c), draws (16), and the tables above.
#   submesh: elements, 4, 2, first index, index count, 0, bounding sphere (x y z r), 8 * submesh, 1,
#            material slot name offset, 0, 0, stream list (7 slots: 0 1 2 ... then zeros)
#   node record: 4x4 matrix (identity), 0, model node, 0, first submesh, submesh count, 0, 0
#   draw: 1, submesh, first index, index count
#   vertex element: stream, offset, semantic (1 position, 2 normal, 3 tangent, 5 texcoord, 6 colour), index,
#                   format (47 float3, 35 half4, 14 rgba8 unorm)
import struct

import numpy as np

HEAD = 0x4c
SUB = 0x58
NODE = 0x5c


def _cstr(b, o):
    e = b.index(b'\0', o)
    return b[o:e].decode('latin1'), e + 1


def parse(m):
    h = list(struct.unpack_from('<19I', m, 0))
    nsub, nel, nib, nmat, nsub2 = h[:5]
    assert nib == 1 and nsub2 == nsub and h[9] == nel and h[16] == h[17] == 0, h
    d = {'lod': h[18]}
    d['subs'] = []
    for i in range(nsub):
        s = struct.unpack_from('<6I4f12I', m, HEAD + i * SUB)
        d['subs'].append({'raw': list(s), 'start': s[3], 'count': s[4], 'sphere': s[6:10], 'mat_off': s[12]})
    d['nodes'] = []
    for i in range(h[12]):
        o = HEAD + h[13] + i * NODE
        mat = struct.unpack_from('<16f', m, o)
        ints = struct.unpack_from('<7I', m, o + 64)
        d['nodes'].append({'matrix': mat, 'raw': list(ints), 'node': ints[1], 'first': ints[3], 'count': ints[4]})
    d['draws'] = [list(struct.unpack_from('<4I', m, HEAD + h[15] + 16 * i)) for i in range(h[14])]
    end_vb = HEAD + h[6]
    o, d['vb_names'] = HEAD + h[5], []
    while o < end_vb:
        s, o = _cstr(m, o)
        d['vb_names'].append(s)
    d['ib_name'], _ = _cstr(m, HEAD + h[6])
    raw_mats = m[HEAD + h[7]:HEAD + h[8]]
    d['mat_names'] = [x.decode('latin1') for x in raw_mats.split(b'\0')[:nmat]]
    d['mat_pad'] = len(raw_mats) - sum(len(x) + 1 for x in d['mat_names'])
    d['subtable'] = [struct.unpack_from('<2I', m, HEAD + h[8] + 8 * i) for i in range(nsub)]
    d['elements'] = [struct.unpack_from('<5I', m, HEAD + h[10] + 20 * i) for i in range(nel)]
    st = m[HEAD + h[11]:]
    d['streams'] = [struct.unpack_from('<2I', st, 8 * i) for i in range(len(st) // 8)]
    assert len(st) % 8 == 0 and len(d['streams']) == len(d['vb_names']), (len(st), d['vb_names'])
    return d


def build(d):
    subs = b''
    for s in d['subs']:
        r = list(s['raw'])
        r[3], r[4], r[12] = s['start'], s['count'], s['mat_off']
        r[6:10] = s['sphere']
        subs += struct.pack('<6I4f12I', *r)
    nodes = b''
    for n in d['nodes']:
        r = list(n['raw'])
        r[1], r[3], r[4] = n['node'], n['first'], n['count']
        nodes += struct.pack('<16f', *n['matrix']) + struct.pack('<7I', *r)
    draws = b''.join(struct.pack('<4I', *x) for x in d['draws'])
    vbn = b''.join(x.encode('latin1') + b'\0' for x in d['vb_names'])
    ibn = d['ib_name'].encode('latin1') + b'\0'
    mats = b''.join(x.encode('latin1') + b'\0' for x in d['mat_names']) + b'\0' * d.get('mat_pad', 0)
    subt = b''.join(struct.pack('<2I', *x) for x in d['subtable'])
    els = b''.join(struct.pack('<5I', *x) for x in d['elements'])
    sts = b''.join(struct.pack('<2I', *x) for x in d['streams'])
    o_nodes = len(subs)
    o_draws = o_nodes + len(nodes)
    o_vbn = o_draws + len(draws)
    o_ibn = o_vbn + len(vbn)
    o_mat = o_ibn + len(ibn)
    o_subt = o_mat + len(mats)
    o_el = o_subt + len(subt)
    o_st = o_el + len(els)
    ns, ne = len(d['subs']), len(d['elements'])
    head = struct.pack('<19I', ns, ne, 1, len(d['mat_names']), ns, o_vbn, o_ibn, o_mat, o_subt, ne, o_el, o_st,
                       len(d['nodes']), o_nodes, len(d['draws']), o_draws, 0, 0, d['lod'])
    return head + subs + nodes + draws + vbn + ibn + mats + subt + els + sts


def mat_offsets(names):
    out, o = [], 0
    for n in names:
        out.append(o)
        o += len(n) + 1
    return out


# ---- buffers

VB_FORMATS = {47: ('<f4', 3, 0x27, 12), 35: ('<f2', 4, 0x23, 8), 14: ('u1', 4, 0x0e, 4)}


def buffer_head(kind, stride, count, fmt):
    return struct.pack('<4I', kind | 0x1100, stride, count, fmt)


def read_buffer(head, data):
    kind, stride, count, fmt = struct.unpack('<4I', head)
    assert len(data) == stride * count, (stride, count, len(data))
    return kind & 0xff, stride, count, fmt


# ---- bounds

def sphere(p):
    """A bounding sphere (Ritter's, then grown to cover every point). p: (n, 3) float array."""
    p = np.asarray(p, dtype=np.float64)
    a = p[0]
    b = p[np.argmax(((p - a) ** 2).sum(1))]
    c = p[np.argmax(((p - b) ** 2).sum(1))]
    ctr, r = (b + c) / 2, np.sqrt(((b - c) ** 2).sum()) / 2
    for _ in range(64):
        d = np.sqrt(((p - ctr) ** 2).sum(1))
        i = int(np.argmax(d))
        if d[i] <= r * (1 + 1e-6):
            break
        nr = (r + d[i]) / 2
        ctr = ctr + (p[i] - ctr) * ((nr - r) / d[i])
        r = nr
    r = float(np.sqrt(((p - ctr) ** 2).sum(1)).max())
    return (float(ctr[0]), float(ctr[1]), float(ctr[2]), r)


def model_bounds(model, pts):
    """GfxModelRes (argument 0x20001) with its bounds set for the points: sphere at 0x10 (x y z r), box at 0x24
    (min x y z, max x y z)."""
    m = bytearray(model)
    p = np.asarray(pts, dtype=np.float32)
    struct.pack_into('<4f', m, 0x10, *sphere(p))
    struct.pack_into('<3f', m, 0x24, *p.min(0))
    struct.pack_into('<3f', m, 0x30, *p.max(0))
    return bytes(m)
