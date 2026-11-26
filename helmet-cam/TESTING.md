# Helmet Cam: test plan (0.1.11)

Turn on **Debug log** in the addon settings for the first run. Lines start with `[helmet-cam]` in `swofty/loader.log`.

## 1. Loads and runs

- [ ] The log shows `loaded 0.1.11: live state ..., postfx data available, frame tick on`.
- [ ] Before the session, the log shows a `[postfx] Helmet Cam's pass visor is ready` line.
- [ ] On track in cockpit view, the log shows `postfx data: the visor pass reads the head and strip data`.
- [ ] In cockpit view you see the helmet edges, the brow padding, the tint and the strip.
- [ ] In T-cam, chase and TV cameras, the picture is the game's own.
- [ ] While paused, the picture is the game's own (the pass doesn't run).
- [ ] In replays and photo mode, the picture is the game's own.
- [ ] Turning **Helmet view** off in ADDON SETTINGS in game gives the game's own picture straight away.

## 2. Team strip

- [ ] The log shows `camera car N, team badge "...": strip row R`. **Send me the badge text** for each team you try: the row mapping depends on these names.
- [ ] Mercedes shows MERCEDES and Ferrari shows FERRARI (mirrored while **Strip seen from inside** is on).
- [ ] An F2 car or a team without a row shows the plain dark strip.

## 3. Head sway: directions (the main thing to check)

With Debug log on, the log prints `g long .. lat .. steer ..` once a second.

- [ ] Hard braking: the view dips (the world moves up a little). If it lifts instead, the g sign is reversed.
- [ ] Accelerating: the view lifts slightly.
- [ ] A left-hander: note which way the horizon rolls and which way the picture shifts. It should lean into the corner and look a little towards the apex.
- [ ] No black or stretched edge shows at the screen borders at **Head sway** 100.
- [ ] Wheel screen (Driver Display): it moves with the car, and nothing looks detached.

## 4. Camera height and side (needs a relaunch)

- [ ] **Camera height** "3 cm lower": the build log shows no errors for `erp/mount.json`, and the cockpit camera sits lower.
- [ ] If the camera doesn't move, tell me: the game may place the cockpit camera from the template rather than this mount.
- [ ] **Camera side** "2 cm left": check it moves to the left. If it goes right, the side is reversed.
- [ ] Back to "Game's own" / "Centre": the camera is the game's own again.

## 5. Performance

- [ ] The log never shows `pass visor is off: it took ... over its 1.50 ms budget`.

## 6. Camera probe (0.1.2, for stage 2)

**Camera probe** is on by default and changes nothing in game. With **Debug log** on as well, every line has the raw values.

- [ ] At load, the log shows `probe: Game_FreeLookCameraModifier_Update resolves` (or "doesn't resolve": then stage 2 needs another route).
- [ ] Drive a lap in cockpit view without touching the look buttons. Do `probe: N call(s)/s` lines appear, and is N about the frame rate?
- [ ] Hold look left, then look right (stick or buttons). Which values change (the `changed` stars, yaw / pitch / off)?
- [ ] Switch to T-cam and chase cam. Do new modifiers appear (`probe: a free-look modifier at ... (new)`)?
- [ ] Send the log.

## 7. Real head turn (0.1.5)

**Head turn into corners** (default 40, 0 = off). With **Debug log** on, the log shows `head turn: lat .. smoothed .. -> target ..` once a second.

- [ ] In a long corner, the head settles a little towards the apex and comes back smoothly on the exit.
- [ ] Lane changes, weaving, and kerbs on a straight don't move the head.
- [ ] At 0, the camera is the game's own (the log says `head turn: off`).
- [ ] Your own look buttons still work.

## 8. No flashing (0.1.6)

- [ ] At lights out, the helmet stays on (0.1.5 blinked off for a moment).
- [ ] Opening an in-game menu: the helmet stays up to a third of a second, then fades out smoothly. No on/off flicker.
- [ ] Back to driving from a menu: the helmet fades in over about 0.15 s.
- [ ] Pause, T-cam, chase cam, replays and photo mode: the helmet goes off straight away.
- [ ] Any flashing left: note the time. The launcher still drops a few frames once a minute (in its own stats logging), and Helmet Cam can't cover those.

## 9. Visor rain (0.1.8)

**Visor rain** is Auto (from the weather) by default; **Always** shows it in any weather. Auto needs the game's UDP telemetry on.

- [ ] Set it to Always and drive. Parked or slow: drops bead on the visor and creep down.
- [ ] At mid speed, drops stretch and drift out to the sides.
- [ ] Flat out, the middle of the visor stays fairly clear, with thin streaks blown off the sides and a light mist.
- [ ] In a wet race on Auto, the log shows `visor rain: weather 3/4/5 (rain)`, and the drops fade in over a few seconds.
- [ ] Optional: to remove the game's own vertical drips, close the game and set `rainBeads="false"` in `Documents\My Games\F1 25\hardwaresettings\hardware_settings_config.xml`. Note what else it removes (water beads on the bodywork?).
- [ ] The log never shows `pass visor is off: ... over its 1.50 ms budget` in the rain.

## 10. Aston Martin Mode (0.1.11)

- [ ] With **Aston Martin Mode** on, in the 2026 Aston Martin: the log shows `Aston Martin Mode: on, road vibration 300 %`, and the shake is clearly stronger than at Road vibration 100.
- [ ] Moving the Road vibration slider (even to 0) doesn't change the shake in the Aston.
- [ ] In any other car (and the 2025 Aston Martin), the slider works as before.
- [ ] With the mode off, the Aston uses the slider.
