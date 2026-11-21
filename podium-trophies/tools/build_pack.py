# Builds a trophy pack: the ready game resources of one or more trophies, for the Podium Trophies engine to pair
# with tracks. A pack is a folder with pack.json and one folder a trophy; the engine finds it in its own addon
# (pack/) and in any enabled addon (podium-trophies/), so a track addon can ship the trophy for its own track.
#
#   python tools/build_pack.py <trophies.json> <pack folder>
#
# trophies.json (paths are relative to it):
#   { "trophies": [ { "id": "trophy_hockenheim",          a trophy type: "trophy_" and a-z, 0-9 or _, unique
#                     "name": "German GP",                 shown in the launcher
#                     "donor": "trophy_closed_a",          the stock trophy whose model layout it takes (default);
#                                                          closed_a and modern_a hold 65,535 vertices a model
#                     "grip": "trophy_modern_b",           the stock trophy whose winner's hand pose it gets
#                                                          (default: the donor; "none": no hand pose, as Monaco)
#                     "source": "models/hockenheim.obj",   an OBJ, or "<game archive>.erp:<model name part>"
#                     "height": "grip",                    metres, or the grip trophy's height (default)
#                     "materials": { ... },                as in the README's "Your own textures"
#                     "tracks": ["hockenheim"],            track codes it goes to unless the player picks another
#                     "preview": "models/hockenheim.png" } ] }   optional: a picture for the launcher's menu
#
# Run it with the game closed: it reads the game's stock podium archive.
import json
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import materials  # noqa: E402
import newtype  # noqa: E402
import trophy  # noqa: E402

PACK_SCHEMA = 1


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__ if __doc__ else 'build_pack.py <trophies.json> <pack folder>')
    cfg_path, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    base = os.path.dirname(cfg_path)
    cfg = json.load(open(cfg_path, encoding='utf-8'))
    podium = trophy.Archive(trophy.stock_check(trophy.PODIUM))
    stock = newtype.stock_types(podium)
    if os.path.isdir(out):
        if os.path.exists(os.path.join(out, 'pack.json')) or not os.listdir(out):
            shutil.rmtree(out)
        else:
            raise SystemExit(f'{out} is not empty and holds no pack.json; pick an empty folder')
    os.makedirs(out)

    def rel(p):
        return os.path.relpath(p, out).replace('\\', '/')

    pack, ids = [], set()
    for t in cfg['trophies']:
        tid, donor_name = t['id'], t.get('donor', 'trophy_closed_a')
        grip = t.get('grip', donor_name)
        newtype.check_name(tid, stock)
        if donor_name == 'trophy_monaco' or donor_name not in stock:
            raise SystemExit(f'{tid}: donor {donor_name} is not a podium trophy this tool copies')
        if grip != 'none' and (grip not in stock or grip.startswith('trophy_f2')):
            raise SystemExit(f'{tid}: grip {grip} is not a stock podium trophy (or "none")')
        if tid in ids:
            raise SystemExit(f'{tid} is in the list twice')
        ids.add(tid)
        donor = trophy.Model(podium, trophy.podium_idf(donor_name))
        d = os.path.join(out, tid)
        os.makedirs(d)
        ops, src = [], t['source']
        if '.erp:' in src:
            path, part = src.split('.erp:', 1)
            src_arc = trophy.Archive(trophy.stock_check(path + '.erp'))
            g = trophy.model_geometry(trophy.Model(src_arc, src_arc.find_model(part)))
            copy_from = [(path + '.erp', src_arc)]
        else:
            obj = os.path.join(base, src)
            g = trophy.obj_geometry(trophy.read_obj(obj))
            specs = materials.mtl_textures(obj)
            specs.update(t.get('materials', {}))
            used = {sl for _, parts in g.objects for sl, _ in parts}
            g.materials = {}
            for slot, spec in specs.items():
                if slot not in used:
                    continue
                if isinstance(spec, str):
                    g.materials[slot] = spec  # a game material
                    continue
                mat, mops = materials.custom_material(podium, spec, f'{tid}_{slot}', os.path.join(d, 'tex'), base)
                for o in mops:  # custom_material names its files from the repo root
                    o['sources'] = [rel(os.path.join(materials.ROOT, s)) for s in o['sources']]
                    o.pop('erp', None)
                g.materials[slot] = mat
                ops.extend(mops)
            copy_from = [(p, trophy.Archive(trophy.stock_check(p))) for p in t.get('copy_from', [])]
        h = t.get('height', 'grip')
        tall = g.pos[:, 1].max() - min(0.0, g.pos[:, 1].min())
        if h in ('grip', 'donor'):  # the height of the trophy the winner's hand pose was made for
            ref = donor if grip in ('none', donor_name) else trophy.Model(podium, trophy.podium_idf(grip))
            h = float(ref.attr[(1, 0)][:, 1].max())
        scale = float(h) / tall if h else 1.0
        built, notes, mats = trophy.build_slot(donor, g, None, scale)
        model_ops = []
        for res, rtype, frs in newtype.renamed_model(podium, donor_name, built, tid):
            tag = re.sub(r'[^a-z0-9]+', '_', res.rsplit('/', 1)[1].lower()).strip('_')
            paths = []
            for i, b in enumerate(frs):
                paths.append(os.path.join(d, f'{tag}.{i}.bin'))
                open(paths[-1], 'wb').write(b)
            model_ops.append({'op': 'add-resource', 'resource': res, 'type': rtype, 'sources': [rel(p) for p in paths]})
        copies = []
        added = {o['resource'] for o in ops}
        for m in mats:
            if m in podium.res or m in added:
                continue
            hit = next(((p, a) for p, a in copy_from if m in a.res), None)
            if not hit:
                raise SystemExit(f'{tid}: {m} is not in the podium archive or in {[p for p, _ in copy_from]}')
            p, a = hit
            for r in [m] + trophy.material_deps(a, m):
                if r not in podium.res and not any(c['resource'] == r for c in copies):
                    copies.append({'op': 'copy-resource', 'from_erp': p, 'resource': r})
        entry = {'id': tid, 'name': t.get('name', tid), 'donor': donor_name, 'grip': grip, 'tracks': t.get('tracks', []),
                 'ops': copies + ops + model_ops}
        if t.get('preview'):
            pv = os.path.join(d, 'preview' + os.path.splitext(t['preview'])[1].lower())
            shutil.copyfile(os.path.join(base, t['preview']), pv)
            entry['preview'] = rel(pv)
        pack.append(entry)
        print(f'{tid}: from {donor_name} <- {src}, scaled {scale:.3f} to {h:.3f} m; {len(entry["ops"])} resources')
        for n in notes:
            print('  ' + n)
    json.dump({'schema_version': PACK_SCHEMA, 'trophies': pack}, open(os.path.join(out, 'pack.json'), 'w',
              newline='\n'), indent=2)
    print(f'{out}: {len(pack)} trophies')


if __name__ == '__main__':
    main()
