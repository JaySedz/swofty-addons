# Helmet Cam

The cockpit camera as seen from inside a helmet, for F1 25 on the Swofty Launcher.

- **Helmet edge**: the eye port darkens towards its corners and bottom.
- **Brow padding**: the helmet's padding across the top of the view, curving down at the sides.
- **Visor**: clear, light smoke, dark smoke or iridium tint, with optional faint smudges.
- **Visor strip**: your team's strip across the top of the visor. It changes with the car you're in, and it's mirrored by default because you see it from inside.
- **Head movement**: braking dips the view and acceleration lifts it (**Head movement: braking**); cornering rolls it and shifts it towards the corner (**Head movement: cornering**). A damped spring smooths the movement, like a head on a neck.
- **Head turn into corners**: turns the game's own cockpit camera a little towards the apex while you're under sustained cornering load (over about 1 g). It writes to the game's free-look camera between its updates. Lane changes and small corrections don't move it, and your own look buttons always win.
- **Road vibration**: a fine shake that grows with speed.
- **Aston Martin Mode** (off by default): in the 2026 Aston Martin the road vibration runs at 300 %, and the Road vibration slider can't turn it down. Every other car, the 2025 Aston Martin included, keeps your slider.
- **Visor rain**: in the wet, drops bead on the visor when you're slow and are blown off to the sides at speed, stretching into streaks. The middle of the visor clears and a light spray mist builds. Each drop bends the view behind it like a small lens. It follows the game's weather (through the game's UDP telemetry); **Always** shows it in any weather.
- **Camera height and side** (optional): moves every car's cockpit camera down by up to 4.5 cm, or 2 cm to one side, from the game's own eye position.

The helmet stays fixed while the world moves, as it does for a camera inside the helmet. Only the cockpit camera changes, and the HUD draws on top, untouched.

## How it works

- The look is a post-process pass (`fx/visor.hlsl`). Two tiny passes before it (`fx/state.hlsl`) decide how much of the helmet shows. It's on in the cockpit view, and off at once in other views, while paused, in replays, photo mode and cutscenes. When the launcher briefly can't tell the view, as at lights out, the helmet holds for up to 0.35 s and then fades out, so it never flashes.
- The head sway comes from the camera car's g-forces in the launcher's live state (no UDP telemetry needed). The addon's code sends them to the pass every frame.
- The strip comes from the car's team badge, which picks a row of `tex/strips.png`.
- Camera height and side are `patch-pkg` `set-value` edits of each team's cockpit camera mount (`<team>-camscargame.layer`, entry `CameraAttachmentPointCockpit`). Each car keeps its own seat position and moves only by the amount you pick.

The addon doesn't launch programs or use the network.

**Camera probe (0.1.2 test build):** to prepare real head movement, the addon wraps the game's free-look camera update (the launcher's `Game_FreeLookCameraModifier_Update` binding). The game's update runs first, unchanged, and the addon only counts the calls. It reads a few of the modifier's values with guarded reads and logs them. It writes nothing. Where the binding doesn't resolve, nothing is wrapped. **Camera probe** in the settings turns the logging off.

## Fallback

- **Addon off**: nothing changes.
- **Camera height and side at "Game's own"**: no archive is touched.
- **No live state** (an older core, an unverified game build): the view stays still and shows the generic strip.
- **Core can't detect the camera view**: the pass doesn't run.

## Compatibility

Driver Display only changes the HUD's UI files, and this addon only changes the picture and the camera mount, so they work together. The wheel screen is part of the 3D picture, so it moves with the head sway as the rest of the car does.

With Frame Generation on, the moving picture can smear a little in generated frames, because the game's motion vectors don't know about the sway. Turn the sway down if it bothers you.

## Your own visor strips

`tex/strips.png` is 1024 × 1024, with 16 rows of 64 px:

| Row | Team |
|---|---|
| 0 | Generic (any other car) |
| 1 | Ferrari |
| 2 | Mercedes |
| 3 | Red Bull |
| 4 | McLaren |
| 5 | Aston Martin |
| 6 | Alpine |
| 7 | Williams |
| 8 | Haas |
| 9 | Audi / Sauber |
| 10 | Racing Bulls |
| 11 | Cadillac |
| 12 | Konnersport |
| 13–15 | Spare |

To use your own art, paint over a row. Transparent pixels show no strip. Then rebuild the package. `tools/make_strips.py` rewrites the placeholders.

## Building

- `build.bat` builds `native/helmet_cam.dll` (MSVC).
- `tools/gen_mount.py "<F1 25 folder>"` regenerates `erp/mount.json` from an unmodified install.
- `package.bat` writes `dist/sedz.helmet-cam.swoftyaddon`.
- `swofty postfx compile .` checks the shader.
