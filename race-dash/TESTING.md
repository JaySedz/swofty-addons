# Testing before 1.0

Every update on the Swofty launcher is approved by hand (the addon reads the game's UDP packets), so 1.0 goes there
only once everything below passes. 0.x builds are for testing (locally, or overtake.gg for early feedback).
Turn on **Debug log** in the addon's settings for test runs; the notes say which `[race-dash]` lines to check.

## Loading and coexistence

- [ ] Game loads into: a race (grid start), a race with a formation lap, practice, qualifying, time trial.
- [ ] No crash leaving a session, restarting a session, returning to the menus, and loading a second session.
- [ ] Wheel Dash turned off; no other addon replacing `lcd_display.idf` / `race_assets_f1.ui`.
- [ ] Shift lights above the LCD keep the game's colours (2026: left green, right purple). Compare with the addon
      turned off.
- [ ] SimHub (or another UDP app) still receives telemetry while the addon runs.
- [ ] With the launcher's own UDP hook active (a career with custom drivers: `[udp] stock ids on` in the log) the
      dash and the launcher both still work.

## Layout

- [ ] Every value centred in its cell; nothing overlaps the grid lines or the next cell (lap time vs RPM, delta).
- [ ] Grid lines visible on the wheel.
- [ ] Longest values fit: lap time `1:59.999`, RPM `13100`, delta `+10.00`, speed `350`, fuel `+10.00`,
      gaps `+59.999`, ERS `OVERTAKE`, pop-up `VIRTUAL SAFETY CAR`.
- [ ] Nothing of the game's stock LCD shows through the backdrop.

## Values

- [ ] Last lap: matches the game's own lap time (log: `udp tap: ... lap 1 (last N ms)`).
- [ ] Delta: green when faster than your best lap, red when slower, grey `--.--` before a reference exists.
- [ ] Opening race lap timed from leaving the grid (log: `off the grid at ...`), not on the formation lap.
- [ ] Flashback: no time added to the lap (log: `flashback: ...`); replay and pause: no time added.
- [ ] Lap number matches the game's.
- [ ] Speed, RPM, gear, position, SOC % and bar (red under 15 %) match the game.
- [ ] Tyre temperatures (all four) and wear % (all four, the right corner each).
- [ ] Fuel margin matches the game's fuel display.
- [ ] Gap ahead (green) and behind (red); `-` when P1 / last.
- [ ] MIX and BBAL match the game's settings; changing them pops up the blue box with the new value.
- [ ] DIFF change pops up (log: `udp tap: ... setup 1 (diff N)`).
- [ ] ERS mode names right for every mode (log: `ERS mode a -> b`); the change pops up.
- [ ] Safety car, VSC: amber box for as long as it's out. Red flag: red box.
- [ ] UDP telemetry off in the game: the UDP values show `-`, nothing else breaks.

## Release

- [ ] Version 1.0.0 in `swofty.json`, README status line updated.
- [ ] `repository` points at the published source.
