"""Ferrari Screen Fix: straightens the 2026 Ferrari steering wheel screen.

The screen part of the 2026 Ferrari's steering wheel model (the 'lcd' submesh of
ferrari.idf?lod=N&steering_wheel&mesh in f1_2026_vehicle_package/teams/ferrari/wep/ferrari.erp) is flat: its depth
varies by under a millimetre over 94 x 67 mm. Its texture coordinates are not: rows are squeezed near the top and
the edge columns sit higher than the middle, which bows whatever the wheel displays (the game's own screen too) and
loses part of the top. Other cars map theirs straight (the 2026 McLaren: u = -10.92 x + 0.505, v = -10.13 y + 0.945,
exact). This rewrites the Ferrari screen's coordinates as a straight mapping (u from x, v from y) in the model's
shared texture coordinate buffer (lod=all&vb=9: half4 per vertex), for each detail level that has the screen:
lod 0 (base vertex 0), lod 1 (68315) and lod 2 (100902); lod 3 has none.

Usage: python tools/build_fix.py <variant>
  stock  : the straight line through the stock coordinates (the game's own framing, without the bow)
  fit    : v 0.218..0.782 (the McLaren's band) over the whole screen height. 0.1.0 test: flat, but the top
           ~116 canvas px were cut and the display stretched (the display's band is wider than the McLaren's)
  full   : v 0.138..0.787, the whole 942 px display over the screen height, from the fit test's photo (the screen
           showed canvas y ~116..935 with fit: about 1452 px of the screen texture per 1.0 of v). Nothing cut off;
           taller than on other cars (the Ferrari's screen is a taller shape: 1.40 wide to 1 high, the display 1.77)
  shape  : v 0.052..0.873, the display at its own proportions (as wide as the screen, as tall as that makes it),
           centred; dark bands above and below it
Writes erp/vb9.bin (the buffer's data fragment) and erp/ferrari_screen_fix.json.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import erp  # noqa: E402
import gfxmesh  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = r'E:\Steam\steamapps\common\F1 25'
REL = '2025_asset_groups/f1_2026_vehicle_package/teams/ferrari/wep/ferrari.erp'
IDF = 'eaid://f1_2026_vehicle_package/teams/ferrari/idf/ferrari.idf?'
VB_UV = IDF + 'lod=all&vb=9'

variant = sys.argv[1] if len(sys.argv) > 1 else 'stock'
BANDS = {'fit': (0.218, 0.782), 'full': (0.138, 0.787), 'shape': (0.052, 0.873)}
assert variant == 'stock' or variant in BANDS, variant
data = open(os.path.join(GAME, REL.replace('/', os.sep)), 'rb').read()
ents = {n: fr for n, t, fr in erp.entries(data)}


def frag(name, i):
    return erp.fragment(data, ents[name][i])


ib = np.frombuffer(frag(IDF + 'lod=all&ib=0', 1), dtype='<u2')
pos = np.frombuffer(frag(IDF + 'lod=all&vb=6', 1), dtype='<f4').reshape(-1, 3)
uv_head = frag(VB_UV, 0)
kind, stride, count, fmt = gfxmesh.read_buffer(uv_head, frag(VB_UV, 1))
assert (kind, stride, fmt) == (9, 8, 35), (kind, stride, fmt)  # vertex buffer, half4
uv = np.frombuffer(frag(VB_UV, 1), dtype='<f2').reshape(-1, 4).copy()

# the screen's vertices, every detail level (submesh raw[5] is its base vertex)
screen = set()
for lod in range(4):
    name = IDF + f'lod={lod}&steering_wheel&mesh'
    if name not in ents:
        continue
    d = gfxmesh.parse(frag(name, 0))
    offs, o = {}, 0
    for nm in d['mat_names']:
        offs[o] = nm
        o += len(nm) + 1
    for s in d['subs']:
        if offs[s['mat_off']] != 'lcd':
            continue
        base = s['raw'][5]
        vs = base + np.unique(ib[s['start']:s['start'] + s['count']].astype(int))
        p = pos[vs]
        assert np.all(np.abs(p[:, 2] + 0.1405) < 0.002) and np.all(np.abs(p[:, 0]) < 0.0475), (lod, p.min(0), p.max(0))
        print(f'lod {lod}: {len(vs)} screen vertices from {vs.min()}')
        screen |= set(vs.tolist())
vs = np.array(sorted(screen))
p = pos[vs]
x0, x1 = p[:, 0].min(), p[:, 0].max()
y0, y1 = p[:, 1].min(), p[:, 1].max()  # y up: y1 the top edge

# u: straight across, 0 at +x (the screen's left as the driver sees it, as the stock and the McLaren have it)
u = (x1 - p[:, 0]) / (x1 - x0)
if variant == 'stock':
    A = np.c_[p[:, 1], np.ones(len(p))]
    k, c = np.linalg.lstsq(A, uv[vs, 1].astype(np.float64), rcond=None)[0]
    v = k * p[:, 1] + c
else:
    top, bottom = BANDS[variant]
    v = top + (y1 - p[:, 1]) / (y1 - y0) * (bottom - top)
old = uv[vs, :2].astype(np.float64)
uv[vs, 0] = u.astype(np.float16)
uv[vs, 1] = v.astype(np.float16)
print(f'{variant}: u {u.min():.3f}..{u.max():.3f}, v {v.min():.3f}..{v.max():.3f}; '
      f'largest change u {np.abs(u - old[:, 0]).max():.3f}, v {np.abs(v - old[:, 1]).max():.3f}')

out = uv.tobytes()
assert len(out) == len(frag(VB_UV, 1))
os.makedirs(os.path.join(ROOT, 'erp'), exist_ok=True)
open(os.path.join(ROOT, 'erp', 'vb9.bin'), 'wb').write(out)
ops = {'schema_version': 1, 'ops': [
    {'op': 'replace-resource', 'erp': REL, 'resource': VB_UV, 'fragment': 1, 'source': 'erp/vb9.bin'}]}
json.dump(ops, open(os.path.join(ROOT, 'erp', 'ferrari_screen_fix.json'), 'w'), indent=2)
print('wrote erp/vb9.bin, erp/ferrari_screen_fix.json')
