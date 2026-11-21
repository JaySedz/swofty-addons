# Materials of a trophy pack: textures from an OBJ's MTL file or from trophies.json, made into game textures and a
# material copied from a podium trophy material (see the README's "Your own textures").
import os
import re

import texture
import trophy

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_MATERIAL = 'eaid://environment_package/core_assets/objects/materials/trophy/trophy_closed_a_01_01.material'
MATERIAL_DIR = 'eaid://environment_package/core_assets/objects/materials/trophy/'
MAPS = (('colour', '_d.tif'), ('normal', '_n.tif'), ('surface', '_s.tif'))


def mtl_textures(obj_path):
    """{material name: {'colour', 'normal', 'surface': png}} from the OBJ's MTL files: map_Kd; norm, map_Bump or
    bump; and an export's '# surface map:' line. A material whose maps are all an export's own (*.game.png) is left
    out, so it keeps its game material; one with a map of its own uses all of its maps."""
    out, cur = {}, None
    folder = os.path.dirname(obj_path)
    keys = {'map_Kd': 'colour', 'norm': 'normal', 'map_Bump': 'normal', 'bump': 'normal', 'map_bump': 'normal'}
    for line in open(obj_path, encoding='utf-8', errors='replace'):
        if not line.startswith('mtllib '):
            continue
        mtl = os.path.join(folder, line[7:].strip())
        if not os.path.isfile(mtl):
            continue
        for ml in open(mtl, encoding='utf-8', errors='replace'):
            if ml.startswith('# surface map:'):
                t = ['surface', ml.split(':', 1)[1].strip()]
            else:
                t = ml.split()
            if not t:
                continue
            if t[0] == 'newmtl':
                cur = out.setdefault(' '.join(t[1:]), {})
            elif cur is not None and (t[0] in keys or t[0] == 'surface') and len(t) > 1:
                path = os.path.normpath(os.path.join(os.path.dirname(mtl), t[-1]))
                if os.path.isfile(path):
                    cur[keys.get(t[0], 'surface')] = path
    return {k: v for k, v in out.items()
            if 'colour' in v and not all(p.endswith('.game.png') for p in v.values())}


def custom_material(podium, spec, tag, raw_dir, folder):
    """A new material from spec (colour/normal/surface maps, see trophies.json) on a copy of a podium trophy material.
    Returns (material path, [ops])."""
    base_name = spec.get('base', BASE_MATERIAL)
    base = podium.frags(base_name)[0]
    views = trophy.material_deps(podium, base_name)
    ops, swaps = [], {}
    os.makedirs(raw_dir, exist_ok=True)
    for kind, end in MAPS:
        old = next((v for v in views if v.endswith(end)), None)
        if old is None:
            raise SystemExit(f'{base_name} has no {end} texture')
        src = spec.get(kind)
        if kind == 'colour' and not src:
            raise SystemExit(f'{tag}: a custom material needs "colour" (a PNG)')
        if kind == 'surface' and src is None:
            src = 'metal'
        if isinstance(src, str) and src not in texture.SURFACE_PRESETS:
            src = os.path.join(folder, src)
            if not os.path.isfile(src):
                raise SystemExit(f'{tag}: {src} is missing')
        fmt, w, h, mips, data = texture.encode(kind, src, spec.get('normal_style', 'opengl') == 'opengl')
        new = texture.texture_name(old, tag)
        swaps[old] = new
        stem = os.path.join(raw_dir, new.rsplit('/', 1)[1])
        open(stem + '.head', 'wb').write(texture.surface_head(fmt, w, h, mips))
        open(stem + '.pixels', 'wb').write(data)
        open(stem + '.view', 'wb').write(texture.view(fmt, mips, new + '.image'))
        ops.append({'op': 'add-resource', 'erp': trophy.PODIUM, 'resource': new + '.image', 'type': 'GfxSurfaceRes',
                    'sources': [rel(stem + '.head'), rel(stem + '.pixels')]})
        ops.append({'op': 'add-resource', 'erp': trophy.PODIUM, 'resource': new, 'type': 'GfxSRVResource',
                    'sources': [rel(stem + '.view')]})
        print(f'  {tag} {kind}: {w} x {h}, {mips} mips -> {new.rsplit("/", 1)[1]}')
    slug = re.sub(r'[^a-z0-9]+', '_', tag.lower()).strip('_')
    mat = MATERIAL_DIR + 'pt_' + slug + '.material'
    p = os.path.join(raw_dir, 'pt_' + slug + '.material')
    open(p, 'wb').write(texture.retexture(base, swaps))
    ops.append({'op': 'add-resource', 'erp': trophy.PODIUM, 'resource': mat, 'type': 'GfxMaterialRes',
                'sources': [rel(p)]})
    return mat, ops


def rel(p):
    return os.path.relpath(p, ROOT).replace('\\', '/')
