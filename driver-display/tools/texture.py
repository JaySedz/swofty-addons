# Textures for the dash: a white image with its shape in the alpha channel (as the game's own LCD icons are, tinted
# by the group widget's colourOverride), written as the game's UI texture resources:
#   <name>.tif.image  GfxSurfaceRes, two fragments: a 40-byte head (version 2, 264, format 70 = BC7, width,
#                     height, depth 1, mips 1, array 1, 0, 0) and the BC7 blocks
#   <name>.tif        GfxSRVResource, one fragment: a 24-byte head (5, format 70, 0, 1, 0, 0) and the image's name
# (read from the stock LCD textures, ers_battery_icon and ers_activate_pill). The launcher's add-resource takes the
# fragment names, flags and compression from a resource of the same type and fragment count in the archive.
#
# The BC7 encoder writes mode 6 only (one subset, RGBA endpoints of 7 bits plus a p-bit, 4-bit indices). The colour
# is white everywhere, so the endpoints differ in alpha only, and the alpha of a block is stored to within the
# 16-step ramp between its lowest and highest values.
import struct

W4 = (0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64)  # BC7 4-bit interpolation weights


def _block(alphas):
    lo, hi = min(alphas), max(alphas)
    ramp = [((64 - w) * lo + w * hi + 32) >> 6 for w in W4]
    idx = [min(range(16), key=lambda i: abs(ramp[i] - a)) for a in alphas]
    a0, a1 = lo, hi
    if idx[0] >= 8:  # the anchor (pixel 0) index must have its top bit clear: swap the endpoints
        a0, a1 = a1, a0
        idx = [15 - i for i in idx]
    p0, p1 = a0 & 1, a1 & 1
    bits, n = 1 << 6, 7  # mode 6: six zero bits, then a one
    def put(v, width):
        nonlocal bits, n
        bits |= (v & ((1 << width) - 1)) << n
        n += width
    for _ in range(3):  # R, G, B: white at both ends (127 with the p-bit: 254 or 255)
        put(127, 7)
        put(127, 7)
    put(a0 >> 1, 7)
    put(a1 >> 1, 7)
    put(p0, 1)
    put(p1, 1)
    put(idx[0], 3)
    for i in idx[1:]:
        put(i, 4)
    assert n == 128
    return bits.to_bytes(16, 'little')


def bc7_white(alpha, w, h):
    """BC7 blocks for a w x h alpha image (bytes, row by row); w and h multiples of 4."""
    assert w % 4 == 0 and h % 4 == 0
    out = bytearray()
    empty = None
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            a = [alpha[(by + y) * w + bx + x] for y in range(4) for x in range(4)]
            if not any(a):
                if empty is None:
                    empty = _block(a)
                out += empty
            else:
                out += _block(a)
    return bytes(out)


def surface_head(w, h):
    return struct.pack('<10I', 2, 264, 70, w, h, 1, 1, 1, 0, 0)


def view(image_name, slice_index=None):
    """A texture view (GfxSRVResource): a 24-byte head (dimension, format 70, first mip, mips, first slice, slices)
    and the image's name. Dimension 5 is a plain texture (slices 0); 6 an array, here one slice of it (the game's
    own array views read 6, 70, 0, 1, 0, <count>)."""
    if slice_index is None:
        return struct.pack('<6I', 5, 70, 0, 1, 0, 0) + image_name.encode('ascii') + b'\0'
    return struct.pack('<6I', 6, 70, 0, 1, slice_index, 1) + image_name.encode('ascii') + b'\0'


def dds(w, h, blocks):
    """A DDS file around BC7 blocks (DX10 header, format 98), for checking a texture with an image viewer."""
    hdr = bytearray(128)
    hdr[0:4] = b'DDS '
    struct.pack_into('<7I', hdr, 4, 124, 0x1007 | 0x80000, h, w, len(blocks), 0, 1)
    struct.pack_into('<2I', hdr, 76, 32, 4)
    hdr[84:88] = b'DX10'
    return bytes(hdr) + struct.pack('<5I', 98, 3, 0, 1, 0) + blocks


# ---- materials and their bindings (read from the stock LCD's lcd_default.material and lcd_display.emb)
# A material (GfxMaterialRes): an offset table, the parameters, then strings; its texture's view path is stored
# twice at the end, its length in the table. A path of the stock one's length (46 characters) is swapped in place,
# with no offset to move. me_colour multiplies the texture: its RGB floats at 0xD4, its alpha at 0xE0.
STOCK_TEXTURE = b'eaid://ui_package/art/textures/small_white.tif'


def material(stock, view_name, rgba, uber=False):
    """lcd_default.material with another texture view (a 46-character path) and colour (0..255 each); uber turns on
    me_useUberData (the float at 0xF8, after me_colour, the heap index and me_finalColourTint)."""
    new = view_name.encode('ascii')
    assert len(new) == len(STOCK_TEXTURE), view_name
    assert stock.count(STOCK_TEXTURE) == 2 and struct.unpack_from('<3f', stock, 0xD4) == (1.0, 1.0, 1.0)
    d = bytearray(stock.replace(STOCK_TEXTURE, new))
    struct.pack_into('<3f', d, 0xD4, *(c / 255.0 for c in rgba[:3]))
    struct.pack_into('<f', d, 0xE0, rgba[3] / 255.0 if len(rgba) > 3 else 1.0)
    if uber:
        assert struct.unpack_from('<f', stock, 0xF8)[0] == 0.0
        struct.pack_into('<f', d, 0xF8, 1.0)
    return bytes(d)


def binding(entries):
    """A material binding (GfxMatBindRes, .emb): mesh slot name -> material path, for (slot, material) pairs. Header:
    entries, distinct materials, the material block's offset after the header; the (name, material) offset pairs;
    the names; the material paths. (Rebuilds the stock lcd_display.emb byte for byte.)"""
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
