# Immersive Pitstops

Pit stops in F1 25 come out at almost exactly the same time every stop: the game gives each driver one stop time
for the whole session. This addon makes pit stops something that can go right or wrong:

- every stop's time varies, quicker or slower;
- a wheel nut can stick and hold the car on its jacks while the crew fight it, usually for a few seconds, now
  and then for up to a minute; sometimes the team gives up and the car's race is over;
- now and then a wheel isn't on properly: box next lap to have it fixed, or it comes off;
- call your stop with **Box This Lap** in the MFD radio menu, or the crew are still in the garage when you get there;
- crossing the pit exit line costs a time penalty from the first offence.

The game's own small crew mistakes happen as in the stock game. Races in My Team, Driver Career and Grand Prix, for
you and the AI teams (the call and the loose wheel's "box this lap" are yours).

It changes game database values and the crew's "wheel on" mistake animations, and has a small native plugin
(`native/src/immersive_pitstops.cpp`, source included). No network access, no files written, no processes
started.

Uses work from Terminal Damage by nyannoying:
[https://github.com/nyannoying1337/f1-25-terminal-damage](https://github.com/nyannoying1337/f1-25-terminal-damage)
(the way a car is retired: the game's own retirement message and the byte pattern that finds it, now only a
fallback for launchers before 1.10.26; and the way a wheel is torn off: the game's own corner damage set just past
its detach limit; and the layout of its public API). Copyright (c) 2026 nyannoying, used under the Terminal Damage
Attribution License 1.0 (`LICENSE-terminal-damage`). Not endorsed by Terminal Damage's author.

## What it changes

**Every stop varies.** The game gives each driver one stop time for the whole session (its pit crew training band:
about 2.4 s in Driver Career). The plugin speeds each stop's crew up or slows it down (**Stop times**):
- Default: about 70 % of stops land in the normal range (about 2.2-2.6 s), the rest quicker (down to about 1.8 s) or
  slower (up to about 2.9 s), half and half.
- More fast stops: quicker on average; about a third of stops are quick ones, down to about 1.7 s.
- More slow stops: slower on average; about a third of stops are slow ones, up to about 2.9 s.

Without a crew mistake or a stuck wheel nut, no stop goes past about 3 s. Yours and the AI's. It scales the crew's
animation time through the launcher binding `Sess_PitStopUpdateJob_Update`. The game's own crew mistakes happen as in
the stock game.

**Stuck wheel nut.** A separate incident from the game's own crew mistakes: at the start of each stop the plugin
rolls **Stuck wheel nut chance** (1 % by default), yours and the AI's. On a hit it hands the crew a "wheel on" mistake
sequence instead of the normal stop: the tyre-on man fights the wheel and the gun man keeps retrying with the gun
while the rest of the crew, the jacks and the car hold still. A "STUCK WHEEL NUT" alert tells you once you're
released (the HUD's alert box isn't seen in the pit lane); the engineer has no line for it, as it isn't one of the
game's own mistakes.
- The sequences: the game's "wheel on" mistakes (in `pitstops/f1/pitstop_24/25/26.erp`, and their front wing adjust
  versions) rebuilt with a minute-long pause, built from the game's own files by `tools/make_stuck_nut.py`. The plugin
  swaps the sequence name it passes to `Game_PitStopInstance_PlaySequence` (a launcher binding).
- The length: about 5 s (60 %), 10 s (25 %), 20 s (10 %), 30 s (4 %) or a full 60 s (1 %). When the time is up the
  plugin moves the crew's animation clock ahead by whole repeats of the struggle, to the same pose near the end of the
  pause, and the stop carries on. (The clock is the crew sequence's time, PitStopInstance +0x3b0 then +0x5c, from the
  launcher's research notes: checked sane before it's written, or the stuck nut plays its full minute.)
- When the game picks one of its own "wheel on" mistakes, it plays at its stock length.
- At 1 % a stuck nut comes up about once in three seasons for your car, nearly always a short one; a full-minute one
  about once in 10,000 stops, roughly once a decade across the grid. Only stuck
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

**Wrong tyres.** Now and then the crew fit a different compound from the one you picked (**Wrong tyres chance**),
always one for the current weather, and you're told once you leave the box. The AI teams get it too, with no alert.

**Loose wheel.** Now and then a normal stop sends you out with a wheel that isn't on properly (**Loose wheel
chance**): "LOOSE WHEEL - box this lap". Pit and you get a normal stop that fixes it. Cross the line on track
without pitting and the wheel comes off: the plugin sets the game's own damage for that corner just past its
detach limit, the game tears the wheel off and its wheel loss ends your race (if the car is somehow still running
20 s later, the team retires it). A flashback back before the line leaves the wheel loose again; one that leaves
you past the line takes it off again. A red flag fixes it. AI cars can't be told to pit, so for them it's a very
rare incident (the same chance): the wheel comes off 5-25 s into the out-lap. The safety car is left out.

