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
RESOURCE = "eaid://animation_package/sequences/pitstops/f1/pitstopstartjobdostandardstop%s.sequence"
# Each "wheel on" mistake gets its own length (0.2.2's Random; the only choice since 1.0): the game picks which
# wheel's mistake happens, so the stuck nut's length comes out random.
# Each also in its front wing adjust ("fwa") version, which the game plays when the stop changes the wing angle
# (the AI do that a lot: 0.2.2's test had three AI cars on the unedited fwa version).
_B = [("rlon", 60), ("rron", 60)]  # 60 s each; the plugin cuts each stuck nut to a rolled length (1.0.4); rear only (1.0.5)
BUILDS = _B + [("fwa" + w, s) for w, s in _B]
GUN_PROP = {"FL": "frontleft", "FR": "frontright", "RL": "rearleft", "RR": "rearright"}
FPS = 30


def find_window(root):
    """The struggle: the tyre-on man's tyre_on_error sound, play to stop (rear right: 79..121), and the corner.
    Returns the window, the clips that loop it, and the gun man's own loop start: his tyre_gun_error key (1.0.5: the
    gun man and his gun repeat his failed attempt with the gun, from that key, for the same length; on the rear
    right it is 3 frames before the tyre man's window, so before 1.0.5 his attempt never repeated)."""
    clip, play, stop, gun = None, None, None, None
    for n, _ in vtf.walk(root):
        if n.kind:
            continue
        if n.name == b"NeSequenceClip":
            clip = n.attr(b"name").decode()
        elif n.name == b"Key" and n.attr(b"play") == b"play_anim_tyre_on_error" and play is None:
            play, corner = int(n.attr(b"time_f")), clip[:2]
        elif n.name == b"Key" and n.attr(b"play") == b"stop_anim_tyre_on_error" and stop is None:
            stop = int(n.attr(b"time_f"))
        elif n.name == b"Key" and n.attr(b"play") == b"play_anim_tyre_gun_error" and gun is None:
            gun = int(n.attr(b"time_f"))
    if play is None or stop is None or corner not in GUN_PROP:
        raise SystemExit("no tyre_on_error window in this sequence")
    loop = {corner + "_TG", corner + "_TF", GUN_PROP[corner] + "_wheel_gun_prop", "TyreNew_%s_Car1" % corner,
            "TyreNew_%s_Car2" % corner, corner.lower()}
    gun_clips = {corner + "_TG", GUN_PROP[corner] + "_wheel_gun_prop"}
    return (play, stop + 1), loop, gun_clips, gun
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


def stretch(root, extra, window, LOOP, GUN=(), gun_start=None):
    a0, b = window
    d = b - a0
    reps = extra // d
    p = b  # the pause goes in where the struggle ends
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
        # the clip's own loop: the gun man and his gun from his gun error key, everyone else the tyre man's window;
        # both are d frames long, so the plugin's cut (whole repeats of d) keeps every clip in step
        a = gun_start if (clip in GUN and gun_start is not None) else a0
        new_anims, new_others = [], []
        for k in anims:
            t, dur = ival(k, b"time_f"), ival(k, b"duration_f")
            start = ival(k, b"animStartTime_f", 0)
            if loop and a < t < a + d and dur:
                raise SystemExit("%s: an AnimKey starts inside the struggle at %d" % (clip, t))
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
            if loop and k.name == b"Key" and k.attr(b"play") is not None and a <= t < a + d:
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
    out_dir = os.path.join(HERE, "erp")
    os.makedirs(out_dir, exist_ok=True)
    for wheel, seconds in BUILDS:
        stock = read_fragment(game, ERP, RESOURCE % wheel, 0)
        window, loop, gun_clips, gun = find_window(vtf.parse(stock))
        d = window[1] - window[0]
        extra = max(1, round(seconds * FPS / d)) * d  # whole repeats of the struggle
        root = vtf.parse(stock)
        stretch(root, extra, window, loop, gun_clips, gun)
        data = vtf.write(root)
        base = os.path.join(out_dir, "%s_stuck_%ds" % (wheel, seconds))
        print("%s: struggle frames %d-%d, gun man from %s, looping %s" % (wheel, window[0], window[1], gun,
                                                                      ", ".join(sorted(loop))))
        open(base + ".sequence", "wb").write(data)
        open(base + ".tsmd", "wb").write(vtf.tsmd(vtf.parse(data)))
        print("%s: +%.1f s, %d frames, %d bytes" % (os.path.basename(base), extra / FPS,
                                                    int(root.attr(b"duration_f")), len(data)))


if __name__ == "__main__":
    main()
