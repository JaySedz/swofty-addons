# Testing before 1.0

Every update on the Swofty launcher is approved by hand (the addon reads the game's UDP packets), so 1.0 goes there
only once everything below passes. 0.x builds are for testing (locally, or overtake.gg for early feedback).
Turn on **Debug log** in the addon's settings for test runs; the notes say which `[cockpit-hud]` lines to check.

## Loading and coexistence

- [ ] Game loads into: a race (grid start), a race with a formation lap, practice, qualifying, time trial.
- [ ] No crash leaving a session, restarting a session, returning to the menus, and loading a second session.
- [ ] Wheel Dash and Wheel OSD turned off; no other addon replacing `lcd_display.idf` / `race_assets_f1.ui`.
- [ ] Shift lights above the LCD keep the game's colours (2026: left green, right purple). Compare with the addon
      turned off.
- [ ] SimHub (or another UDP app) still receives telemetry while the addon runs.
- [ ] With the launcher's own UDP hook active (a career with custom drivers: `[udp] stock ids on` in the log) the
      dash and the launcher both still work: the log says `udp tap (try 1): tapped; ... sendto launcher:...`
      (the tap behind the launcher's hook), the page button, ERS mode, MIX/BBAL and the colours all work.

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

## Flags, colours, pages (0.3.0)

- [ ] Yellow flag in your sector (an incident ahead, no safety car): amber YELLOW FLAG box and blinking yellow
      strip (log: `track status: yellow`). Blue flag when being lapped: blue box and strip.
- [ ] Tyre temperature colours change at the limits set in the addon's settings; wear colours too.
- [ ] Fuel margin red when below 0, green above.
- [ ] Page button via UDP Action 1 (bound in the game's controls) switches race <-> car page (log: `page 2`).
- [ ] Page button bound on a wheel, a controller and the keyboard.
- [ ] Car page: wing / floor / diffuser / sidepod damage matches the game's damage screen; brake temperatures;
      gearbox / engine / ICE in the panel, hidden under a pop-up.

## DRS, straight mode, overtake (0.3.3)

- [ ] 2025 content: DRS cell grey OFF, metres to the next zone, green READY in a zone, cyan OPEN when open.
- [ ] 2026 content: ERS mode turns blue in the overtake window and while using it, yellow otherwise.
- [ ] 2026 content with the Debug log on: drive through straight-mode zones and use overtake; the log's
      `packet 16 (...)` lines map straight mode (send the log).

## 0.4.0 layout (the mock-ups)

- [ ] Race page: everything inside its cell; LAP TIME runs, shows the finished lap for 3 s at the line; LAP n/total
      in a race; P01 style position; gear R / N / 1-8; RPM with a thousands comma.
- [ ] Delta against the previous lap (default) and against the best lap (setting); the label follows.
- [ ] Mode badge: OVERTAKE blue (2026); DRS green / DRS OPEN yellow (2025); gone otherwise.
- [ ] Car page: the block car readable; parts change colour with damage; tyres with wear; FUEL MODE, DIFF, BRAKE
      BIAS values right.
- [ ] Pop-ups cover the bottom middle cell on both pages; the flag strip along the top edge.
- [ ] The Δ sign shows in "FUEL Δ" and "PREV LAP Δ" (if it shows as a box, the font lacks it).

## Release

- [ ] Version 1.0.0 in `swofty.json`, README status line updated.
- [ ] `repository` points at the published source.

## Penalties, chequered flag, pit page (0.4.6)

- [ ] A time penalty (e.g. track limits after three warnings): white screen `+5 SEC PENALTY` with the reason for
      4 s (log: `penalty event: type 4 ...`).
- [ ] A track limits warning: amber pop-up `WARNING` / `TRACK LIMITS` in the ERS SOC cell.
- [ ] Qualifying: an invalidated lap pops up red `LAP INVALID`.
- [ ] Drive through / stop-go (e.g. pit lane speeding): white screen, and UNSERVED on the pit page until served.
- [ ] Chequered flag: chequers with FINAL LAP when it comes out, FINISHED P n when you cross the line (log:
      `chequered flag out`, `finished: ...`). A red flag still shows over it.
- [ ] Page button cycles race -> car -> pit -> race (log: `page 3`).
- [ ] Pit page: PIT WINDOW laps and REJOIN match the MFD's pit panel (log: `session packet N bytes: pit window
      a-b, rejoin Pn ...`); OPEN in green inside the window.
- [ ] Pit page: tyre compound, colour and age match the game; PIT STOP TIME appears after a stop.
- [ ] Pit page: front wing damage matches the game's damage screen; tyre wear % matches the car page's colours.
- [ ] VSC screen reads "VSC" and fits; the safety car screen's black text reads clearly on the yellow.
- [ ] Flag strip blinks along the bottom edge too (check on the Ferrari wheel, which cuts the top off).

## Textured car (0.4.9)

- [ ] Car page shows the drawn car (not the battery icon, not blank): every part in place, none upside down or
      mirrored. If the battery icon shows stretched, the LCD material ignores textureOverride.
- [ ] Damage a front wing: that wing turns amber / red; brakes colour by brake damage; tyres by wear.
- [ ] Nothing of the car shows on the race or pit page.

## Boot screen (0.4.20)

- [x] The team logo shows on black when the game hands over the car (Mercedes, 0.4.20).
- [ ] Every 2026 team's logo (log: `boot screen: team <key>, logo slice N`, N 0..10).
- [ ] A logo swapped with the launcher's picture addon shows the swapped one.
- [ ] A custom (12th) team: no boot screen, not a wrong logo (its key isn't in the 2026 set).
- [ ] Practice: shows when leaving the garage; race: when the intro ends (skipped and watched).

