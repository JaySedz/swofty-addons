"""Writes db/pitstops.json and erp/stuck_nut.json. Run from anywhere: python tools/make_ops.py

What the game does with these values (launcher RE notes, re/db/functions.tsv):
- The training band (pit_crew_training.probabilities) sets a stop's target time and its mistake chance.
- A mistake's min/max_time_loss is NOT what the stop lasts: the crew plays the mistake's own sequence
  (pitstops/f1/pitstop_NN.erp, "...stop" + animation_name) and the car leaves at its JC / Stop events.
  Tested 2026-11-22: wheel and wing rows at 30-60 s still gave the stock ~2 s mistake.
  So the stuck wheel nut is a longer rear right "wheel on" sequence (tools/make_stuck_nut.py).
"""
import json
import os

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ops = []


def s(sel, field, val, **kw):
    o = {"op": "set", "select": sel, "field": field, "value": num(val)}
    o.update(kw)
    ops.append(o)


def num(v):
    if isinstance(v, (int, float)):
        return ("%.2f" % v).rstrip("0").rstrip(".")
    return v


P = "pit_crew_training.probabilities.probability_set[_metadata_=%s].probability_%d"
M = "pit_crew_training.mistakes.mistake_info[_metadata_=%s]"
F1_ROWS = [P % ("f1", i) for i in range(5)] + [P % ("f1_equal_performance", 0)]

# F1 training bands: (min s, max s, min mistake %, max mistake %)
bands = [(2.00, 2.95, 18, 25), (1.90, 2.90, 14, 18), (1.80, 2.85, 10, 14), (1.70, 2.80, 7, 10), (1.60, 2.75, 4, 7)]
for i, (lo, hi, mlo, mhi) in enumerate(bands):
    r = P % ("f1", i)
    s(r, "min_routine_pit_stop_time", lo); s(r, "max_routine_pit_stop_time", hi)
    s(r, "min_chance_mistake_percent", mlo); s(r, "max_chance_mistake_percent", mhi)
r = P % ("f1_equal_performance", 0)
s(r, "min_routine_pit_stop_time", 1.80); s(r, "max_routine_pit_stop_time", 2.85)
s(r, "min_chance_mistake_percent", 8); s(r, "max_chance_mistake_percent", 10)

# optional: team pit times (read by the pit lane in every mode)
stock = [("mercedes", 2.55, 2.8, 1.95, 2.15), ("ferrari", 2.4, 2.8, 1.9, 2.15), ("redbull", 2.35, 2.55, 1.82, 1.95),
         ("williams", 2.25, 2.5, 1.82, 1.9), ("forceindia", 2.5, 2.95, 1.95, 2.25), ("renault", 2.45, 2.95, 2, 2.2),
         ("tororosso", 2.55, 2.95, 2.05, 2.2), ("haas", 2.65, 3, 2.1, 2.25), ("mclaren", 2.4, 2.75, 1.9, 2.1),
         ("sauber", 2.55, 3, 2.05, 2.25)]
T = "pit_lane.pit_times.team_pit_times[_metadata_=%s]"
for t, a, b, c, d in stock:
    w = {"team_pit_times": True}
    s(T % t, "min_pit_time", a - 0.45, when=w); s(T % t, "max_pit_time", b + 0.2, when=w)
    s(T % t, "min_pit_time_fully_upgraded", c - 0.25, when=w); s(T % t, "max_pit_time_fully_upgraded", d + 0.5, when=w)

# test mode: forces every stop (last, so it wins over the ops above)
for mode, t, mc in (("fast", 1.6, 0), ("slow", 3.0, 0), ("big_mistake", None, 100)):
    w = {"test_mode": mode}
    for r in F1_ROWS:
        if t is not None:
            s(r, "min_routine_pit_stop_time", t, when=w); s(r, "max_routine_pit_stop_time", t, when=w)
        s(r, "min_chance_mistake_percent", mc, when=w); s(r, "max_chance_mistake_percent", mc, when=w)
# every wheel mistake plays the rear right "wheel on" sequence, the one the stuck nut replaces, with the
# rear right row's engineer lines (0.1.5 test: a front left row played "issue on the left front")
RR_ON = {"animation_name": "RRON", "immersive_vo_name": "PitStopImmersive_BackRightWheel_Attachment",
         "cinematic_vo_name": "PitStopCinematic_BackRightWheel_Attachment", "event_node_name": "brw_attach"}
