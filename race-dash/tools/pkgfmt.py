# Reader / writer for the game's "!pkg" serialisation (UI layouts .idf and templates .ui).
#
# Layout: "!pkg" u32 0, ptr root, ptr table-array. Containers: "!idi" (dict: u32 count|flags, then
# {u32 key string offset, u32 value, u32 fnv1a32(key)} sorted by hash) and "!ili" (list: u32 count|flags,
# then u32 values). A value with bit 31 set is the absolute offset of a container; otherwise it is
# (table << 24) | index into the typed tables listed by "!vca": table 0 is the string pool "!sbi" (index =
# byte offset of a length-prefixed, NUL-terminated string), the rest are "!vbi" blocks: 4-char type, count,
# stride, data.
import struct

TABLES = ['str', 'ui64', 'quat', 'ui32', 'vec3', 'shnm', 'fp32', 'bool', 'si32', 'vec4']
FMT = {'ui64': '<Q', 'quat': '<4f', 'ui32': '<I', 'vec3': '<4f', 'shnm': '16s', 'fp32': '<f', 'bool': '<B',
       'si32': '<i', 'vec4': '<4f', 'rgba': '<4B', 'ui08': '<B', 'ui16': '<H', 'si64': '<q'}


NODEDUP = {'vec3', 'rgba'}


def fnv1a(b):
    h = 0x811c9dc5
    for c in b:
        h = ((h ^ c) * 0x01000193) & 0xffffffff
    return h


class V:
    """A typed leaf value."""
    __slots__ = ('t', 'v')

    def __init__(self, t, v):
        self.t, self.v = t, v

    def __repr__(self):
        return f'{self.t}:{self.v!r}'

    def __eq__(self, o):
        return isinstance(o, V) and o.t == self.t and o.v == self.v

    def __hash__(self):
        return hash((self.t, self.v))


class D(list):
    """A dict container: list of (key, value) in file order, plus the header flags."""
    flags = 1

    def get(self, k, default=None):
        for a, b in self:
            if a == k:
                return b
        return default

    def set(self, k, v):
        for i, (a, b) in enumerate(self):
            if a == k:
                self[i] = (k, v)
                return
        self.append((k, v))


class L(list):
    flags = 0


def read(d):
    u = lambda o: struct.unpack_from('<I', d, o)[0]
    assert d[:4] == b'!pkg'
    root_p, vca_p = u(8) & 0x7fffffff, u(12) & 0x7fffffff
    assert d[vca_p:vca_p + 4] == b'!vca'
    ptrs = [u(vca_p + 8 + 4 * i) & 0x7fffffff for i in range(u(vca_p + 4))]
    pool = ptrs[0] + 8
    tables = {}
    order = ['str']
    for i, p in enumerate(ptrs[1:], 1):
        assert d[p:p + 4] == b'!vbi'
        name = d[p + 4:p + 8].decode()
        n, stride = u(p + 8), u(p + 12)
        fmt = FMT[name]
        order.append(name)
        vals = []
        for k in range(n):
            x = struct.unpack_from(fmt, d, p + 16 + k * stride)
            vals.append(x[0] if len(x) == 1 else x)
        tables[name] = vals

    def s(off):
        n = d[pool + off]
        return d[pool + off + 1:pool + off + 1 + n].decode('utf-8')

    def val(v):
        if v & 0x80000000:
            return node(v & 0x7fffffff)
        t, i = v >> 24, v & 0xffffff
        if t == 0:
            return V('str', s(i))
        return V(order[t], tables[order[t]][i])

    def node(o):
        tag = d[o:o + 4]
        w = u(o + 4)
        n, fl = w & 0xffffff, w >> 24
        if tag == b'!idi':
            r = D()
            r.flags = fl
            for i in range(n):
                k, v, h = struct.unpack_from('<III', d, o + 8 + 12 * i)
                key = s(k)
                assert fnv1a(key.encode()) == h
                r.append((key, val(v)))
            return r
        if tag == b'!ili':
            r = L()
            r.flags = fl
            for i in range(n):
                r.append(val(u(o + 8 + 4 * i)))
            return r
        raise ValueError(f'unknown chunk {tag} at {o:#x}')

    return node(root_p)


