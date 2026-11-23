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

# F1 training bands: stop times only (min s, max s). 1.0.13: the mistake chances are the game's own again: changing
# them never gave the addon control (2026-11-23: ~140 stops at 10-14 % and 18 stops at 50 %, all without a mistake;
# at 100 % every stop had one). Stuck wheel nuts are rolled by the plugin instead (native/src).
bands = [(2.00, 2.95), (1.90, 2.90), (1.80, 2.85), (1.70, 2.80), (1.60, 2.75)]
for i, (lo, hi) in enumerate(bands):
    r = P % ("f1", i)
    s(r, "min_routine_pit_stop_time", lo); s(r, "max_routine_pit_stop_time", hi)
r = P % ("f1_equal_performance", 0)
s(r, "min_routine_pit_stop_time", 1.80); s(r, "max_routine_pit_stop_time", 2.85)

# (Test mode was removed in 1.0.14: the sliders go to 100 % for testing)

# Crossing the pit exit white line: a time penalty from the first offence in races (stock: a warning first).
# rules.crossing_pit_white_line.<long_race|short_race>.penalty_level_0.level, read by Sess_Rules_ReadPitWhiteLine.
# Tested 2026-11-23: level 3 gives 2 s; level 4 (the stock second offence) gives 3 s with the engineer's radio call.
# So the first offence is level 4 too (1.0.18); practice and qualifying stay warnings.
for race in ("long_race", "short_race"):
    s("rules.crossing_pit_white_line.%s.penalty_level_0" % race, "level", "4")

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
# The rear wheels' "wheel on" files, always: 60 s stuck nuts. The plugin plays one in place of a normal stop when its
# own stuck nut roll lands, and cuts it to a rolled length (1.0.4); when the game picks its own "rear wheel on"
# mistake, the plugin cuts the pause out entirely, so that plays at its stock length. The front files stay stock: a
# front file holding the rear crew's animation switched the game's crew mistakes off (1.0.6-1.0.8).
for year in (24, 25, 26):
    erp = "2025_asset_groups/animation_package/scenes/pitstops/f1/pitstop_%d.erp" % year
    for frag, ext in ((0, "sequence"), (1, "tsmd")):
        for fwa in ("", "fwa"):  # the plain stop and its front wing adjust version
            for wheel in ("rlon", "rron"):
                erp_ops.append({"op": "replace-resource", "erp": erp, "resource": RES % (fwa + wheel),
                                "fragment": frag, "source": "erp/%s%s_stuck_60s.%s" % (fwa, wheel, ext)})
json.dump({"schema_version": 1, "ops": erp_ops}, open(os.path.join(HERE, "erp", "stuck_nut.json"), "w", newline="\n"),
          indent=1)
print(len(ops), "db ops,", len(erp_ops), "erp ops")
