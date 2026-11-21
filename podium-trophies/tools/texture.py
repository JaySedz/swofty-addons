# Textures for the podium trophies: PNG -> the game's texture resources, and back to PNG for checking.
#
# Read from the trophy textures in cinematicspodium.erp and the cabinet trophy archives:
#   <name>.tif.image  GfxSurfaceRes: a 40-byte head (2, 264, format, width, height, 1, mips, 1, 0, 0) and the blocks
#                     of every mip, largest first. The streamed ones have a third fragment naming a loose
#                     .2025.mipmaps file with the larger mips; the podium archive also has two-fragment surfaces up to
#                     1024 wide (flags, the team crew textures) that keep the whole chain inside, which is what this
#                     writes, so no loose file is needed.
#   <name>.tif        GfxSRVResource: a 24-byte head (5, format, 0, mips, 0, 0) and the image's name.
# Formats (the game's own numbers, identified by decoding the trophy textures):
#   54  BC1, colour (_d)                       52  BC1, surface map (_s)
#   65  BC5, normal map (_n): X in red, Y in green, DirectX style (green down: a curl test on the stock normal maps
#       fits the green-down field 1.4 to 2.5 times better than green-up)
# The surface map's blue channel is metalness (255 on the gold and silver, 0 on the ribbon and the marble base); red
# and green are probably occlusion and gloss. Presets are values read off the cabinet trophy's own map.
import io
import struct

import numpy as np

FMT_COLOUR, FMT_SURFACE, FMT_NORMAL = 54, 52, 65
BLOCK_BYTES = {52: 8, 54: 8, 65: 16}
SURFACE_PRESETS = {
    'metal': (255, 211, 255),  # the cabinet trophy's polished gold body
    'plain': (255, 178, 0),    # its red ribbon (cloth, not metal)
}
MAX_SIZE = 2048


# ---- encoders