def _items(c):
    """A container's values in file order: a dict's by key hash."""
    if isinstance(c, D):
        return sorted(c, key=lambda kv: fnv1a(kv[0].encode()))
    return [(None, b) for b in c]


def assemble(root, align=4):
    """Encodes a tree as the game lays it out: containers breadth first, dict entries in hash order,
    strings and table values in first-use order, tables in first-use order."""
    pool = bytearray()
    strings = {}
    tables, index = {}, {}

    def sidx(x):
        if x not in strings:
            b = x.encode('utf-8')
            strings[x] = len(pool)
            pool.append(len(b))
            pool.extend(b)
            pool.append(0)
        return strings[x]

    leafref = {}

    def leaf(x):
        if x.t == 'str':
            return sidx(x.v)
        if x.t not in tables:
            tables[x.t], index[x.t] = [], {}
        ix = index[x.t]
        if x.t in NODEDUP:  # every use gets its own entry (the game's vec3 table holds duplicates)
            leafref[id(x)] = len(tables[x.t])
            tables[x.t].append(x.v)
            return leafref[id(x)]
        if x.v not in ix:
            ix[x.v] = len(tables[x.t])
            tables[x.t].append(x.v)
        return ix[x.v]

    order, q = [], [root]
    while q:
        nq = []
        for c in q:
            order.append(c)
            for k, b in _items(c):
                if k is not None:
                    sidx(k)
                if isinstance(b, (D, L)):
                    nq.append(b)
                else:
                    leaf(b)
        q = nq
    tnames = ['str'] + list(tables)
    off, pos = {}, 0x10
    for c in order:
        off[id(c)] = pos
        pos += 8 + (12 if isinstance(c, D) else 4) * len(c)
    out = bytearray(pos)

    def ref(b):
        if isinstance(b, (D, L)):
            return 0x80000000 | off[id(b)]
        if b.t == 'str':
            return strings[b.v]
        i = leafref[id(b)] if b.t in NODEDUP else index[b.t][b.v]
        return (tnames.index(b.t) << 24) | i

    for c in order:
        o = off[id(c)]
        if isinstance(c, D):
            struct.pack_into('<4sI', out, o, b'!idi', len(c) | (c.flags << 24))
            for i, (k, b) in enumerate(_items(c)):
                struct.pack_into('<III', out, o + 8 + 12 * i, strings[k], ref(b), fnv1a(k.encode()))
        else:
            struct.pack_into('<4sI', out, o, b'!ili', len(c) | (c.flags << 24))
            for i, b in enumerate(c):
                struct.pack_into('<I', out, o + 8 + 4 * i, ref(b))
    vca_at = len(out)
    hdr = bytearray(b'!vca' + struct.pack('<I', len(tnames)) + bytes(4 * len(tnames)))
    base = vca_at + len(hdr)
    body = bytearray(b'!sbi' + struct.pack('<I', len(pool)) + bytes(pool))
    ptrs = [base]
    for t in tnames[1:]:
        fmt = FMT[t]
        stride = struct.calcsize(fmt)
        while (base + len(body)) % max(align, min(stride, 16)):  # each table aligned to its stride
            body.append(0)
        ptrs.append(base + len(body))
        body += b'!vbi' + t.encode() + struct.pack('<II', len(tables[t]), stride)
        for v in tables[t]:
            body += struct.pack(fmt, *(v if isinstance(v, tuple) else (v,)))
    for i, p in enumerate(ptrs):
        struct.pack_into('<I', hdr, 8 + 4 * i, 0x80000000 | p)
    while (base + len(body)) % 4:  # the file is padded to 4 bytes
        body.append(0)
    res = bytearray(out + hdr + body)
    res[:4] = b'!pkg'
    struct.pack_into('<III', res, 4, 0, 0x80000010, 0x80000000 | vca_at)
    return bytes(res)
