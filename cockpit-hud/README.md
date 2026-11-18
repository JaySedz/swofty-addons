# Cockpit HUD

A full HUD on the F1 25 steering wheel display, built to replace the game's on-screen HUD (for VR, or with the
game's HUD turned off). Its values come from the launcher's live car state (`swofty::live`), the launcher's
telemetry API, the game's own LCD data, and the game's UDP telemetry packets.

**About the UDP packets:** the addon reads the telemetry packets the game itself sends by wrapping the game's
calls to Windows' `sendto`. **It passes the game's own packets through unchanged and opens no sockets**: it
sends and receives nothing of its own, so SimHub and other telemetry apps keep receiving everything. The source
is linked in the addon's repository field. See "Reading the game's UDP packets" below.

**Status: 1.0.0.** Turn off Wheel Dash (`ross.wheel-dash`) and Wheel OSD (`ashton.wheel-osd`) while using it:
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

**Car page:** a top-down car drawn from its own textures (art/car), each part coloured by its damage (front
wings, rear wing, sidepods, floor and bargeboards, diffuser, engine cover, gearbox, brakes: green / amber / red,
grey until known) and the tyres by their wear; chassis, cockpit and suspension grey. Brake temperatures either
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
- **Pit limiter:** the whole screen (PIT LIMITER with your speed) while it is on, coloured by the flag out: blue
  normally, yellow under a yellow, amber under a safety car / VSC, red under a red flag.
- **Penalties** (time penalty, drive through, stop-go, disqualification): white over the whole screen for 4 s
  with the reason. **Warnings** (track limits, a collision): white on black for 3 s, the black and white flag. An
  invalidated lap pops up red in the ERS SOC cell.
- **DRS / overtake enabled and disabled** (with the reason: wet track, safety car, red flag, too early): the whole
  screen in blue (the game's overtake colour) for 3 s, as the game announces them at the top of its HUD.
- **Chequered flag:** chequers over the screen, 4 s when it comes out, 8 s when you finish (FINISHED P n).
- **Setting changes** (ERS mode, fuel mode, brake bias, differential) pop up in the ERS SOC cell for 2 s.
- **Boot screen:** when the game hands you the car (the end of the intro, skipped or watched; else when the car
  first moves), your team's logo shows on black for 3 s, as the dash switching on. The logo is the game's own 2026
  team logo, asked for by its name so that a logo replaced by a mod (the launcher's importer) shows too; nothing
  of it ships with the addon.
- **2026 Ferrari wheel:** that car's steering wheel bends its screen (the game's own display bends too). On it the
  HUD's text and lines are drawn from copies placed against the measured bend so they show straight ("Ferrari
  wheel correction" setting, on by default; tools/ferrari_2026_warp.json). Other cars are not affected.
- Tyre temperatures and the fuel margin are white for now: the game's own tyre windows aren't readable yet (a
  table of compound windows is in the code, `tyre_window`, for when the launcher exposes them).
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
  elements, adds the dash's, and writes them to `ui/` with `erp/cockpit_hud.json`, the launcher ops that swap them
  in.
- `native/src/cockpit_hud.cpp` writes the values to `ui_menu_data.hud_rdash_*` leaves with the launcher's HUD API.
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

`package.bat` writes `dist\sedz.cockpit-hud.swoftyaddon`.

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
