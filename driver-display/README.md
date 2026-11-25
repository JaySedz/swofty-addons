# Driver Display

A full HUD on the F1 25 steering wheel display, built to replace the game's on-screen HUD (for VR, or with the
game's HUD turned off). Its values come from the launcher's live car state (`swofty::live`), the launcher's
telemetry API, the game's own LCD data, and the game's UDP telemetry packets.

**About the UDP packets:** the addon reads the telemetry packets the game itself sends by wrapping the game's
calls to Windows' `sendto`. **It passes the game's own packets through unchanged and opens no sockets**: it
sends and receives nothing of its own, so SimHub and other telemetry apps keep receiving everything. The source
is linked in the addon's repository field. See "Reading the game's UDP packets" below.

**Status: 1.4.0. Needs launcher 1.10.41 or newer.** Tested on the Steam copy of the game. The EA app copy gets live
race data from launcher 1.10.40 and the telemetry feed from 1.10.41, the data the dash was missing there, and the
one game function the dash hooks (the wheel LEDs) is bound on the EA build too; it hasn't been tested on EA yet
(reports welcome: the log's "[driver-display] udp tap" and "rev lights" lines say what it found). Turn off Wheel Dash (`ross.wheel-dash`) and Wheel OSD (`ashton.wheel-osd`) while using it:
they replace the same wheel display files. Turn the game's UDP telemetry on (Settings > Telemetry) for brake bias,
fuel mix, the differential, tyre temperatures and wear, the page button and the game's exact lap times.

## The screen

Three pages, switched by the page button (a "UDP Action" bound in the game's controls); the bottom middle cell
(ERS SOC and the mode badge) and its pop-ups are on all of them.

**Race page**

| | | |
|---|---|---|
| LAP TIME (running; the finished lap for 3 s after the line) | GEAR, RPM, SPEED | PREV LAP / BEST LAP delta (green faster, red slower) |
| POS, LAP n/total | | Gaps to the cars ahead / behind, labelled with their drivers' codes (HAM, LEC) |
| Tyre temperatures FL FR / RL RR | ERS SOC % + mode badge | FUEL margin |

**Practice, qualifying and time trial:** the race page changes its right column to the sector times (S1, S2,
S3: purple the session's best of any car, green your best, yellow neither; each stays until you next complete that
sector), shows the delta to your best lap (TO PB) under the lap time in place of LAP n/total, and the session's
time left (Q2 TIME LEFT) in place of the fuel margin. Needs the game's UDP telemetry.

**Car page:** a top-down car drawn from its own textures (art/car), each part coloured by its damage (front
wings, rear wing, sidepods, floor and bargeboards, diffuser, gearbox, brakes: green / amber / red, grey until
known), the engine cover by the engine's temperature (green, amber from 130 C, red from 150 C) and the tyres by their wear; chassis, cockpit and suspension grey. Brake temperatures either
side of the car (the fronts by the nose); FUEL MODE and DIFF bottom left, BRAKE BIAS bottom right.

**Pit page:** PIT WINDOW laps (green while you're in it) and CURRENT TYRES (compound colour, age) on top; REJOIN
POSITION (where you'd rejoin if you pitted now) and FRONT WING DAMAGE (left / right) in the middle; tyre wear per
corner bottom left, the last PIT STOP TIME bottom right. The MFD's pit plan (next tyres, downforce level, wing
repair) is the MFD's own data and nothing outside it can read it, so the page can't show it.

- **Mode badge** under ERS SOC: straight mode (2026) or DRS (2025): an empty grey outline counting the metres
  down to the next zone, a green box when ready, a purple box (the game's colour) when active / open. ERS SOC
  turns blue while overtake is available or on (2026).
- **Flags** fill the whole screen: red flag, safety car / VSC / formation lap (bright yellow), yellow, blue flag.
  The "Flag screen" setting shows them for 4 s when they come out (then a blinking strip along the top and bottom
  edges: some wheels, such as the Ferrari's, cut the top off) or for as long as they are out. After the flag
  screen the background stays dark amber under a safety car / VSC and dark red under a red flag. When a safety
  car, VSC or yellow clears, a **green flag** flashes over the whole screen for 3 s.
- **Safety car / VSC:** after the flag screen the bottom row turns amber with SAFETY CAR or VSC (SC IN THIS LAP /
  VSC ENDING when the game says it is ending), and the delta box becomes SC DELTA: the game's safety car delta,
  green while above it, red when too fast. Going under it shows SLOW DOWN in white on black for 3 s, and in the bar
  until back above it. The delta box returns to the lap delta when the caution clears. Needs the game's UDP
  telemetry.
- **Pit limiter:** the whole screen (PIT LIMITER with your speed) while it is on, coloured by the flag out: blue
  normally, yellow under a yellow, amber under a safety car / VSC, red under a red flag.
- **Penalties** (time penalty, drive through, stop-go, disqualification): white over the whole screen for 4 s
  with the reason. **Warnings** (track limits, a collision): white on black for 3 s, the black and white flag. An
  invalidated lap pops up red in the ERS SOC cell.
- **DRS / overtake enabled and disabled** (with the reason: wet track, safety car, red flag, too early): the whole
  screen in blue (the game's overtake colour) for 3 s, as the game announces them at the top of its HUD.
- **Chequered flag:** chequers over the screen, 4 s when it comes out, 8 s when you finish (FINISHED P n). Out of
  the race (DNF, RETIRED, NOT CLASSIFIED): white on black for the rest of the session.
- **Setting changes** (ERS mode, fuel mode, brake bias, differential) pop up in the ERS SOC cell for 2 s.
- **Start lights:** the screen goes black and the five red lights come on with the game's, then a green GO at
  lights out. Setting "Start lights screen" (1.2.15, on by default): off keeps the normal page through the start.
- **Immersive Pitstops** (1.2.16, only when [Immersive Pitstops](../immersive-pitstops) 1.0.21+ is installed too;
  without it nothing here runs): your stuck wheel nut as an amber full screen with the time in the box, a loose wheel
  across the bottom row in red (LOOSE WHEEL  BOX NOW) until the pit lane, WHEEL OFF and RETIRED full
  screens in red, and AI cars' stuck nuts and loose wheels on the bottom row. Read only: it reads what Immersive Pitstops shares through the launcher's services.
- **Team pages** (1.3.0): Ferrari, McLaren, Mercedes, Red Bull, Aston Martin (1.3.1), Williams (1.3.4), Audi / the 2025 Sauber (1.3.7), Haas (1.3.9) and Alpine (1.3.10), on both the 2025 and 2026 cars (1.3.2), show their own team's dash on every page
  (race, practice / qualifying, safety car / VSC, car, pit), in their team's style; other teams follow in later
  updates, and every other car keeps the standard pages. On the team pages the battery bar turns blue in the
  overtake zone, and the mode badge reads STRAIGHT (purple while active). Setting "Team pages speed unit": KPH or MPH.
- **2026 Ferrari pages** (after Ferrari's 2026 race screen): on the 2026 Ferrari every page uses Ferrari's own frame, every value white on a coloured
  box. Every page: SPEED / REV (on red) top left, the mode bar (purple active, green ready, grey otherwise) under
  the centre, the MGU-K bar on the right (blue deploying, red harvesting), ERS SOC as a number and a 20-segment
  bar. Race: lap time, last sector's colour, delta (green / red / grey), LAP / TO GO, BB / FUEL, the car ahead and
  behind, the gear in the centre, tyre temperatures. Practice / qualifying: last lap, the session (FP2, Q1), to
  your best lap, STINT LAP / TIME LEFT, PERSONAL BEST, S1-S3 on their colours. Under a safety car / VSC: the banner
  and SC delta in yellow, LAP / TO GO, BB / FUEL, EST. PIT EXIT and PIT WINDOW, the gear on yellow with the speed
  under it. Car page: the car in the middle, FUEL MODE / DIFF. Pit page: PIT WINDOW (green when open), the tyres
  on their compound's colour, REJOIN, REPAIR WING (red YES once the front wing is in the red band), PIT STOP TIME,
  tyre wear on its colour. Speed in KPH or MPH by the setting "Ferrari page speed unit". Other cars keep the usual
  pages. On the Ferrari pages (1.2.24) the setting-change pop-up is a box in the upper centre and the
  bottom-row messages (race messages, fastest lap, the safety car bar) a banner across the lower centre, white
  text on their colour; full screens are unchanged.
- **2026 McLaren pages** (after McLaren's 2026 race screen): minimal,
  no boxes, coloured text where McLaren's has it. Every page: the battery as a vertical bar on the left (13
  segments and the %), the revs as one on the right (20 segments against the rev limit), the mode badge over a big
  gear with the MGU-K line under it (orange deploying, red harvesting, grey otherwise), tyre values round a car
  icon. Race: lap time, fuel delta, lap n/total, position, RPM, the delta (green / red), speed, the interval list
  (the nearest five cars, up to three ahead). Practice / qualifying: last lap, personal best, time left, stint
  lap, the delta to your best (green / yellow), the last lap's sectors. Under a SC / VSC: the top half yellow with
  the SC delta, the banner, gear and speed and the estimated pit exit, then lap, fuel, pit window and brake bias.
  Car page: the car, fuel mode, diff, brake bias, fuel delta. Pit page: pit window, rejoin, repair wing (orange
  YES), lap, the tyres in their compound's colour, speed, tyre wear, pit stop time. Pop-ups and messages use the
  same centre box and banner as the Ferrari's pages.
- **2026 Mercedes pages** (after Mercedes's 2025 race screen): a grid
  of cells under a header with the page's name, a 12-light RPM strip (cyan, orange, lilac) and the session's time;
  the gears as a column on the left with the current one lit, the battery as a 20-segment column on the right.
  White text only: the delta's cell turns green or red, the tyres' cell takes the compound's colour, the best
  lap's cell is green, REPAIR WING's cell orange, three yellow cells across the top under a safety car / VSC.
  Race: delta, position, lap, lap time, tyre temperatures, the gear, the cars ahead and behind, the mode, speed,
  fuel delta, brake bias. Practice / qualifying: delta to best, position, stint lap, last lap, sectors, tyre
  temperatures, personal best. Car page: the car, fuel mode, differential, brake bias. Pit page: pit window,
  tyres, tyre wear, rejoin, repair wing, pit stop time.
- **2026 Red Bull pages** (after Red Bull's race screen): an open layout with a big speed and a
  light top left (the last sector's pace: grey, yellow, green your best, purple the session's best), a huge gear in the centre, the deltas and lap time on the right, a list down
  the left (lap, position, tyre age, fuel delta; practice: the sectors and personal best), the battery as a green
  column on the left edge (blue in the overtake zone), a yellow box at the bottom (last lap, time left, fuel
  delta under a SC, pit stop time), the tyres as four coloured cells (temperature bands, wear bands on the pit
  page). White text only, on slightly deeper colours than the mock-ups so it reads.
- **Ferrari Mode** (1.2.25, with Immersive Pitstops 1.0.23+ and its Ferrari Mode option on, on the 2026 Ferrari):
  the dash plays along. K1 IS AVAILABLE every 20 s while the battery is at 80 % or more in a race; retirements
  read MUST BE THE (a part); the pit page's numbers are a little off (pit window late, rejoin place out, the wing
  call the wrong way round, another compound, a slower stop, shuffled wear, new numbers each lap); the engineer's
  BOX CONFIRMED reads WE ARE CHECKING. Without Immersive Pitstops, or with the option off, nothing changes.
- **Fastest lap:** the bottom row turns purple for 5 s with the driver and the time.
- **Across the bottom row** (blue, amber for incidents): BACKMARKER with the driver's code as you close on a lapped car (races), PIT WINDOW OPEN,
  RAIN IN n MIN when the forecast's chance passes 50 % in the dry, RETIRED with the driver's code when a car
  retires, CONTACT with the driver's code when you touch another car; FRONT WING / REAR WING DAMAGE as the damage rises
  10 points, TYRE WEAR with the corner as a tyre passes 75 % wear (when a puncture becomes
  possible); FL SLOW PUNCTURE (amber) when the game punctures a tyre and FL FULL PUNCTURE (red) when it bursts,
  from the game's own puncture state (launcher 1.10's tyre damage library, no UDP needed). With the game's UDP
  telemetry off, RETIRED comes from the launcher's race control events instead, and the practice / qualifying
  layout from the live session type.
- **New in 1.2** (from launcher 1.10, no UDP needed unless noted): YELLOW SECTOR 2 in amber as a sector's yellow
  comes out and SECTOR 2 CLEAR in blue as it ends; on the car page a punctured tyre turns amber as it deflates and
  red from halfway to a burst. Safety car screen: while a safety car or VSC is out (after its flag screen), the race
  page's top two rows become a yellow panel with SC / VSC DELTA, the gear over the speed and EST. PIT EXIT POS (the
  delta and pit exit position need UDP); the bottom row keeps the tyres, ERS SOC (with the lap under it) and fuel.
  SAFETY CAR IN THIS LAP / VSC ENDING shows across the bottom row as it ends. The car and pit pages keep the amber
  bar.
- **Undercut calls** (1.2.6; races from lap 2, not under a safety car): only around your own pit window (the
  game's, from 2 laps before its ideal lap to its latest: needs the game's UDP output) and against a car on the same
  number of stops as you. UNDERCUT CHANCE with the code of the car ahead (blue) as your window comes, when it has
  been within the gap (3 s by default, setting "Undercut gap") for 8 s: pit to undercut it. UNDERCUT THREAT with the
  code of the car behind (amber) when it pits from within the gap: pit to cover it. Then NOR OUT  P9  2.1s BEHIND
  as it leaves the pit lane. No overcut calls: whether staying out pays can't be judged from this data. The pit page
  shows the cars you'd rejoin between under REJOIN POSITION. Setting "Undercut alerts" turns them off.
- **Engineer radio tags** (1.2.3, launcher 1.10.20 or later, Steam): the race engineer's important calls as a
  short tag across the bottom row while he talks: BOX THIS LAP, NEW STRATEGY  DAMAGE, RAIN SOON, MGU FAULT,
  FLOOR DAMAGE  BOX and so on (blue strategy and weather, amber damage and faults, red a failure). The tag comes
  from the name of the game's voice line; what the dash already shows itself (punctures, flags, penalties, the
  pit window, wing damage) gets none. Setting "Engineer radio tags". A full radio page waits until the launcher
  lets an addon use the MFD's radio.
- **Catch / hold lap times** (1.2.7, races from lap 3, not under a safety car): once a lap as you cross the line,
  CATCH LEC  1:31.5 (blue: the lap time that catches the car ahead by the flag) or LEC IN 5 LAPS when you're
  already catching it, and HOLD NOR  1:32.1 (amber, first) when a quicker car behind would catch you before the
  flag. Cars within 10 s only. Every car's laps are timed from the live state, its pace the average of its last 3
  clean laps; the laps left come from the game's total laps (UDP). Stops still to come and tyre wear aren't in it.
  Setting "Catch / hold lap times".
- **Real-driver rev lights for created drivers** (1.2.9, setting on by default): a created career driver's steering
  wheel LEDs show the classic bar (the first five green RPM lights) where a real driver in the same car keeps the
  first group for single mode lights (straight mode / overtake). The game builds each car's LEDs from three group
  settings it takes from the driver's record, and a created driver has none (all 2). Driver Display copies the
  most common real-driver settings onto your car before each LED update (1.2.10 preferred the team-mate's,
  but in the Ferrari that gave flashing greens: 1.2.11). 1.2.12: only when the career's player driver is a
  created one (the launcher's career state says so); any car with your created style is skipped as a source, a real
  driver's lights (his own style included: Leclerc's greens flash briefly in the game) are never touched, and a car
  changed by mistake while a session loads gets its own settings back. **This part uses the dash's own
  offsets into the game** (the LED update's entries, from the launcher's reverse-engineering notes and a static
  disassembly of Steam 1.0.145): every read is checked, and anything unexpected after a game patch leaves the
  game's own lights (logged once, "rev lights: left to the game ...") without touching the rest of the dash.
- **FINAL LAP** over the screen in white (the white flag) as the last lap starts. The gap to the car ahead or behind blinks for 3 s
  as it comes within a second (from lap 2). Under the fuel margin: the margin per lap left (+ to spend, - to save).
- **Boot screen:** when the game hands you the car (the end of the intro, skipped or watched; else when the car
  first moves), your team's logo shows on black for 3 s, as the dash switching on. The logo is the game's own 2026
  team logo, asked for by its name so that a logo replaced by a mod (the launcher's importer) shows too; nothing
  of it ships with the addon.
- **2026 Ferrari wheel screen** (1.2.20): the game maps that car's wheel screen unevenly (its own display bows and
  loses its top too; the screen itself is flat in the model). Driver Display rewrites the texture coordinates of
  the screen part (102 vertices of the steering wheel model, three detail levels) so it maps straight, like every
  other car's: one `replace-resource` op on `ferrari.idf?lod=all&vb=9` in
  `2025_asset_groups/f1_2026_vehicle_package/teams/ferrari/wep/ferrari.erp`, built from the game's own buffer by
  `tools/ferrari_screen.py`. Nothing else on the car changes. The display fills that screen, so it looks a little
  taller there than on other cars (the screen is a taller shape). Don't install the separate test addon Ferrari
  Screen Fix as well: both replace the same buffer. **After a game update that changes the 2026 Ferrari's model**,
  the shipped buffer must be rebuilt (the build checks the screen is still the one it was measured on). Until
  1.2.19 every element had a copy bent against the screen; those are gone (387 elements instead of 476).
- Tyre temperatures and the fuel margin are white: the launcher (1.10) doesn't expose the game's own tyre windows.
- Lap time and delta hold still while the game pauses its lap packets (replays, flashbacks).
- The page button is a "UDP Action" bound in the game's Controls menu, on any device (wheel, controller,
  keyboard); UDP Action 1 by default, changeable in the addon's settings.
- Settings: what the delta compares with (previous or best lap), the flag screen, the tyre wear
  colour limits, the page button.
- Tyre temperatures and the fuel margin come from the game's packets, else the game's own LCD values.
- Brake bias, fuel mix, the differential, tyre wear and the game's last lap time come from the game's UDP
  telemetry packets ("-" while the game's UDP telemetry is off).
- The delta runs on the game's own lap clock from its lap packets (as Wheel Dash did): the clock rewinds with a
  flashback and stops in pauses and replays; the reference is your previous lap or your best one by the game's
  own times (invalid laps only until there is a valid one). Last lap is the game's own time. Without the packets the addon
  times laps itself (on the game's session time when the launcher has it, else frame time).
- Turn on **Debug log** in the addon's settings to write timing and telemetry values to `swofty/loader.log`
  every 10 s.

## How it works

- `tools/pkgfmt.py` reads and writes the game's `!pkg` UI format (layouts `.idf`, templates `.ui`). It
  round-trips all 8,472 UI files of the game byte for byte.
- `tools/erp.py` reads the game's `.erp` packages (zstd needs a 64-bit `libzstd.dll`; it finds the one GitHub
  Desktop ships, or set `ZSTD_DLL`).
- `tools/build_ui.py` starts from the game's own `lcd_display.idf` and `race_assets_f1.ui`, hides the stock
  elements, adds the dash's, and writes them to `ui/` with `erp/driver_display.json`, the launcher ops that swap them
  in.
- `native/src/driver_display.cpp` writes the values to `ui_menu_data.hud_rdash_*` leaves with the launcher's HUD API.
- `native/src/udp_tap.hpp` reads the game's UDP packets (below); `native/src/packet_values.hpp` holds what it
  decodes.

## Reading the game's UDP packets

The game hands each telemetry packet to ws2_32's `sendto` (or `WSASendTo`). At load, `native/src/udp_tap.hpp`
finds the game module's pointers to those two functions (by the functions' addresses, not by any game offset)
and swaps them for wrappers that decode the packet and then call what the pointer held before. The packet goes
out unchanged, and any other tap (the launcher's) still runs. The addon opens no socket and sends nothing of its
own.

Decoding follows EA's F1 25 UDP specification: car status (fuel mix, brake bias, fuel margin, ERS mode), car
damage (tyre wear, wings, floor), car setups (differential) and lap data (last lap time). The 2026 season pack
(packet format 2026) sends 24 cars; the per-car size is worked out from each packet's size.

## Build

```
python tools\build_ui.py "E:\Steam\steamapps\common\F1 25"
build.bat
package.bat
```

`package.bat` writes `dist\sedz.driver-display.swoftyaddon`.

## Format notes

- `!pkg`: `!idi` dicts (entries `{key string, value, fnv1a32(key)}` sorted by hash) and `!ili` lists, laid out
  breadth first. A value is a container offset (bit 31) or `(table << 24) | index` into typed tables (`!sbi`
  strings, `!vbi` ui32 / vec3 / vec4 / rgba / ...). vec3 and rgba tables keep duplicates; the others don't.
- Layout nodes: `name`, `elementId`, `typeName` (eText, eNull, ...), `position`, `scale`, `params` (style,
  boxWidth, halign, eLayer.layer, guid), `geoms`, `anims`, `children`. The scene's `nodeidlookup` is the sorted
  djb2 hashes of every node name; a top-level node's `elementId` is djb2 of its name.
- Template widgets: `[props, ..., [[{nodeId, debugName}, ...]], ...]` (slot 4: node paths); props hold `name`,
  `nameId` (djb2 of the name), `id`, `type` (UILB label, UIGP group), `text` / `visible` as `{value, dbentry}`
  and `colourOverride {value: rgba}`. A `dbentry` starting with `#` is an absolute view model path, e.g.
  `#ui_menu_data.hud_rdash_rpm`; others are the LCD's own data (`current_gear`, `front_left_tyre_temp_formatted`, ...). Check how the game itself binds a value before using it: `time_remaining` is a 0..1 animation fraction, and text bound to it crashed the game (0.1.0).
- Colour: the widget's `colourOverride`. Shapes are the stock white divider line (`divider_horizontal_4`,
  7.0 x 0.02) stretched by the node's scale; meshes lie in their x/z plane and the node's turn about x stands
  them up. Animation-clip colours are multiplied by the mesh's own colour, so they are not used.