**Call your stop** (race stops). Pick **Box This Lap** in the MFD radio menu before you come in: the engineer
confirms and the crew come out for you as usual. Come in without calling and the crew aren't
ready: they stay in the garage until you have stopped in your box, then run out and do the stop.
- The engineer calling you in counts too: every engineer line in the game's subtitles that tells you to box (read
  one by one): the pit window ("box this lap", "the pit window is open"), a strategy change, a new strategy he
  proposes that you accept, damage to the front wing, rear wing, sidepods, diffuser or floor, a stuck-open DRS,
  punctures, the weather and tyre calls, pitting under the safety car, pitting early for traffic, and low fuel. His
  "stay out" lines and the pit window countdowns don't count.
- The engineer's lines are caught with the launcher's speech event.
- The crew's call-out (MessageCarEnteredPitLane, the message Terminal Damage also holds back for a broken car) is
  held back with the launcher binding `Ne_Post_BusMessage` and sent later. Its type is read from the game's code,
  starting at the launcher bindings `Game_PitLane_EnterLimitZone` and `g_Game_MessageBus`.
- The pit lane still starts your crew's run-out as the car gets close, so that run-out is held at its first frame,
  in the garage, until the car is in its box. Started that late, the game plays the run-out at a catch-up speed;
  the plugin plays it at the crew's normal pace instead (their rate, at +0x3bc in the crew's object per the
  launcher's research notes, divided out). Your crew are found by garage order (every crew plays its idle
  sequence as the session loads, in garage order) or by your or your team-mate's last stop.
- If the call-out can't be found, the crew come out but stand still in the box instead.

**Crew readiness** on the Pit Stops page (Always ready) turns this off for a team: its crew are then always ready, as
in the stock game; Never ready keeps them in the garage even when you call. A loose
wheel you were told to box for counts as called; a car Terminal Damage has broken never waits. AI stops
are unchanged. Without the launcher's speech events every stop counts as called.

**Pit exit line.** Crossing the white line at the pit exit in a race costs a time penalty from the first offence,
with the engineer's radio call (the stock game warns first). The game sets the length by race distance: 3 s in a short
race, 10 s in a long one. It's the game's own rule,
`rules.crossing_pit_white_line.<long_race / short_race>.penalty_level_0.level`, set from 2 (warning) to 4 (the stock
game's own second-offence level); the game issues and serves it. Practice and qualifying stay warnings. Applies to every
car.

**Driver Display.** With Driver Display also installed, this addon shares its pit events with it (a stuck wheel nut
and the time in the box, a loose wheel, a wheel that came off, a retirement and why), through a launcher service
(`native/src/ip_link.hpp`); Driver Display shows them. It only reads them: nothing changes in either addon when the
other isn't installed.
With Driver Display 1.2.25 or later, **Ferrari Mode** (Light or Full) also changes Driver Display's Ferrari pages (in the
2026 Ferrari).

## Settings

Every chance, crew readiness and stop times are set on the **Pit Stops** page (below). The addon's settings only hold
the testing switches and logging:

- **Loose wheel every stop (testing)**: Off by default. On, every normal race stop of yours leaves a loose wheel.
- **AI loose wheel every stop (testing)**: Off by default. On, every AI normal race stop leaves a loose wheel.
- **Wrong tyres every stop (testing)**: Off by default. On, every race stop of yours gets the wrong tyres.
- **AI wrong tyres every stop (testing)**: Off by default. On, every AI race stop gets the wrong tyres.
- **Log pit stops (testing)**: Off by default; writes each stop's details to the launcher log.

