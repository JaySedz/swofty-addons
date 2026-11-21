# Binary XML ("\x01BXML") as the podium scene and sequences store it, and the TSMD string stats that go with it.
#
# A file is "\x01BXML" and then records: u16 length (of the record after this field), u8 kind, u8 flag, u16 count,
# body. Kind 0 opens an element (name, then count attribute key/value pairs, all NUL-terminated) whose children
# follow until a kind 5 record closes it; kind 1 is text; kind 6 records trail the root. Elements here are
# [name, attrs, children, flag, end_flag]; text is ('text', bytes, flag).
import struct


def parse(data):
    if data[:5] != b'\x01BXML':
        raise ValueError('not binary XML')
    pos = 5

    def rec():
        nonlocal pos
        ln, kind, flag, n = struct.unpack_from('<HBBH', data, pos)
        body = data[pos + 6:pos + 2 + ln]
        pos += 2 + ln
        return kind, flag, n, body

    def element(flag, n, body):
        parts = body.split(b'\0')
        name, kv = parts[0], parts[1:1 + 2 * n]
        el = [name, [(kv[2 * i], kv[2 * i + 1]) for i in range(n)], [], flag, 0]
        while True:
            kind, f, m, b = rec()
            if kind == 5:
                el[4] = f
                return el
            if kind == 0:
                el[2].append(element(f, m, b))
            elif kind == 1:
                el[2].append(('text', b[:-1], f))
            else:
                raise ValueError(f'record kind {kind} inside an element')

    kind, flag, n, body = rec()
    if kind != 0:
        raise ValueError('the root is not an element')
    root = element(flag, n, body)
    tail = []
    while pos < len(data):
        kind, flag, _, _ = rec()
        if kind != 6:
            raise ValueError(f'record kind {kind} after the root')
        tail.append(flag)
    return root, tail


def write(root, tail=()):
    out = bytearray(b'\x01BXML')

    def rec(kind, flag, n, body):
        out.extend(struct.pack('<HBBH', len(body) + 4, kind, flag, n) + body)

    def element(el):
        name, attrs, kids, flag, end = el
        rec(0, flag, len(attrs), name + b'\0' + b''.join(k + b'\0' + v + b'\0' for k, v in attrs))
        for k in kids:
            if k[0] == 'text':
                rec(1, k[2], 0, k[1] + b'\0')
            else:
                element(k)
        rec(5, end, 0, b'')

    element(root)
    for f in tail:
        rec(6, f, 0, b'')
    return bytes(out)


def elements(el):
    yield el
    for k in el[2]:
        if k[0] != 'text':
            yield from elements(k)


def attr(el, key):
    return next((v for k, v in el[1] if k == key), None)


def set_attr(el, key, value):
    el[1] = [(k, value if k == key else v) for k, v in el[1]]


def clone(el):
    if el[0] == 'text':
        return el
    return [el[0], list(el[1]), [clone(k) for k in el[2]], el[3], el[4]]


def stats(root):
    """The 28 TSMD counts: count and total length of each string group, then of its distinct strings."""
    els = list(elements(root))
    texts = [k[1] for e in els for k in e[2] if k[0] == 'text']
    keys = [k for e in els for k, _ in e[1]]
    vals = [v for e in els for _, v in e[1]]
    names = [e[0] for e in els]
    groups = [keys, vals, names, texts, names + keys, vals + texts, names + keys + vals + texts]
    out = []
    for g in groups:
        out += [len(g), sum(map(len, g))]
    for g in groups:
        u = set(g)
        out += [len(u), sum(map(len, u))]
    return out


def tsmd(root, stock_frag0=None, stock_tsmd=None):
    """TSMD stats for root. With the stock pair, any margin the stock header had over its own file is kept, so the
    header never asks for less memory than the game's own did."""
    s = stats(root)
    version = 1
    if stock_tsmd:
        version, *old = struct.unpack_from('<29I', stock_tsmd, 4)
        mine = stats(parse(stock_frag0)[0])
        s = [a + max(0, o - m) for a, o, m in zip(s, old, mine)]
    return b'TSMD' + struct.pack('<29I', version, *s)
