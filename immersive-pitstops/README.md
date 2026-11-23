# Immersive Pitstops

Pit stops in F1 25 come out at almost exactly the same time every stop: the game gives each driver one stop time
for the whole session. This addon makes pit stops something that can go right or wrong:

- every stop's time varies, quicker or slower, now and then a little fumbled;
- a rear wheel nut can stick and hold the car on its jacks while the crew fight it, usually for a few seconds, now
  and then for up to a minute; sometimes the team gives up and the car's race is over;
- now and then a wheel isn't on properly: box next lap to have it fixed, or it comes off;
- call your stop with **Box This Lap** in the MFD radio menu, or the crew are still in the garage when you get there;
- crossing the pit exit line costs a 3 s penalty.

The game's own small crew mistakes happen as in the stock game. Races in My Team, Driver Career and Grand Prix, for
you and the AI teams (the call and the loose wheel's "box this lap" are yours).

It changes game database values and the crew's "wheel on" mistake animations, and has a small native plugin
(`native/src/immersive_pitstops.cpp`, source included). No network access, no files written, no processes
started.

Uses work from Terminal Damage by nyannoying:
[https://github.com/nyannoying1337/f1-25-terminal-damage](https://github.com/nyannoying1337/f1-25-terminal-damage)
(the way a car is retired: the game's own retirement message and the byte pattern that finds it, now only a
fallback for launchers before 1.10.26; and the way a wheel is torn off: the game's own corner damage set just past
its detach limit). Not endorsed by Terminal Damage's author.

## What it changes

**Pit crew training bands** (`pit_crew_training.probabilities`, F1 set): the stop times are widened. The game
picks a band by the team's pit crew training level (in Driver Career every team uses the "up to 60 %" band) and
rolls each driver's stop time from it once per session. The crew mistake chances are the game's own, so its small
crew mistakes happen as often as in the stock game.

| Training level | Stock stop | New stop |
|---|---|---|
| up to 20 % | 2.50-2.54 s | 2.00-2.95 s |
| up to 40 % | 2.45-2.50 s | 1.90-2.90 s |
| up to 60 % | 2.40-2.45 s | 1.80-2.85 s |
| up to 80 % | 2.30-2.40 s | 1.70-2.80 s |
| up to 100 % | 2.24-2.30 s | 1.60-2.75 s |
| equal performance | 2.24 s | 1.80-2.85 s |

**Every stop varies.** The plugin speeds each stop's crew up or down by up to 30 %, evenly, and about one stop
in six is a little fumbled on top. A 2.4 s stop lands anywhere from about 1.7 to 3.1 s, now and then up to
about 4 s. Yours and the AI's. It scales the crew's animation time through the launcher binding
`Sess_PitStopUpdateJob_Update`.

**Stuck wheel nut.** A separate incident from the game's own crew mistakes: at the start of each stop the plugin
rolls **Stuck wheel nut chance** (3 % by default), yours and the AI's. On a hit it hands the crew a rear wheel's
"wheel on" sequence (left or right rear, rolled) instead of the normal stop: the tyre-on man fights the wheel and the
gun man keeps retrying with the gun while the rest of the crew, the jacks and the car hold still. A popup tells you
("STUCK WHEEL NUT"); the engineer has no line for it, as it isn't one of the game's own mistakes.
- The sequences: the rear wheels' "wheel on" mistakes (`pitstopstartjobdostandardstop<rl/rr>on.sequence` and their
  front wing adjust versions, in `pitstops/f1/pitstop_24/25/26.erp`) rebuilt with a minute-long pause, built from the
  game's own files by `tools/make_stuck_nut.py`. The plugin swaps the sequence name it passes to
  `Game_PitStopInstance_PlaySequence` (a launcher binding).
- The length: about 5 s (45 %), 10 s (30 %), 20 s (14 %), 30 s (7 %) or a full 60 s (4 %). When the time is up the
  plugin moves the crew's animation clock ahead by whole repeats of the struggle, to the same pose near the end of the
  pause, and the stop carries on. (The clock is the crew sequence's time, PitStopInstance +0x3b0 then +0x5c, from the
  launcher's research notes: checked sane before it's written, or the stuck nut plays its full minute.)
- When the game picks its own "rear wheel on" mistake, the plugin cuts the pause out entirely, so it plays at its
  stock length.
- Rear wheels only, behind the driver, where the loop's small jumps are out of view. (A front wheel's file holding the
  rear crew's animation switches the game's crew mistakes off altogether: found in testing.)
- At 3 % a full-minute stuck nut comes up about once in 800 stops, roughly once a season across the grid. Only stuck
  nuts longer than about 13 s run past the pit lane's limit and roll the **Retirement chance**.

**Holding the car in its box.** While a car is in its box the pit lane counts a timer, and at about 20 s it lets
the car go, even with the crew mid-animation. The plugin wraps the launcher binding `Game_PitLane_UpdateMovement`
and keeps that timer just under the limit, so the crew finishing ends the stop as normal; after 120 s in the box
the game's limit is let back in, in case a crew never finishes. The timer's place in the game's memory comes from
the launcher's research notes, not a launcher API, so a game update can break it. The plugin checks the record
against the garage the launcher reports before it writes anything, and turns itself off for the session (logged)
if they don't match.

