# Testing before 1.0

Every update on the Swofty launcher is approved by hand (the addon reads the game's UDP packets), so 1.0 goes there
only once everything below passes. 0.x builds are for testing (locally, or overtake.gg for early feedback).
Turn on **Debug log** in the addon's settings for test runs; the notes say which `[driver-display]` lines to check.
Ticked: confirmed in a test (the version in brackets). Unticked: still to check on the release candidate.

## Loading and coexistence

- [x] Grand Prix practice and race load and run (0.4.x); a full-length race (0.4.33).
- [x] A career / modded profile with ~70 addons: loads and runs (0.4.29, 0.4.33).
- [x] The tap behind the launcher's own UDP hook: `udp tap (try 1): tapped; ... sendto launcher:...` (0.4.x).
- [ ] Qualifying and time trial load and run.
- [ ] A race with a formation lap, and one without.
- [ ] No crash leaving a session, restarting a session, returning to the menus, and loading a second session.
- [ ] SimHub (or another UDP app) still receives telemetry while the addon runs.
- [ ] UDP telemetry off in the game: the packet values show `-`, nothing else breaks.
- [ ] Shift lights above the LCD keep the game's colours (compare with the addon turned off).
- [ ] Wheel Dash and Wheel OSD turned off (both replace the same wheel display files).

## Race page

- [x] Lap time, gear, RPM, speed, position, LAP n/total match the game (0.4.36 photos).
- [x] Delta (PREV LAP) green faster / red slower; gaps AHEAD yellow, BEHIND green (0.4.36).
- [x] ERS SOC yellow, red under 15 %, blue while overtake is available (0.4.36).
- [x] Straight mode badge: grey with the metres counting down, green READY, purple ACTIVE (0.4.2, 0.4.36).
- [ ] AHEAD / BEHIND show the drivers' codes (HAM, LEC) from the live driver names (log: `driver codes from the
      live state's driver names`; needs the current launcher) (0.4.38).
- [ ] Tyre temperatures (white, centred, with °) and the fuel margin (white) right; `--` / the game's value before
      the packets (1.0).
- [ ] Lap time reads 0:00.000 on the grid and the formation lap, starts at the real start (log: `off the grid at
      ...` only when pulling away) (0.4.29).
- [ ] Flashback, replay and pause: no time added to the lap.
- [ ] Longest values fit: lap time `1:59.999`, RPM `13,100 RPM`, delta `+10.00`, speed `350`, gaps `+59.999`.
- [ ] 2025 content: DRS badge grey with metres, green DRS READY, purple DRS OPEN.

## Car page

- [x] The drawn car shows, every part in place, green when undamaged (0.4.11, 0.4.36).
- [x] Fuel mode, differential, brake bias values right (0.4.36).
- [ ] A damaged part turns amber / red; brakes by brake damage; tyres by wear.
- [ ] Brake temperatures beside the car (fronts on the right), centred brake bias (0.4.37).

## Pit page

- [x] Pit window laps and REJOIN shown (16-18, P22; REJOIN matches the MFD's "Pit Now Rejoin Position") (0.4.36).
- [x] Current tyre and age, front wing damage, tyre wear % shown (0.4.36).
- [ ] Wear % centred without corner labels, coloured green / amber / red at the settings' limits (0.4.37).
- [ ] PIT STOP TIME appears after a stop.
- [ ] Page button cycles race -> car -> pit -> race (log: `page 3`) on a wheel, a controller and the keyboard.

## Flags and messages

- [x] Safety car, VSC, red flag, yellow full screens; SC/VSC background tone; green flag when a caution clears
      (0.4.5, 0.4.7).
- [x] Pit limiter screen, coloured by the flag (0.4.x).
- [x] Penalties: white full screen with the reason; disqualification; chequered flag on the finish (0.4.9).
- [x] Flag strip along the top and the bottom edge (0.4.7).
- [ ] Warnings (track limits, collisions): white on black full screen with the reason (0.4.37).
- [ ] Qualifying: an invalidated lap pops up red `LAP INVALID`.
- [ ] Drive through / stop-go: white screen, served at the pit.
- [ ] Chequered flag when it comes out (CHEQUERED FLAG) and at the finish (FINISHED P n), squares not covering
      the screen at any other time.
- [ ] Lap 2 of a race: blue OVERTAKE / ENABLED (DRS on 2025 content); safety car / rain: OVERTAKE / DISABLED with
      the reason (log: `event: ...`; the `event code seen:` lines list what the 2026 pack sends).
- [ ] Setting changes (ERS mode, fuel mix, brake bias, differential) pop up blue in the ERS SOC cell.
- [ ] Safety car and VSC: full screen 4 s, then the amber bottom bar (SAFETY CAR / VSC, then SC IN THIS LAP / VSC
      ENDING); SC DELTA in the delta box, green above, red too fast; SLOW DOWN screen when too fast; PREV LAP back
      and the green flag after (log: `SC delta ...` every 5 s, `safety car event: type 2 event 1`) (1.0.4).

## Boot screen

- [x] The team logo shows on black when the game hands over the car (Mercedes 0.4.20, Ferrari 0.4.22).
- [x] A logo replaced by a mod shows the replaced one (Corvette Racing over the Cadillac, 0.4.33).
- [ ] A stock team with no logo mod still shows its logo (the by-name layer over the copy, 0.4.33).
- [ ] A custom (12th) team: no boot screen, not a wrong logo (log: `logo slice -1`).
- [ ] Practice: shows when leaving the garage; race: when the intro ends (skipped and watched).

## 2026 Ferrari wheel correction

- [x] Calibration grid on the 2026 Ferrari shows straight (0.4.25).
- [x] Race, car and pit pages on the 2026 Ferrari: lines straight, text level (0.4.27).
- [ ] Any other car: the normal layout (log: `Ferrari wheel correction off`).
- [ ] "Ferrari wheel correction" turned off in the settings: the Ferrari shows the plain layout.

## Release

- [x] Version 1.0.0 in `swofty.json`, README status line updated.
- [ ] `repository` points at the published source (the `driver-display` folder pushed to GitHub).

## After the launcher's 1.10

- [ ] Tyre temperature colours from the game's own optimum temperatures, if the launcher exposes them (the
      compound table in `tyre_window` is a setup guide's guess and is off for 1.0).
- [ ] Review the new bindings for anything the dash can use (the MFD's pit plan, team colours).
