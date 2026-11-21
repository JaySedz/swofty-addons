# Immersive Pitstops: native plugin plan

Status: research only, nothing built. On hold until Swofty launcher 1.10, then check what it exposes
(penalties especially). Own offsets are acceptable for this addon if the launcher doesn't cover something,
as with Terminal Damage, accepting that a game update may break it. The database part (0.1.x) stays as it is: stop times, small mistakes
and the stuck wheel nut. This plan covers what a native plugin could add on top, using only the launcher's
APIs and bindings (no addresses, byte patterns or struct offsets of our own) and no process launch.

## What the launcher gives us (checked in the SDK headers, launcher repo as of 2026-11-19)

| Need | Launcher piece | State |
|---|---|---|
| Know when a car enters its box, is released, leaves | `session.pit` event (previous and new pit status, 0-16) | Shipped, core hook on `Game_Pitlane_SetVehicleStatus` |
| Know a stop was counted | `session.pit_stop` event | Shipped |
| A car's lap | `session.lap` event | Shipped |
| Make a car's wheel come off easily | `cars::live::Car::set("f1_suspension_damage.*")`: `speed_threshold`, `wheel_detach_scale`, `front/rear_damage_scale` per car, live | Shipped; Terminal Damage's author checked `wheel_detach_scale` in game |
| Weaken a car without detaching anything | `engine.power_scale`, `ai.grip_scale` per car | Shipped |
| End a car's race | `sessions::disqualify(car)` (shows as DNF in the final results, may show DSQ during the race) | Shipped, needs core session ops v2 |
| Hand a car to the AI | `sessions::set_control` | Shipped, "waiting on verification" |
| Time / drive-through / stop-go penalty | `sessions::issue_penalty` | **Designed, not in the headers yet.** The raw binding `Sess_Issue_TimePenalty(rules, incident, reason, seconds)` exists, but it needs an incident record we'd lay out ourselves (car at +0, lap at +9), which breaks the update-safe rule |
| Send a car into its box / garage | `Game_PitLane_PutVehicleInPitBox`, `Game_Pitlane_SetVehicleStatus` | Bindings, L2 experimental: teleports, no std operation |
| Stretch one car's stop at runtime | `Sess_PitStopUpdateJob_Update` | Binding exists, but the core can't map a stop to its car yet ("needs PitStopInstance.car") |
| Tell the player | HUD / popup API (as in Driver Display) | Shipped |

## What Terminal Damage taught us about the garage

`nick.terminal-damage-probe` (source: github.com/nyannoying1337/f1-25-terminal-damage, read-only, no code
copied; its licence needs attribution) retires the player's car in the box and gets it pushed into the garage. It does it with its own
byte-pattern scans and offsets, so none of it can be reused under our rules, but it shows how the pit lane
works:

- Pit statuses: 1 OnTrack, 2 EnteringPitLane, 3 EnteringPitBox, 4 InPitBox, 5 InPitBox_CrewCompleted,
  6 InPitBoxPerformedRelease, 7 LeavingPitBox, 8 BeingPushedIntoGarage, 9 InGarage, 15 DrivingThrough.
  These are the same numbers `session.pit` reports, so we can follow every car's stop with no offsets.
- In a race, entering the pit lane makes the pit crew prepare a tyre stop and ignore a push-into-garage
  request. In practice and qualifying the game asks the crew to push the car into the garage. To retire in
  the box, Terminal Damage fakes the crew messages (request 6/7 and crew reports 3/4), sets the crew's
  "ready" byte, and hides the pit stop equipment models so the car can be pushed through. That's the part
  we can't do without our own offsets.
- A retired car is handed to the AI with reason RETIRED or TERMINAL_DAMAGE, and the result gets status 7.

## Features, by what's possible

### 1. Loose wheel after a stop: possible now

Real case: Albon, Imola 2024, released with a loose wheel; Norris at Monza 2025, where the nut wouldn't tighten.

- On `session.pit` 6/7 (release), roll a chance for the car (an option, e.g. 1 in 150 stops). Raise the
  chance when the stop ran long: we time 4 → 6 ourselves, so a stop well over its normal time (likely a
  stuck nut from the database part) is more likely to leave the wheel loose.
- Effect: write that car's `f1_suspension_damage.speed_threshold` and `wheel_detach_scale` very low, so the
  wheel comes off on the next kerb or bump. The game's own wheel-off damage and retirement then take over,
  with its own cutscene for the player.
- The player gets a HUD warning ("Loose wheel, box now"). Pitting again (`session.pit_stop`) puts the
  values back: an extra stop, as in real life.
- AI cars don't know to pit. Two options for them: let the physics decide (the wheel may come off), or after
  a set distance end their race with `sessions::disqualify` (shows as DNF in the final results).
- To test: how low the values need to be for the wheel to actually come off, and whether a write survives
  until the next stop (writes last until the game rebuilds or resets the car's parts).

### 2. Car stops in the box for good (Bottas, Monaco 2021): partly possible

- The database part can already hold a car for minutes with a long stuck-nut range.
- Ending the race there: on a stop past a limit, `sessions::disqualify(car)`. Getting the car pushed into
  the garage like Terminal Damage does needs offsets we can't use; `Game_Pitlane_SetVehicleStatus(…, 9)`
  might teleport it there, and needs testing.

### 3. Unsafe release penalty: wait for the launcher

- Detection with what we have: at a car's release, check whether another car is in status 2-3
  (entering) or 7 (leaving) at the same time, then roll. We have no pit lane positions, so it's an
  approximation.
- Penalty: real F1 gives a time penalty (Albon got a 10 s stop-and-go for releasing with a loose wheel).
  This needs `sessions::issue_penalty`, which the launcher has designed but not shipped. Worth asking
  Swofty for it (issue #691 asks for retire/disqualify helpers in the same area).

### 4. Not possible for now

- Wrong or mixed tyres fitted, a crew that isn't ready, a dropped jack: no game data or API for these.
- Changing one car's stop length at runtime: the binding exists, but the stop-to-car mapping doesn't.
- Making an AI car pit for a loose wheel: no AI pit-decision API.

## Suggested order

1. Loose wheel (feature 1) with the HUD warning and a "what happens to AI" option.
2. Race-ending stop (feature 2) through `disqualify`, behind an option, after the 0.1.x database test shows
   how long stops behave.
3. Unsafe release penalties once `sessions::issue_penalty` ships.

The plugin makes the addon `kind: ["database", "native"]`, and the reviewer screens native code more
closely than database-only addons (Driver Display's plugin was still auto-approved).
