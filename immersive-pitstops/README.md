# Immersive Pitstops

Career pit stops in F1 25 come out at almost exactly the same time every lap: a clean stop with the stock
crew is 2.4 s, every time. This addon spreads them out. Most stops land between about 2.2 and 2.8 s, a good
crew now and then turns one in 1.6 to 2.0 s, mistakes happen more often, and once in a while the rear right
wheel nut sticks and the car sits on its jacks while the crew fight it. Sometimes they get it on and the
car goes back out; sometimes the team gives up and the car's race is over.

It changes game database values and one pit stop animation sequence, and has a small native plugin
(`native/src/immersive_pitstops.cpp`, source included) that keeps the pit lane from releasing a car while
its crew are still working, and retires the car when the team gives up.

Uses work from Terminal Damage by nyannoying:
[https://github.com/nyannoying1337/f1-25-terminal-damage](https://github.com/nyannoying1337/f1-25-terminal-damage)
(the way a car is retired: the game's own retirement message and the byte pattern that finds it). Not
endorsed by Terminal Damage's author.

## What it changes

Pit crew training bands (`pit_crew_training.probabilities`), F1 set. The game picks a band by the team's
pit crew training level, for your team and for the AI teams:

| Training level | Stock stop | New stop | Stock mistake chance | New mistake chance |
|---|---|---|---|---|
| up to 20 % | 2.50-2.54 s | 2.00-2.95 s | 21-30 % | 18-25 % |
| up to 40 % | 2.45-2.50 s | 1.90-2.90 s | 11-20 % | 14-18 % |
| up to 60 % | 2.40-2.45 s | 1.80-2.85 s | 6-10 % | 10-14 % |
| up to 80 % | 2.30-2.40 s | 1.70-2.80 s | 1-5 % | 7-10 % |
| up to 100 % | 2.24-2.30 s | 1.60-2.75 s | 0 % | 4-7 % |
| equal performance | 2.24 s | 1.80-2.85 s | 0 % | 8-10 % |

Stuck wheel nut: the game's crew mistakes are pre-made animation sequences, and the car leaves the box when
its sequence ends; the time loss values in the database don't change that. The rear right "wheel on"
mistake (`pitstopstartjobdostandardstoprron.sequence` in `pitstops/f1/pitstop_24/25/26.erp`) is rebuilt
with a pause: the rear right gun man and tyre-on man repeat their struggle while the rest of the crew, the
jacks and the car hold still, until the stop is 10 to 60 s longer. The game's 5 s and 10 s penalty
stops hold the car the same way. `tools/make_stuck_nut.py` builds the sequences from the game's own file.

The pit lane's failsafe: while a car is in its box the pit lane counts a timer, and when it passes the
stop's limit (about 20 s) it marks the crew as done and lets the car go, even mid-animation: no "go", no
drop from the jacks. The plugin wraps the launcher binding `Game_PitLane_UpdateMovement` and keeps that
timer just under the limit while the car is in its box, so the crew finishing ends the stop as normal. A
safety cap (**Longest hold**) lets the game's limit back in after 120 s. The timer's place in the game's
memory comes from the launcher's research notes, not from a launcher API, so a game update can break it.
The plugin checks the record against the garage the launcher reports before it writes anything, and turns
itself off for the session (logged) if they don't match. No network access, no files written, no
processes started.

Retirement: a stop still in the box after 8 s is a stuck wheel nut (a normal stop, even with the game's own
mistake, is well under it), and it rolls the **Retirement chance** once. If it lands, the team gives up
after **Give up after** seconds in the box (or as soon as the crew finish, if that comes first): the plugin
sends the game's own "retired" status for the car, the one the pause menu's Retire sends, and keeps the car
in its box. An AI car is put in its garage 5 s later, as the game does with retired AI cars. Your own car
stays in its box and the game's retirement takes over (the race ends for you). The crew pushing your car
into the garage, as Terminal Damage does it, isn't in this version.

The F2 bands are unchanged. Mistakes stay switched on only in My Team and Driver Career, as in the stock game.

## Settings

- **Stuck wheel nut**: On (default) or Off. It's one of the eight wheel mistakes, so about one mistake in
  eight.
- **Stuck wheel nut time**: about 10, 15, 20 (default), 30 or 60 s. The game plays the whole stop at the
  crew's rolled speed, so the real time varies a little around the setting.
- **Hold the car until the crew finish**: On (default). Off gives the game's own ~20 s limit back.
- **Longest hold (s)**: 120 by default; the safety cap above.
- **Retirement chance (%)**: 25 by default; how many stuck wheel nuts end the car's race. 0 turns it off.
- **Give up after (s in the box)**: 15 by default.
- **Log pit stops (testing)**: writes each stop's box time and the pit lane's limit to the launcher log.
- **Widen team pit times too**: also widens each team's range in `pit_lane.pit_times`. That table is read
  outside the careers too, so Grand Prix changes as well. Off by default.
- **Test mode**: for testing only. Forces every stop to 1.6 s, to 3.0 s, or to a stuck wheel nut.
  Off by default.

Changing a setting needs a rebuild, which the launcher does on the next launch.

## Limits

- The stop schedule is rolled when a session loads, so a change applies from the next session.
- The stuck wheel nut is always the rear right wheel.
- Steam only: the plugin's offsets were found on the Steam build.
- Without the plugin's hold (or if it turns itself off), stuck nuts over about 15 s are cut short.

## Building

`python tools/make_stuck_nut.py "<F1 25 folder>"` writes the sequences in `erp/` (needs the `zstandard`
package and the launcher's `tools/erp.py` and `tools/vtf.py`). `python tools/make_ops.py` writes
`db/pitstops.json` and `erp/stuck_nut.json`. `build.bat` builds `native/immersive_pitstops.dll` (Visual
Studio 2022 with "Desktop development with C++"; the SDK headers in `native/include` are the launcher's,
with the same one-line MSVC fix in `mixin.hpp` as Driver Display's copy). Then run `package.bat`.
