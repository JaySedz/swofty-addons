"""Builds the stuck wheel nut pit stop sequences in erp/.

The game times a pit stop by its crew sequence: the car is released when the sequence reaches its JC / Stop
events, so a longer sequence is a longer stop (the stock 5 s / 10 s penalty stops work the same way). The
rear right "wheel on" mistake (pitstopstartjobdostandardstoprron.sequence, 180 frames at 30 fps) has the rear
right crew fighting the wheel between frames 79 and 122 (tyre_on_error sound 79..121) while the rest of the
crew wait with the car on its jacks.

This script plays the stop as stock up to frame 122, then inserts a pause there:
- the rear right gun man, tyre-on man, gun, new tyre and wheel (LOOP) repeat their struggle, frames 79-122;
- every other track (the other crew, the jacks, the car) has no key during the pause, so it holds its
  frame 122 pose. The stock sequences leave such gaps 1,061 times (the jack props in the 10 s penalty stops
  hold from frame 170 to 498);
then everything carries on from frame 122, so the end of the stop is the stock one.

Only patterns the stock sequences already use are written: AnimKeys with a duration and an animStartTime_f
(4,500+ in pitstop_26.erp), and gaps between keys. No loop keys are added.
Tested 2026-11-22: 0.1.4 (whole crew repeating the window) showed repeated keys play and the car is held;
0.1.5 (other tracks repeating a 4-frame slice) froze them but left them shaking on the spot.

How long the stop really is depends on the crew speed the game plays the sequence at (base / the stop's
target time from the training band), so the extra time is nominal: a 30 s file gave ~28 s on a fast stop.

Run with the game folder: python tools/make_stuck_nut.py "E:/Steam/steamapps/common/F1 25"
Needs the zstandard package and the launcher's tools/erp.py and tools/vtf.py (SWOFTY_TOOLS, or the default path).
"""
import os
import sys

TOOLS = os.environ.get("SWOFTY_TOOLS", r"C:\Users\jay-s\Desktop\SwoftyLauncher-main\tools")
sys.path.insert(0, TOOLS)
import vtf  # noqa: E402
from erp import parse  # noqa: E402
import zstandard  # noqa: E402

ERP = "2025_asset_groups/animation_package/scenes/pitstops/f1/pitstop_26.erp"
RESOURCE = "eaid://animation_package/sequences/pitstops/f1/pitstopstartjobdostandardstoprron.sequence"
WINDOW = (79, 122)          # the rear right crew's tyre-on error, repeated by LOOP tracks
PAUSE_AT = 122              # where the extra time goes in
LOOP = {"RR_TG", "RR_TF", "rearright_wheel_gun_prop", "TyreNew_RR_Car1", "TyreNew_RR_Car2", "rr"}
LENGTHS = (10, 15, 20, 30, 60)  # extra seconds, one file each; over the pit lane limit only with box_limit raised
FPS = 30
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_fragment(game, erp, resource, index):
    path = os.path.join(game, erp)
    meta, res = parse(path)
    with open(path, "rb") as f:
        for name, rtype, unk, frags, h in res:
            if name != resource:
                continue
            fn, off, size, flags, comp, packed = frags[index]
            f.seek(meta["data_off"] + off)
            raw = f.read(packed)
            return zstandard.ZstdDecompressor().decompressobj().decompress(raw) if comp in (0x11, 0x91) else raw
    raise SystemExit("%s not in %s" % (resource, erp))


def ival(k, name, default=None):
    v = k.attr(name)
    return int(v) if v is not None else default


def anim_key(src, time, dur, start):
    k = src.clone()
    k.set_attr(b"time_f", str(time).encode())
    k.set_attr(b"duration_f", str(dur).encode())
    if k.attr(b"animStartTime_f") is not None:
        k.set_attr(b"animStartTime_f", str(start).encode())
    else:
        attrs = []
        for name, value in k.attrs:  # the stock files write it right after duration_f
            attrs.append((name, value))
            if name == b"duration_f":
                attrs.append((b"animStartTime_f", str(start).encode()))
        k.attrs = attrs
    return k


def active_key(keys, frame):
    """The AnimKey playing at frame: the latest one started at or before it that hasn't ended."""
    best = None
    for k in keys:
        t, d = ival(k, b"time_f"), ival(k, b"duration_f")
        if d > 0 and t <= frame < t + d and (best is None or t >= ival(best, b"time_f")):
            best = k
    return best


