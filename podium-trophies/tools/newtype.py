# New podium trophy types: a type of its own a track, beside the game's ten.
#
# The game spawns the trophy by the track's trophyType string: the scene of that name in podiums2019.scene (one
# MultipartActor, mesh <idf dir>/<type>_01) and, in each of the five trophy sequences, the winner's clip
# "~driver_placed_first_<type>". No type name is in the game's code. A new type is therefore:
#   - the donor's model resources under the new name (GfxModelRes, ModelProxyRes, GfxMatBindRes, GfxMeshRes and
#     its buffers), with the names inside them changed;
#   - a copy of the donor's scene and of its five winner's clips under the new name (the donor's hand animation is
#     kept, so a donor of a similar shape fits best);
#   - database sets that point the tracks at it (tools/track_trophies.json lists where each track sets it).
# The launcher helper (helper/src) does the last three at build; this module names the model and checks the edits.
import re

import bxml
import gfxmesh
import trophy

SCENE = 'eaid://animation_package/scenes/cinematics/podiums2019.scene'
SEQUENCES = [f'eaid://animation_package/sequences/cinematics/podium/podium2019_trophy_seq0{n}.sequence'
             for n in range(1, 6)]
# Every stock archive that carries the scene or the sequences (all copies are identical): each gets the edited copy,
# or the game may load the stock one.
CINEMATICS = '2025_asset_groups/animation_package/scenes/cinematics/'
SCENE_ERPS = [CINEMATICS + p for p in (
    'cinematicspodium.erp', 'cinematicspodiummonaco.erp', 'podium_car_filler_01.erp',
    'podium/podium2022_celebrate_parcferme_01.erp', 'podium/podium2022_celebrate_parcferme_02.erp',
    'podium/podium2022_celebrate_pitlane.erp', 'podium/podium2022_celebrate_pitlane_02.erp')]
SEQUENCE_ERPS = [CINEMATICS + 'cinematicspodium.erp', CINEMATICS + 'cinematicspodiummonaco.erp']
NAME = re.compile(r'^trophy_[a-z0-9_]{1,40}$')


def check_name(t, stock):
    if not NAME.match(t):
        raise SystemExit(f'{t}: a trophy type is "trophy_" and then lower-case letters, digits or _')
    if t in stock:
        raise SystemExit(f'{t} is a stock trophy type; use "donor" without "type" to replace it')


def stock_types(podium):
    root, _ = bxml.parse(podium.frags(SCENE)[0])
    return {bxml.attr(s, b'name').decode() for s in root[2]
            if s[0] == b'scene' and (bxml.attr(s, b'name') or b'').startswith(b'trophy_')}


def renamed_model(podium, donor_name, out, new_type):
    """The donor's model resources from build_slot's out (plus its render proxy) under new_type's name:
    [(resource, resource type, [fragments])]."""
    old = trophy.podium_idf(donor_name)[:-len('.idf')]
    new = trophy.TROPHY_DIR + new_type + '_01'

    def ren(name):
        if not name.startswith(old):
            raise SystemExit(f'{name}: not a resource of {old}')
        return new + name[len(old):]

    res = []
    for name, frs in out.items():
        rtype = podium.res[name][0]
        frs = list(frs)
        if rtype == 'GfxMeshRes':
            d = gfxmesh.parse(frs[0])
            d['vb_names'] = [ren(x) for x in d['vb_names']]
            d['ib_name'] = ren(d['ib_name'])
            frs[0] = gfxmesh.build(d)
        elif rtype == 'GfxModelRes':
            frs[0] = frs[0].replace((old + '.idf?lod=0&mesh').encode() + b'\0',
                                    (new + '.idf?lod=0&mesh').encode() + b'\0', 1)
            if (new + '.idf?lod=0&mesh').encode() not in frs[0]:
                raise SystemExit(f'{name}: the mesh name was not where it was expected')
        res.append((ren(name), rtype, frs))
    render = old + '.idf?render'
    res.append((ren(render), podium.res[render][0], podium.frags(render)))
    return res


def edit_scene(frag0, types):
    """podiums2019.scene with a copy of each donor's scene for each new type: {new type: donor}."""
    root, tail = bxml.parse(frag0)
    scenes = {bxml.attr(s, b'name'): s for s in root[2] if s[0] == b'scene'}
    last = max(i for i, s in enumerate(root[2]) if s[0] == b'scene' and bxml.attr(s, b'name').startswith(b'trophy_'))
    for k, (t, d) in enumerate(sorted(types.items())):
        src = scenes.get(d.encode())
        if src is None:
            raise SystemExit(f'{d}: no such scene in podiums2019.scene')
        s = bxml.clone(src)
        bxml.set_attr(s, b'name', t.encode())
        for e in bxml.elements(s):
            if bxml.attr(e, b'id') == d.encode():
                bxml.set_attr(e, b'id', t.encode())
            mesh = bxml.attr(e, b'mesh')
            if mesh is not None:
                if not mesh.startswith(trophy.TROPHY_DIR.encode()):
                    raise SystemExit(f'{d}: its scene names {mesh}')
                bxml.set_attr(e, b'mesh', (trophy.TROPHY_DIR + t + '_01').encode())
        root[2].insert(last + 1 + k, s)
    return bxml.write(root, tail)


def edit_sequence(frag0, types):
    """A trophy sequence with a copy of the donor's winner's clip for each new type."""
    root, tail = bxml.parse(frag0)
    for t, d in sorted(types.items()):
        want = b'~driver_placed_first_' + d.encode()
        i = next((i for i, c in enumerate(root[2]) if c[0] == b'NeSequenceClip' and bxml.attr(c, b'name') == want),
                 None)
        if i is None:
            raise SystemExit(f'{d}: a trophy sequence has no {want.decode()} clip')
        c = bxml.clone(root[2][i])
        bxml.set_attr(c, b'name', b'~driver_placed_first_' + t.encode())
        root[2].insert(i + 1, c)
    return bxml.write(root, tail)
