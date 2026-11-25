"""The 2026 Ferrari's steering wheel screen, mapped straight (1.2.20; tested as the separate Ferrari Screen Fix 0.1.1).

The screen part of that car's steering wheel model (the 'lcd' submesh of ferrari.idf?lod=N&steering_wheel&mesh in
f1_2026_vehicle_package/teams/ferrari/wep/ferrari.erp) is flat, but its texture coordinates aren't: rows squeezed
near the top, the edge columns higher than the middle. Everything the wheel shows bowed and lost its top, which
Driver Display worked around until 1.2.19 with a bent copy of every element. This rewrites the screen's coordinates
straight, u across the width and v over BAND (the whole 942 px display over the screen's height, measured from a
test photo: 'full', which the user confirmed shows everything), in the model's shared texture coordinate buffer
(lod=all&vb=9, half4 a vertex) for each detail level that has the screen: lod 0 (base vertex 0), lod 1 (68315),
lod 2 (100902). Nothing else in the buffer changes.

screen_buffer(game) returns the buffer's data fragment (fragment 1) for a replace-resource op. A game update that
changes the 2026 Ferrari's model needs a rebuild: the addon ships this copy of the whole buffer.
"""
import os

import numpy as np

import erp
import gfxmesh

REL = '2025_asset_groups/f1_2026_vehicle_package/teams/ferrari/wep/ferrari.erp'
IDF = 'eaid://f1_2026_vehicle_package/teams/ferrari/idf/ferrari.idf?'
VB_UV = IDF + 'lod=all&vb=9'
BAND = (0.138, 0.787)  # the display's band of the screen texture, top to bottom

# 1.3.2: the 2025 Ferrari's wheel (f1_2025_vehicle_package) has its own screen: straight, but taller, over the band
# 0.146..0.854, so the 2026 Ferrari's pages came out squashed with a strip under them. Mapped onto the 2026 car's
# band it shows them as that car does. Its positions are vb=0, the screen's coordinates vb=3 (54 vertices, lods 0-3).
CARS = {
    '2026': dict(rel=REL, idf=IDF, pos=6, uv=9, z=-0.1405, half=0.0475, count=102),
    '2025': dict(rel='2025_asset_groups/f1_2025_vehicle_package/teams/ferrari/wep/ferrari.erp',
                 idf='eaid://f1_2025_vehicle_package/teams/ferrari/idf/ferrari.idf?', pos=0, uv=3, z=-0.1050,
                 half=0.0485, count=54),
}


def screen_buffer(game, car='2026'):
    c = CARS[car]
    idf = c['idf']
    vb_uv = idf + f"lod=all&vb={c['uv']}"
    data = open(os.path.join(game, c['rel'].replace('/', os.sep)), 'rb').read()
    ents = {n: fr for n, t, fr in erp.entries(data)}

    def frag(name, i):
        return erp.fragment(data, ents[name][i])

    ib = np.frombuffer(frag(idf + 'lod=all&ib=0', 1), dtype='<u2')
    pos = np.frombuffer(frag(idf + f"lod=all&vb={c['pos']}", 1), dtype='<f4').reshape(-1, 3)
    kind, stride, count, fmt = gfxmesh.read_buffer(frag(vb_uv, 0), frag(vb_uv, 1))
    assert (kind, stride, fmt) == (9, 8, 35), (kind, stride, fmt)  # a vertex buffer of half4
    uv = np.frombuffer(frag(vb_uv, 1), dtype='<f2').reshape(-1, 4).copy()
    screen = set()
    for lod in range(4):
        name = idf + f'lod={lod}&steering_wheel&mesh'
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
            vs = s['raw'][5] + np.unique(ib[s['start']:s['start'] + s['count']].astype(int))
            p = pos[vs]
            # still the flat screen this was measured on (a changed model stops the build)
            assert np.all(np.abs(p[:, 2] - c['z']) < 0.002) and np.all(np.abs(p[:, 0]) < c['half']), (lod, p.min(0))
            screen |= set(vs.tolist())
    assert len(screen) == c['count'], len(screen)
    vs = np.array(sorted(screen))
    p = pos[vs]
    x0, x1, y0, y1 = p[:, 0].min(), p[:, 0].max(), p[:, 1].min(), p[:, 1].max()
    top, bottom = BAND
    uv[vs, 0] = ((x1 - p[:, 0]) / (x1 - x0)).astype(np.float16)
    uv[vs, 1] = (top + (y1 - p[:, 1]) / (y1 - y0) * (bottom - top)).astype(np.float16)
    out = uv.tobytes()
    assert len(out) == len(frag(vb_uv, 1))
    return out
