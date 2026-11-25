# Writes erp/mount.json: the "Helmet mount" options as patch-pkg set-value edits on each team's cockpit camera mount.
#
# Usage: python tools/gen_mount.py "<F1 25 folder>"
#
# Each base team archive (<package>/teams/<team>/wep/<team>.erp) holds <team>-camscargame.layer, whose entry
# camscargame$CameraAttachmentPointCockpit places the cockpit camera: overrides.AttachmentPoint.Offset (vec3) and
# overrides.WorldObject.transform (mat4, translation in the last row). For every height/side choice that isn't
# stock, the op sets both to the team's own stock value plus the choice's offset, so every car keeps its own seat
# position and only moves by the chosen amount. Only archives whose layer has that entry with both values are listed,
# and the stock values are read from this game folder: run it on an unmodified install.
# Livery variant archives (ferrari_monza_25.erp ...) and added teams are left out.
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'driver-display', 'tools'))
import pkgfmt  # noqa: E402

pkgfmt.FMT.setdefault('mat4', '<16f')
pkgfmt.FMT.setdefault('woid', '16s')

try:
    import zstandard

    def unzstd(b, n):
        return zstandard.ZstdDecompressor().decompress(b, max_output_size=n)
except ImportError:  # the repo's ctypes libzstd
    from erp import unzstd  # noqa: E402

PACKAGES = ['f1_2026_vehicle_package', 'f1_2025_vehicle_package', 'f1_2024_story_vehicle_package',
            'f1_2025_story_vehicle_package', 'f2_2026_vehicle_package', 'f2_2025_vehicle_package',
            'f2_2024_vehicle_package']
ATTACH = 'camscargame$CameraAttachmentPointCockpit'

# choice value -> metres (y up). Side: x, + is to the right of the driver (checked in game; see TESTING.md).
HEIGHTS = {'stock': 0.0, 'low': -0.015, 'lower': -0.03, 'lowest': -0.045}
SIDES = {'centre': 0.0, 'left': -0.02, 'right': 0.02}


def resources(f):
    ver, = struct.unpack_from('<I', f, 4)
    data_off, = struct.unpack_from('<Q', f, 0x20)
    count, = struct.unpack_from('<I', f, 0x30)
    p = 0x38
    for _ in range(count):
        p += 4
        nl, = struct.unpack_from('<H', f, p)
        p += 2
        name = f[p:p + nl].rstrip(b'\0').decode('latin1')
        p += nl + 16 + (6 if ver >= 4 else 4)
        n = f[p]
        p += 1
        frags = []
        for _k in range(n):
            off, size = struct.unpack_from('<QQ', f, p + 4)
            comp = f[p + 24]
            packed, = struct.unpack_from('<Q', f, p + 25)
            p += 33
            frags.append((data_off + off, size, comp, packed))
        p += 16
        yield name, frags


def stock_mount(path):
    f = open(path, 'rb').read()
    for name, frags in resources(f):
        if not name.endswith('-camscargame.layer'):
            continue
        off, size, comp, packed = frags[0]
        raw = f[off:off + packed]
        if comp == 0x11:
            raw = unzstd(raw, size)
        root = pkgfmt.read(raw)
        for inst in root.get('instances') or []:
            n = inst.get('name')
            if n is None or n.v != ATTACH:
                continue
            ov = inst.get('overrides')
            offset = ov.get('AttachmentPoint').get('Offset') if ov and ov.get('AttachmentPoint') else None
            tf = ov.get('WorldObject').get('transform') if ov and ov.get('WorldObject') else None
            if offset is None or tf is None:
                return name, None
            return name, (list(offset.v), list(tf.v))
        return name, None
    return None, None


def r(x):
    return round(float(x), 6)


def main():
    game = sys.argv[1]
    root = os.path.join(game, '2025_asset_groups')
    ops, found = [], []
    for pkg in PACKAGES:
        teams = os.path.join(root, pkg, 'teams')
        if not os.path.isdir(teams):
            continue
        for team in sorted(os.listdir(teams)):
            erp = os.path.join(teams, team, 'wep', team + '.erp')
            if not os.path.isfile(erp):
                continue
            res, mount = stock_mount(erp)
            if not mount:
                print(f'skip {pkg}/{team}: {"no cockpit mount entry" if res else "no camera layer"}')
                continue
            (ox, oy, oz, ow), tf = mount
            rel = f'2025_asset_groups/{pkg}/teams/{team}/wep/{team}.erp'
            found.append(rel)
            for h, dy in HEIGHTS.items():
                for s, dx in SIDES.items():
                    if dy == 0 and dx == 0:
                        continue
                    t = [r(v) for v in tf]
                    t[12], t[13] = r(tf[12] + dx), r(tf[13] + dy)
                    ops.append({
                        'op': 'patch-pkg',
                        'when': {'mount_height': h, 'mount_side': s},
                        'erp': rel,
                        'resource': res,
                        'edits': [
                            {'op': 'set-value', 'path': f'instances[name={ATTACH}].overrides.AttachmentPoint.Offset',
                             'value': [r(ox + dx), r(oy + dy), r(oz), r(ow)]},
                            {'op': 'set-value', 'path': f'instances[name={ATTACH}].overrides.WorldObject.transform',
                             'value': t},
                        ],
                    })
            print(f'ok   {pkg}/{team}: offset {r(ox)} {r(oy)} {r(oz)}')
    out = os.path.join(HERE, '..', 'erp', 'mount.json')
    with open(out, 'w', newline='\n') as fh:
        json.dump({'schema_version': 1, 'ops': ops}, fh, indent=1)
        fh.write('\n')
    print(f'{len(found)} archive(s), {len(ops)} op(s) -> erp/mount.json')


if __name__ == '__main__':
    main()
