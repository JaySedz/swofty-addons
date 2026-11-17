# Race Dash

A steering wheel LCD for F1 25 laid out like a real F1 dash. Its values come from the launcher's live car
state (`swofty::live`), the launcher's telemetry API, the game's own LCD data, and the game's UDP telemetry
packets.

**About the UDP packets:** the addon reads the telemetry packets the game itself sends by wrapping the game's
calls to Windows' `sendto`. **It passes the game's own packets through unchanged and opens no sockets**: it
sends and receives nothing of its own, so SimHub and other telemetry apps keep receiving everything. The source
is linked in the addon's repository field. See "Reading the game's UDP packets" below.

**Status: 0.2.1, testing before 1.0.** Turn off Wheel Dash (`ross.wheel-dash`) while using it: both replace the
same two UI files. Turn the game's UDP telemetry on (Settings > Telemetry) for brake bias, fuel mix, the
differential, tyre wear and the game's exact lap times.

## The screen

| | | | | |
|---|---|---|---|---|
| **Last lap** (green) | | **RPM** (red) | **Delta** to your best lap (green faster, red slower) | |
| SPEED | TYRE FL (temp, wear) | **GEAR** | TYRE FR | POS |
| LAP | TYRE RL | | TYRE RR | FUEL (laps margin) |
| SOC % (red under 15) | MIX | ERS MODE | BBAL | GAP (ahead / behind) |
| **SOC bar**: 20 segments of 5 %, the first 3 red | | | | |

- The bottom centre panel pops up a box in place: blue for 2 s when the ERS mode changes (and fuel mix, brake
  bias or the differential, when those are known), amber under a safety car / VSC / formation lap, red under a
  red flag.
- Gear, tyre temperatures and fuel are the game's own LCD values.
- Brake bias, fuel mix, the differential, tyre wear and the game's last lap time come from the game's UDP
  telemetry packets ("-" while the game's UDP telemetry is off).
- Lap times are timed by the addon: on the game's session time when the launcher has it (it stops in pauses and
  replays and goes back with a flashback), else frame time with pauses, a frozen car (replays) and flashbacks
  taken out. A race's opening lap counts from leaving the grid.
- Turn on **Debug log** in the addon's settings to write timing and telemetry values to `swofty/loader.log`
  every 10 s.

## How it works

- `tools/pkgfmt.py` reads and writes the game's `!pkg` UI format (layouts `.idf`, templates `.ui`). It
  round-trips all 8,472 UI files of the game byte for byte.
- `tools/erp.py` reads the game's `.erp` packages (zstd needs a 64-bit `libzstd.dll`; it finds the one GitHub
  Desktop ships, or set `ZSTD_DLL`).
- `tools/build_ui.py` starts from the game's own `lcd_display.idf` and `race_assets_f1.ui`, hides the stock
  elements, adds the dash's, and writes them to `ui/` with `erp/race_dash.json`, the launcher ops that swap them
  in.
- `native/src/race_dash.cpp` writes the values to `ui_menu_data.hud_rdash_*` leaves with the launcher's HUD API.
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

`package.bat` writes `dist\sedz.race-dash.swoftyaddon`.

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