They apply at once, in game too (Career → ADDON SETTINGS).

## The Pit Stops page

The addon adds a **Pit Stops** page to the launcher's sidebar, with three tabs. Changes save at once, and while the game
runs they apply within a couple of seconds.

- **Teams**: every team's pit stop values in a table:
  - **Stuck wheel nut chance**: 1 % by default; 100 gives one at every stop.
  - **Retirement chance**: 25 % by default. Chance a stuck wheel nut ends the car's race.
  - **Loose wheel chance**: 0.3 % by default, 0-10 in 0.1 steps. Your car will be told to box this lap. AI cars lose the wheel on its out lap.
  - **Wrong tyres chance**: 0.3 % by default, 0-10 in 0.1 steps.
  - **Crew readiness**: Ready when called (default: an uncalled stop finds the crew in the garage), Always ready or
    Never ready. Your stops only: the AI always calls its stops.
  - **Stop times**: Default, Fast or Slow (see above).
  The first row, **All teams**, sets every team's values. Click a team and turn on **Own values** to give its crew its
  own; turning it off goes back to All teams. Values apply to the team's cars, yours and the AI's: it's the crew doing
  the stop.
- **Extra Features**: Extra team features, on top of the team tab's values
  - **Ferrari Mode** (Off, Light or Full; Off by default): Ferrari only, player and AI. In Light mode your crew is never ready for you, so your stops are a bit slower. Full mode increases the chance of a stuck or loose wheel to 50%. Adds Ferrari specific popups to Driver Display if installed.
  - **Papaya Rules** (Off by default): McLaren only, player and AI. Both McLarens get identical pit stops.
- **MyTeam**: your MyTeam career team's crew values, the same as a team's. More MyTeam features are planned.

The page keeps its settings in `config.json` in the addon's launcher data folder and hands it to the game when you
press Play (`swofty/addons/sedz.immersive-pitstops/`). Up to 1.0.42 the chances were addon settings; 1.0.43 starts
All teams from them if you pressed Play with 1.0.42, else from the defaults. Ferrari Mode was an addon setting up to
1.0.41; pick it again on the page.

## Limits

- Needs launcher 1.10.41 or later.
- Tested on the Steam copy. The EA app copy (1.0.145) should work but hasn't been tested. It's the same game version, so
  the pit lane and crew values the plugin reads are the same, and the launcher has every binding it uses for that
  build. Its code is protected, so the plugin follows jump stubs when it reads the retirement and crew call-out
  messages from the game's code, and logs when it does.
- Every part checks before it acts and falls back to the stock game instead of writing anything it can't confirm:
  - a launcher binding missing on the build: that part stays off (logged once at load);
  - the box hold needs the launcher to report your car's garage, to confirm the pit lane record; without it the hold
    stays off, the game's own pit lane timing applies, and the plugin's stuck wheel nuts are off too (a long one would
    be cut short mid-animation);
  - the retirement and crew call-out messages are only sent if their type lies inside the game's own image; without
    the retirement message, stuck wheel nuts never end a race;
  - the wheel tear-off and the stuck nut length cut check their values first (the wheel stays on and the car is
    retired; the stuck nut plays in full);
  - without the launcher's engineer speech events (the EA app copy, on the current launcher) every stop counts as
    called, as in the stock game.
  Each failure is logged once, not every frame.

## Building

`python tools/make_stuck_nut.py "<F1 25 folder>"` writes the sequences in `erp/` (needs the `zstandard`
package and the launcher's `tools/erp.py` and `tools/vtf.py`). `python tools/make_ops.py` writes
`db/pitstops.json` and `erp/stuck_nut.json`. `build.bat` builds `native/immersive_pitstops.dll` and the page's
launcher helper `bin/immersive-pitstops-helper.exe` (`helper/src/main.cpp`) (Visual
Studio 2022 or later with "Desktop development with C++"; the SDK headers in `native/include` are the launcher's,
with the same one-line MSVC fix in `mixin.hpp` as Driver Display's copy). Then run `package.bat`.
