# Testing

1.0.10 (2026-11-19) and 1.1.0 went on the Swofty launcher, auto-approved. Test builds stay local until a release.
Turn on **Debug log** in the addon's settings for test runs; the notes say which `[driver-display]` lines to check.
Ticked: confirmed in a test (the version in brackets). Unticked: still to check on the release candidate.

## Loading and coexistence

- [x] Grand Prix practice and race load and run (0.4.x); a full-length race (0.4.33).
- [x] A career / modded profile with ~70 addons: loads and runs (0.4.29, 0.4.33).
- [x] The tap behind the launcher's own UDP hook: `udp tap (try 1): tapped; ... sendto launcher:...` (0.4.x).
- [x] Practice and qualifying load and run (1.0.17-1.0.22).
- [ ] Time trial loads and runs.
- [ ] A race with a formation lap, and one without.
- [ ] No crash leaving a session, restarting a session, returning to the menus, and loading a second session.
- [ ] SimHub (or another UDP app) still receives telemetry while the addon runs.
- [ ] UDP telemetry off in the game: the packet values show `-`, nothing else breaks.
- [ ] Shift lights above the LCD keep the game's colours (compare with the addon turned off).
- [ ] Wheel Dash and Wheel OSD turned off (both replace the same wheel display files).

## Race page

- [x] Lap time, gear, RPM, speed, position, LAP n/total match the game (0.4.36 photos).
- [x] Delta (PREV LAP) green faster / red slower (0.4.36); gaps AHEAD yellow, BEHIND green (1.0.19: white before,
      the colour order bug).
- [x] ERS SOC yellow, red under 15 %, blue while overtake is available (0.4.36).
- [x] Straight mode badge: grey with the metres counting down, green READY, purple ACTIVE (0.4.2, 0.4.36).
- [x] (1.0.19) AHEAD / BEHIND show the drivers' codes (HAM, LEC) from the live driver names (log: `driver codes from the
      live state's driver names`; needs the current launcher) (0.4.38).
- [x] (1.0.x photos) Tyre temperatures (white, centred, with °) and the fuel margin (white) right; `--` / the game's value before
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
- [x] (1.0.2, ENABLED; DISABLED unconfirmed) Lap 2 of a race: blue OVERTAKE / ENABLED (DRS on 2025 content); safety car / rain: OVERTAKE / DISABLED with
      the reason (log: `event: ...`; the `event code seen:` lines list what the 2026 pack sends).
- [ ] Setting changes (ERS mode, fuel mix, brake bias, differential) pop up blue in the ERS SOC cell.
- [x] (1.0.4) Safety car and VSC: full screen 4 s, then the amber bottom bar (SAFETY CAR / VSC, then SC IN THIS LAP / VSC
      ENDING); SC DELTA in the delta box, green above, red too fast; SLOW DOWN screen when too fast; PREV LAP back
      and the green flag after (log: `SC delta ...` every 5 s, `safety car event: type 2 event 1`) (1.0.4).

## Practice, qualifying and race messages (1.0.16-1.0.22)

- [x] Practice / qualifying layout: sector times purple / green / yellow, TO PB, time left (1.0.19).
- [x] The race layout on the grid, not qualifying's (1.0.19).
- [x] DNF / RETIRED stays on the screen for the rest of the session (1.0.20).
- [x] Start lights, GO at lights out (1.0.22).
- [x] Fastest lap bar with the right driver and time (1.0.22: BEA, LEC, ANT).
- [x] RETIRED and CONTACT across the bottom row (1.0.22).
- [ ] BACKMARKER (compare with the game's own message), PIT WINDOW OPEN, RAIN IN n MIN, FINAL LAP.
- [ ] Battle alert blink; fuel per lap under the margin.

## 1.1.8 / 1.2.0 (launcher 1.10)

- [ ] FL SLOW PUNCTURE (amber) and FL FULL PUNCTURE (red) from the game's puncture state; TYRE WEAR past 75 % still.
- [ ] With UDP off: RETIRED from race control, the practice / qualifying layout from the live session type.
- [ ] YELLOW SECTOR n and SECTOR n CLEAR; nothing under a safety car or red flag.
- [ ] Car page: a punctured tyre amber, then red from halfway to the burst.
- [ ] Safety car screen (yellow, 1.2.0): words and values fit; EST. PIT EXIT POS matches the MFD's rejoin position;
      VSC DELTA under a VSC; SAFETY CAR IN THIS LAP / VSC ENDING across the bottom row; LAP n/total under ERS SOC;
      the race layout back at the green flag; the Ferrari wheel's copy lines up.
- [ ] Sector times (practice / qualifying) fully on the screen.
- [ ] 1.2.6 undercut calls: only near your pit window and against a car on your stop count; UNDERCUT CHANCE for a
      car steady within the gap ahead as the window comes; UNDERCUT THREAT when the car steady behind pits; nothing
      for a leader dropping through your place in the pit lane (1.2.5's Leclerc call); no "P0" OUT lines.
- [ ] Pit page: BETWEEN shows the two cars around the rejoin position (compare with the MFD).
- [ ] 1.2.3 radio tags: "radio tags: on" at load; a tag on the bottom row for box calls, strategy changes,
      weather and faults; "radio: eng_... -> TAG / (no tag)" for every engineer line in the log, to tune the list.
- [x] 1.2.4's held numbers: still blurred and no longer smooth; taken out in 1.2.5 (the game's own wheel screen blurs
      too: anti-aliasing / upscaling on the scene).
- [ ] 1.2.7 catch / hold: one call a lap from lap 3; CATCH with a sensible target or "IN n LAPS" when catching;
      HOLD only when the car behind is quicker; nothing under a safety car, for cars over 10 s away or with UDP off;
      "pace: lap n of N, mine ...: ..." in the log.
- [ ] 1.2.8: the game starts on launcher 1.10.23+ (1.2.1-1.2.7 removed wheel elements the F1 / F2 HUD dial
      templates of flow_options.erp also name: the launcher refused the build). The build keeps them (mod_1..4,
      ers_null, ignore_null: 465 of 512 nodes); check the options menu's HUD dial preview too.
- [ ] 1.2.9 rev lights: "rev lights: the LED update is wrapped" at load; in a career with a created driver the wheel
      LEDs match the team-mate's style (first five as mode lights, one green in a straight-mode zone);
      "rev lights: player car N 2/2/2 -> a/b/c (car M's, the team-mate)" in the log; the setting off brings the
      game's bar back; nothing changes for a real driver; no "left to the game" line.
- [ ] 1.2.10 rev lights: a created driver with a created team-mate (My Team) gets the real style from most cars
      ("cars with the player's own style skipped"); a real driver logs "keeps the game's lights (... the same as most
      cars)"; the style comes back after a restart / the next session.
- [ ] The boot logo leaves no faint copy on the page after it (3 s logo, then 1 s black).
- [ ] 1.2.1: the game's own wheel screen is out of the layout (378 of 512 nodes, colour to node 102). Check every
      page, the boot screen, the flag screens and the safety car screen still draw, and the log has no new errors
      from the wheel display when it switches on or off (the game's display_enabled / disabled signals).

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

- [x] Version 1.1.0 in `swofty.json`, README status line updated (1.1.1: the feature list left out of the description: the launcher shows it in the small settings panel).
- [x] `repository` points at the published source (the `driver-display` folder pushed to GitHub).

## After the launcher's 1.10

- [x] Tyre temperature colours: launcher 1.10 has no optimum temperatures; they stay white (the user's choice).
- [ ] Review the new bindings for anything the dash can use (the MFD's pit plan, team colours).