**Retirement.** A stop that runs past the pit lane's own limit (about 15.8 s in the box, longer for penalty stops
and for a stop you didn't call) is a stuck wheel nut. It rolls the **Retirement chance** once. The stuck nut plays
out in full; if the roll missed the car goes back out, if it landed the team retires the car when the crew
finish: the plugin sends the game's own "retired" status, the one the pause menu's Retire sends, and keeps the car
in its box. An AI car is put in its garage 5 s later; for your car the game's retirement takes over. The message is
found from the launcher bindings `Game_Game_RetireLocalPlayer` and `g_Game_MessageBus` (Terminal Damage's byte
pattern on older launchers). Short stuck nuts (under about 13 s) finish under the limit and never end a race.

A push of your retired car into the garage by Terminal Damage's crew is in the plugin but switched off
(`kTdHandOff`): on the current game build Terminal Damage's push doesn't start. It waits for a Terminal Damage
update.

**Loose wheel.** Now and then a normal stop sends you out with a wheel that isn't on properly (**Loose wheel
chance**): "LOOSE WHEEL - box this lap". Pit and you get a normal stop that fixes it. Cross the line on track
without pitting and the wheel comes off: the plugin sets the game's own damage for that corner just past its
detach limit, the game tears the wheel off and its wheel loss ends your race (if the car is somehow still running
20 s later, the team retires it). A flashback back before the line leaves the wheel loose again; one that leaves
you past the line takes it off again. A red flag fixes it. AI cars can't be told to pit, so for them it's a very
rare incident (**AI loose wheel chance**): the wheel comes off 5-25 s into the out-lap. The safety car is left out.

**Call your stop** (race stops). Pick **Box This Lap** in the MFD radio menu before you come in: the engineer
confirms ("BOX CONFIRMED") and the crew come out for you as usual. Come in without calling and the crew aren't
ready: they stay in the garage until you have stopped in your box, then run out and do the stop.
- The engineer's confirmation is caught with the launcher's speech event.
- The crew's call-out (MessageCarEnteredPitLane, the message Terminal Damage also holds back for a broken car) is
  held back with the launcher binding `Ne_Post_BusMessage` and sent later. Its type is read from the game's code,
  starting at the launcher bindings `Game_PitLane_EnterLimitZone` and `g_Game_MessageBus`.
- The pit lane still starts your crew's run-out as the car gets close, so that run-out is held at its first frame,
  in the garage, until the car is in its box. Started that late, the game plays the run-out at a catch-up speed;
  the plugin plays it at the crew's normal pace instead (their rate, at +0x3bc in the crew's object per the
  launcher's research notes, divided out). Your crew are found by garage order (every crew plays its idle
  sequence as the session loads, in garage order) or by your or your team-mate's last stop.
- If the call-out can't be found, the crew come out but stand still in the box instead.

**Call your stop** in the settings turns this off: the crew are then always ready, as in the stock game. A loose
wheel you were told to box for counts as called; a car Terminal Damage has broken never waits. AI stops
are unchanged. Without the launcher's speech events every stop counts as called.

**Pit exit line.** Crossing the white line at the pit exit in a race costs a 3 s time penalty from the first offence,
with the engineer's radio call, as in real F1 (the stock game warns first). It's the game's own rule,
`rules.crossing_pit_white_line.<long_race / short_race>.penalty_level_0.level`, set from 2 (warning) to 4 (3 s, the
stock game's own second-offence level); the game issues and serves it. Practice and qualifying stay warnings. Applies
to every car.

The F2 bands are unchanged.

## Settings

- **Stuck wheel nut chance (%)**: 3 by default; how many stops get a stuck wheel nut. 0 turns it off, 100 gives one
  on every stop (testing).
- **Retirement chance (%)**: 25 by default; how many stuck wheel nuts end the car's race. 0 turns it off.
- **Loose wheel chance (%)**: 3 by default; how many of your normal race stops leave a loose wheel. 0 turns
  it off.
- **AI loose wheel chance (%)**: 1 by default; how many AI stops lose a wheel on the out-lap. 0 turns it off.
- **Ferrari Mode**: Off by default. On, Ferrari's cars get a stuck wheel nut or a loose wheel at 50 % (a
  joke mode). Ferrari is found by its team badge in the launcher's live data.
- **Call your stop**: On (default) or Off. Off: the crew are always ready, whether you called or not.
- **Log pit stops (testing)**: Off by default; writes each stop's details to the launcher log.

Every setting applies at once, in game too (Career → ADDON SETTINGS).

## Limits

- The training band roll happens when a session loads, so a change to it applies from the next session.
- Steam only: the plugin's offsets were found on the Steam build.
- If the plugin turns itself off, stuck nuts over about 15 s are cut short by the pit lane.

## Building

`python tools/make_stuck_nut.py "<F1 25 folder>"` writes the sequences in `erp/` (needs the `zstandard`
package and the launcher's `tools/erp.py` and `tools/vtf.py`). `python tools/make_ops.py` writes
`db/pitstops.json` and `erp/stuck_nut.json`. `build.bat` builds `native/immersive_pitstops.dll` (Visual
Studio 2022 or later with "Desktop development with C++"; the SDK headers in `native/include` are the launcher's,
with the same one-line MSVC fix in `mixin.hpp` as Driver Display's copy). Then run `package.bat`.