for w8 in ("front_left", "front_right", "back_left", "back_right"):
    for kind in ("removal", "attachment"):
        for field, value in RR_ON.items():
            s(M % ("slow_%s_wheel_%s" % (w8, kind)), field, value, when={"test_mode": "big_mistake"})
# with Random stuck nut times each wheel has its own stuck nut: there, every wheel mistake plays its own wheel's
# "wheel on" sequence and lines instead (later ops win)
CORNER = {"front_left": ("FL", "FrontLeft", "flw"), "front_right": ("FR", "FrontRight", "frw"),
          "back_left": ("RL", "BackLeft", "blw"), "back_right": ("RR", "BackRight", "brw")}
for w8, (code, vo, node) in CORNER.items():
    on = {"animation_name": code + "ON", "immersive_vo_name": "PitStopImmersive_%sWheel_Attachment" % vo,
          "cinematic_vo_name": "PitStopCinematic_%sWheel_Attachment" % vo, "event_node_name": node + "_attach"}
    for kind in ("removal", "attachment"):
        for field, value in on.items():
            s(M % ("slow_%s_wheel_%s" % (w8, kind)), field, value,
              when={"test_mode": "big_mistake", "stuck_length": "random"})

# The pit lane lets a car go when its in-box timer passes a per-stop limit (Game_PitLane_UpdateMovement case 4:
# rec+0xe4 += dt vs rec+0xe8, + 5 s with a penalty), whatever the crew are doing (0.1.5-0.1.7 tests: released
# with no "go", not dropped, through the crew). Team_Load_PitTimes (RVA 0x34cb170 on Steam 26566308) reads 12
# hashed pit_lane settings, found as 64-bit key constants in its code: 10, 5, 0.2, 0.6, 5, 5, 2.4, 7.5, 1,
# (2.6,1,5.5), 0.3, 0.2. The limit is likely the 10 or the 7.5; box_limit raises one or both to find out.
# Tested 2026-11-22 (0.1.8): raising either to 60 s changed nothing (the car left after 20.7 s both times), so the
# limit is set per stop elsewhere; native/src/immersive_pitstops.cpp holds the timer under it instead.
# Keys, for reference: the 10 = 0x1ad57c6a26755ece, the 7.5 = 0xb9411d7447502779.

json.dump({"schema_version": 1, "ops": ops}, open(os.path.join(HERE, "db", "pitstops.json"), "w", newline="\n"), indent=1)

# the stuck wheel nut sequence, in every F1 pit stop archive the game has (2024, 2025 and 2026 seasons)
erp_ops = []
RES = "eaid://animation_package/sequences/pitstops/f1/pitstopstartjobdostandardstop%s.sequence"
FIXED = ("10", "15", "20", "30", "60")                                   # rear right only
RANDOM = (("flon", 5), ("fron", 15), ("rlon", 30), ("rron", 60))       # each wheel its own length
for year in (24, 25, 26):
    erp = "2025_asset_groups/animation_package/scenes/pitstops/f1/pitstop_%d.erp" % year
    for on in ({"big_mistakes": "on"}, {"big_mistakes": "off", "test_mode": "big_mistake"}):
        for frag, ext in ((0, "sequence"), (1, "tsmd")):
            for fwa in ("", "fwa"):  # the plain stop and its front wing adjust version
                for length in FIXED:
                    erp_ops.append({"op": "replace-resource", "erp": erp, "resource": RES % (fwa + "rron"),
                                    "fragment": frag, "source": "erp/%srron_stuck_%ss.%s" % (fwa, length, ext),
                                    "when": dict(on, stuck_length=length)})
                for wheel, seconds in RANDOM:
                    erp_ops.append({"op": "replace-resource", "erp": erp, "resource": RES % (fwa + wheel),
                                    "fragment": frag, "source": "erp/%s%s_stuck_%ds.%s" % (fwa, wheel, seconds, ext),
                                    "when": dict(on, stuck_length="random")})
json.dump({"schema_version": 1, "ops": erp_ops}, open(os.path.join(HERE, "erp", "stuck_nut.json"), "w", newline="\n"),
          indent=1)
print(len(ops), "db ops,", len(erp_ops), "erp ops")