def _blocks(img):
    """H x W x C -> (H/4 * W/4, 16, C), block rows top to bottom, pixels row by row."""
    h, w, c = img.shape
    return img.reshape(h // 4, 4, w // 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(-1, 16, c)


def _565(c):
    c = np.clip(np.rint(c), 0, 255)
    return ((np.rint(c[..., 0] * 31 / 255).astype(np.int64) << 11) | (np.rint(c[..., 1] * 63 / 255).astype(np.int64) << 5)
            | np.rint(c[..., 2] * 31 / 255).astype(np.int64))


def _888(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.float64)


def _bc1_fit(px, e0, e1):
    """Quantized endpoints (c0 > c1, four-colour mode) and the indices and error for them."""
    c0, c1 = _565(e0), _565(e1)
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    p0, p1 = _888(c0), _888(c1)
    pal = np.stack([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3], 1)  # (n, 4, 3)
    d = ((px[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)  # (n, 16, 4)
    idx = d.argmin(-1)
    flat = c0 == c1  # one colour: index 0 everywhere (the three-colour mode's c0)
    idx[flat] = 0
    err = np.take_along_axis(d, idx[..., None], -1)[..., 0].sum(-1)
    return c0, c1, idx, err


def bc1(img):
    """BC1 blocks for an H x W x 3 uint8 image (H and W multiples of 4)."""
    px = _blocks(img.astype(np.float64))
    mean = px.mean(1, keepdims=True)
    x = px - mean
    cov = np.einsum('nki,nkj->nij', x, x)
    axis = np.ones((len(px), 3)) / np.sqrt(3)
    for _ in range(8):  # power iteration: the colour line of each block
        axis = np.einsum('nij,nj->ni', cov, axis)
        axis /= np.maximum(np.linalg.norm(axis, axis=1, keepdims=True), 1e-12)
    t = np.einsum('nki,ni->nk', x, axis)
    e0 = mean[:, 0] + axis * t.max(1, keepdims=True)
    e1 = mean[:, 0] + axis * t.min(1, keepdims=True)
    best = _bc1_fit(px, e0, e1)
    # one least-squares pass: the endpoints that best fit the chosen indices
    w = np.array([[1, 0], [0, 1], [2 / 3, 1 / 3], [1 / 3, 2 / 3]])[best[2]]  # (n, 16, 2)
    a = np.einsum('nki,nkj->nij', w, w) + np.eye(2) * 1e-6
    b = np.einsum('nki,nkc->nic', w, px)
    e = np.linalg.solve(a, b)  # (n, 2, 3)
    ref = _bc1_fit(px, e[:, 0], e[:, 1])
    better = ref[3] < best[3]
    c0, c1, idx, _ = (np.where(better if v.ndim == 1 else better[:, None], r, v) for v, r in zip(best, ref))
    bits = (idx.astype(np.uint64) << (2 * np.arange(16, dtype=np.uint64))).sum(1, dtype=np.uint64)
    out = np.zeros(len(px), dtype=[('c0', '<u2'), ('c1', '<u2'), ('i', '<u4')])
    out['c0'], out['c1'], out['i'] = c0, c1, bits
    return out.tobytes()


def _bc4(ch):
    """BC4 blocks (8 bytes) for (n, 16) values 0..255: the block's max and min as endpoints, eight-value mode."""
    hi, lo = ch.max(1), ch.min(1)
    hi_i, lo_i = np.rint(hi).astype(np.int64), np.rint(lo).astype(np.int64)
    ramp = np.stack([hi_i, lo_i] + [((7 - i) * hi_i + i * lo_i) / 7 for i in range(1, 7)], 1)  # codes 0..7
    idx = np.abs(ch[:, :, None] - ramp[:, None, :]).argmin(-1)
    idx[hi_i == lo_i] = 0
    bits = (idx.astype(np.uint64) << (3 * np.arange(16, dtype=np.uint64))).sum(1, dtype=np.uint64)
    out = np.zeros((len(ch), 8), np.uint8)
    out[:, 0], out[:, 1] = hi_i, lo_i
    for k in range(6):
        out[:, 2 + k] = (bits >> np.uint64(8 * k)) & np.uint64(255)
    return out


def bc5(img):
    """BC5 blocks for an H x W x 2 uint8 image: red then green, each a BC4 block."""
    px = _blocks(img.astype(np.float64))
    return np.concatenate([_bc4(px[:, :, 0]), _bc4(px[:, :, 1])], 1).tobytes()


# ---- mips

def _lin(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def _srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055) * 255.0


def _half(a):
    return (a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2]) / 4


def mip_chain(img, kind):
    """Mips down to 4 x 4 (the trophy textures stop there): kind 'colour' averages in linear light, 'normal' averages
    the vectors and renormalizes (img is then H x W x 3 floats, -1..1), 'surface' averages the values."""
    out = [img]
    cur = _lin(img.astype(np.float64)) if kind == 'colour' else img.astype(np.float64)
    while min(cur.shape[:2]) > 4:
        cur = _half(cur)
        if kind == 'normal':
            cur = cur / np.maximum(np.linalg.norm(cur, axis=-1, keepdims=True), 1e-6)
            out.append(cur)
        else:
            out.append(np.clip(np.rint(_srgb(cur) if kind == 'colour' else cur), 0, 255).astype(np.uint8))
    return out


# ---- the game's resources

def surface_head(fmt, w, h, mips):
    return struct.pack('<10I', 2, 264, fmt, w, h, 1, mips, 1, 0, 0)


def view(fmt, mips, image_name):
    return struct.pack('<6I', 5, fmt, 0, mips, 0, 0) + image_name.encode('ascii') + b'\0'


def _pow2(n):
    p = 4
    while p * 2 <= n:
        p *= 2
    if n - p > p * 2 - n and p * 2 <= MAX_SIZE:  # nearest power of two
        p *= 2
    return min(p, MAX_SIZE)


def _load(path, mode):
    from PIL import Image
    im = Image.open(path).convert(mode)
    w, h = _pow2(im.width), _pow2(im.height)
    if (w, h) != im.size:
        im = im.resize((w, h), Image.LANCZOS)
    return np.array(im)


def encode(kind, src, opengl_normal=True):
    """(format, w, h, mips, pixel bytes) for one map. kind: 'colour', 'normal' or 'surface'. src: a PNG path; for
    'surface' also a preset name or an [r, g, b] value, and for 'normal' None (flat), both made 4 x 4."""
    if kind == 'colour':
        img = _load(src, 'RGB')
        chain, fmt = mip_chain(img, 'colour'), FMT_COLOUR
        data = b''.join(bc1(m) for m in chain)
    elif kind == 'surface':
        if isinstance(src, str) and src in SURFACE_PRESETS:
            src = SURFACE_PRESETS[src]
        if isinstance(src, (list, tuple)):
            img = np.tile(np.array(src, np.uint8), (4, 4, 1))
        else:
            img = _load(src, 'RGB')
        chain, fmt = mip_chain(img, 'surface'), FMT_SURFACE
        data = b''.join(bc1(m) for m in chain)
    elif kind == 'normal':
        if src is None:
            v = np.tile(np.array([0.0, 0.0, 1.0]), (4, 4, 1))
        else:
            v = _load(src, 'RGB').astype(np.float64) / 127.5 - 1.0
            if opengl_normal:  # Blender, Substance's OpenGL preset: green up -> the game's green down
                v[..., 1] = -v[..., 1]
            v /= np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-6)
        chain, fmt = mip_chain(v, 'normal'), FMT_NORMAL
        data = b''.join(bc5(np.clip(np.rint((m[..., :2] + 1) * 127.5), 0, 255).astype(np.uint8)) for m in chain)
    else:
        raise ValueError(kind)
    return fmt, chain[0].shape[1], chain[0].shape[0], len(chain), data


# ---- decoding (Pillow), for checks and for exporting the game's textures

def _dds(w, h, blocks, dxgi):
    hdr = bytearray(128)
    hdr[0:4] = b'DDS '
    struct.pack_into('<7I', hdr, 4, 124, 0x1007 | 0x80000, h, w, len(blocks), 0, 1)
    struct.pack_into('<2I', hdr, 76, 32, 4)
    hdr[84:88] = b'DX10'
    return bytes(hdr) + struct.pack('<5I', dxgi, 3, 0, 1, 0) + blocks


DXGI = {52: 71, 54: 71, 65: 83, 57: 77, 55: 74, 69: 98, 70: 98}  # 57/55/69 unverified guesses


def decode(head, pixels):
    """The top mip of a GfxSurfaceRes as a PIL image (RGB; a normal map's Z is rebuilt into blue)."""
    from PIL import Image
    _, _, fmt, w, h = struct.unpack_from('<5I', head)
    bpb = BLOCK_BYTES.get(fmt, 16)
    top = pixels[:max(1, w // 4) * max(1, h // 4) * bpb]
    im = Image.open(io.BytesIO(_dds(w, h, top, DXGI[fmt])))
    im.load()
    if fmt == FMT_NORMAL:
        a = np.array(im.convert('RGB')).astype(np.float64) / 127.5 - 1
        z = np.sqrt(np.clip(1 - a[..., 0] ** 2 - a[..., 1] ** 2, 0, 1))
        a[..., 2] = z
        im = Image.fromarray(np.clip(np.rint((a + 1) * 127.5), 0, 255).astype(np.uint8))
    return im.convert('RGB')


# ---- materials

def retexture(material, swaps):
    """A material (GfxMaterialRes) with its texture paths swapped: {old view path: new view path}. Each path is
    stored twice at the end of the resource, its length in the offset table, so each new one has the old one's
    length (texture_names makes them so)."""
    out = material
    for old, new in swaps.items():
        o, n = old.encode('ascii'), new.encode('ascii')
        assert len(o) == len(n), (old, new)
        assert out.count(o + b'\0') == 2, old
        out = out.replace(o + b'\0', n + b'\0')
    return out


def texture_name(old, tag):
    """A view path with old's length: old's folder, then 'pt<hash of tag>_<tag>' cut or padded with x, then old's
    _d.tif ending. The hash keeps two cut tags apart."""
    import hashlib
    import re
    folder, base = old.rsplit('/', 1)
    end = base[base.rindex('_'):]  # _d.tif
    room = len(base) - len(end)
    h = hashlib.sha1(tag.encode()).hexdigest()[:6]
    slug = re.sub(r'[^a-z0-9]+', '_', tag.lower()).strip('_')
    assert room >= 9, old
    stem = (f'pt{h}_{slug}')[:room].ljust(room, 'x')
    return f'{folder}/{stem}{end}'


def selftest():
    """Re-encodes the stock Bahrain podium cup's maps (decoded, then encoded as a PNG would be) and checks the error."""
    import os
    import sys
    import tempfile
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import trophy
    from PIL import Image
    arc = trophy.Archive(os.path.join(trophy.GAME, trophy.PODIUM))
    tmp = tempfile.mkdtemp()
    ok = True
    for kind, end, limit in (('colour', 'd', 2.0), ('normal', 'n', 1.0), ('surface', 's', 2.0)):
        head, pixels = arc.frags(f'eaid://environment_package/core_assets/objects/textures/trophy/'
                                 f'trophy_closed_a_01_{end}.tif.image')[:2]
        ref = np.array(decode(head, pixels))
        src = ref.copy()
        if kind == 'normal':
            src[..., 1] = 255 - src[..., 1]  # as an export writes it (OpenGL)
        p = os.path.join(tmp, f'{end}.png')
        Image.fromarray(src).save(p)
        fmt, w, h, mips, data = encode(kind, p)
        out = np.array(decode(surface_head(fmt, w, h, mips), data))
        err = np.abs(out[..., :2 if kind == 'normal' else 3].astype(float) - ref[..., :out.shape[-1]][..., :2 if kind == 'normal' else 3]).mean()
        good = err <= limit and fmt == struct.unpack_from('<I', head, 8)[0]
        ok &= good
        print(f'{kind}: format {fmt}, {w} x {h}, {mips} mips, mean error {err:.2f} levels', 'ok' if good else 'FAIL')
    print('texture selftest', 'passed' if ok else 'FAILED')
    return ok


if __name__ == '__main__':
    import sys
    sys.exit(0 if selftest() else 1)