def stretch(root, extra):
    a, b = WINDOW
    d = b - a
    reps = extra // d
    p = PAUSE_AT
    clip = None
    for node, _ in vtf.walk(root):
        if node.kind:
            continue
        if node.name == b"NeSequenceClip":
            clip = node.attr(b"name").decode()
            continue
        if node.name != b"NeSequencePacket":
            continue
        anims = [k for k in node.children if k.kind == 0 and k.name == b"AnimKey"]
        others = [k for k in node.children if k.kind == 0 and k.name != b"AnimKey"]
        loop = clip in LOOP
        new_anims, new_others = [], []
        for k in anims:
            t, dur = ival(k, b"time_f"), ival(k, b"duration_f")
            start = ival(k, b"animStartTime_f", 0)
            if t >= p:
                new_anims.append(anim_key(k, t + extra, dur, start) if dur else shifted(k, extra))
            elif dur == 0 or t + dur <= p:
                new_anims.append(k)
            else:  # plays across the pause: split it
                before = trim(k, p - t)
                if not loop:
                    # a held track must not loop into the gap: the car's clip has loop=true, and in 0.1.6 it
                    # kept looping during the pause (the car bobbed on its jacks)
                    before.attrs = [(n, v) for n, v in before.attrs if n != b"loop"]
                new_anims.append(before)
                new_anims.append(anim_key(k, p + extra, t + dur - p, start + (p - t)))
        if anims:
            if loop:
                k = active_key(anims, a)
                if k is not None:
                    t, s = ival(k, b"time_f"), ival(k, b"animStartTime_f", 0)
                    for r in range(reps):
                        new_anims.append(anim_key(k, p + r * d, d, s + (a - t)))
            # every other track gets no key during the pause: it holds its frame 122 pose, as the stock
            # penalty sequences hold props in a gap between keys
        for k in others:
            t = ival(k, b"time_f")
            if t is None:
                new_others.append(k)
                continue
            new_others.append(shifted(k, extra) if t >= p else k)
            if loop and k.name == b"Key" and k.attr(b"play") is not None and a <= t < b:
                for r in range(reps):  # the struggle's sounds play again in every repeat
                    new_others.append(at_time(k, p + r * d + (t - a)))
        new_anims.sort(key=lambda k: ival(k, b"time_f"))
        rest = [c for c in node.children if c.kind != 0]
        node.children = new_anims + sorted(new_others, key=lambda k: ival(k, b"time_f", -1)) + rest

    root.set_attr(b"duration_f", str(int(root.attr(b"duration_f")) + extra).encode())
    parts = []
    for part in root.attr(b"userData").decode().split(","):
        if part.startswith("stTime="):
            part = "stTime=%g" % round(float(part[7:]) + extra / FPS, 2)
        parts.append(part)
    root.set_attr(b"userData", ",".join(parts).encode())


def shifted(k, by):
    return at_time(k, ival(k, b"time_f") + by)


def at_time(k, t):
    c = k.clone()
    c.set_attr(b"time_f", str(t).encode())
    return c


def trim(k, dur):
    c = k.clone()
    c.set_attr(b"duration_f", str(dur).encode())
    return c


def main():
    game = sys.argv[1] if len(sys.argv) > 1 else r"E:\Steam\steamapps\common\F1 25"
    stock = read_fragment(game, ERP, RESOURCE, 0)
    out_dir = os.path.join(HERE, "erp")
    os.makedirs(out_dir, exist_ok=True)
    d = WINDOW[1] - WINDOW[0]
    for seconds in LENGTHS:
        extra = round(seconds * FPS / d) * d  # whole repeats of the struggle
        root = vtf.parse(stock)
        stretch(root, extra)
        data = vtf.write(root)
        base = os.path.join(out_dir, "rron_stuck_%ds" % seconds)
        open(base + ".sequence", "wb").write(data)
        open(base + ".tsmd", "wb").write(vtf.tsmd(vtf.parse(data)))
        print("%s: +%.1f s, %d frames, %d bytes" % (os.path.basename(base), extra / FPS,
                                                    int(root.attr(b"duration_f")), len(data)))


if __name__ == "__main__":
    main()
