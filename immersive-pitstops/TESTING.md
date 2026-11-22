# Immersive Pitstops 0.2.14: what to test

Install `dist\sedz.immersive-pitstops.swoftyaddon` and send the launcher log after each test.

## Where things stand

- [x] 0.2.0: the pit box hold works: held 68 s with a 60 s stuck wheel nut, then dropped and released
      normally (2026-11-22). The crew's small jump at the mistake is gone with it.
- [x] 0.2.2: retirement works: your car (26.1 s in the box) and AI cars retired when the crew finished, AI
      cars put in their garages; the game's race control logged them as retired (2026-11-22).
- But in that test three AI cars were retired after only ~10 s: they had the game's ordinary mistake in
  the front wing adjust version of the stop (not edited in 0.2.2), and 0.2.2 counted any stop over 8 s
  as a stuck nut. 0.2.3: the four front wing adjust "wheel on" mistakes get stuck nuts too, and a stuck
  nut is a stop that runs past the pit lane's own limit (~15.8 s), which no stock stop reaches.
- 0.2.3: **Random** is the default stuck nut time.
- [x] 0.2.3: the misfire is fixed: AI cars with ~10 s stops are released, only real stuck nuts retire.
- Still open: in Test mode some AI stops (~10 s) play a mistake version that isn't edited, so they never
  get a stuck nut. The game's own log channels can't be copied on this build (the launcher's channel table
  is for another build), so 0.2.4 logs the name of every crew sequence itself (`crew sequence: ...` lines,
  with **Log pit stops** on). One test with AI cars pitting names the version.
- The "go" radio call after a long stop is parked (not needed).
- 0.2.5-0.2.6 (stuck front wing) were dropped; 0.2.7 is the pre-wing code again.

## Test 0a: loose wheel and flashbacks (0.2.14)

0.2.13 result: with Terminal Damage off, the wheel came off when you stayed out [x] ("very aggressively").
A flashback right after took you back over the line and brought the wheel back; crossing again didn't take it
off, but the 20 s fallback still retired you. 0.2.14 watches your car's progress after the wheel came off:

- [ ] Flash back to before the line: "LOOSE WHEEL - The wheel still isn't on properly. Box this lap!", log
      `flashed back before the line ... loose again`. Cross the line again: the wheel comes off again. Pit
      instead: it's fixed.
- [ ] Flash back but stay past the line: log `flashed back past the line ...: the wheel comes off again`.
- [ ] The 20 s fallback starts again after a flashback, so time in the flashback screen doesn't retire you.

## Test 0b: loose wheel fixes (0.2.13)

0.2.12 result: the AI wheels came off [x] (cars 14, 16, 18, 5, 15, 2 on their out-laps; a red flag followed).
The red flag then moved cars into the pit lane, which read as your car crossing the line 30 ms later: your
wheel was torn off by the red flag, not by a missed stop. After Restart Session no popup came: the restart
fires no session start, so the loose wheel stayed "done". The safety car also got AI rolls. 0.2.13:

- [ ] A red flag fixes every loose wheel still on its car ("RED FLAG - The crew fix the loose wheel", log
      `red flag; car N's wheel is fixed`), AI ones too.
- [ ] A line crossing only counts if no red flag follows within 1.5 s.
- [ ] After Restart Session the loose wheel rolls again at your next stop (log `was rebuilt (a restart)`).
- [ ] No `AI car 22` (the safety car) loose wheel lines.
- [ ] Your own wheel coming off when you really miss the stop (AI chance at 1 %).

## Test 0: loose wheel, the wheel coming off (0.2.11)

0.2.10 result: the message, the fix when you pit and the 20 s retirement all worked [x], but no wheel came
off. The game never counted a hit (kerbs aren't hits; Terminal Damage's suspension log: "hits 0"), and your
car's suspension damage scales are 0. 0.2.11 tears the wheel off the way Terminal Damage does: the game's own
damage for that corner set just past its detach limit.

Settings: **Test mode: Off**, **Loose wheel chance: 100 %**, **AI loose wheel chance: 100 %**, **Log
pit stops: On**. A race; do a normal stop and stay out.

- [ ] Crossing the line: "You didn't box: the wheel has come off", and a wheel really comes off (the rear
      left, rear right, front left or front right, picked at the stop). The game should end your race
      itself within a second or two. Log: `stayed out; its wheel comes off`, `car N's <corner> wheel comes off
      (damage ... -> ..., detach limit ...)`, then `is out of the race (status ...)`.
- [ ] If instead the log says `suspension damage isn't where expected` or `detach limit reads ...`, the lookup
      failed on this build: you're retired after 20 s as before.
- [ ] AI cars: `AI car N: loose wheel roll ... will come off on the out-lap`, and a little later that car loses
      its wheel on track (the log's `car N's ... wheel comes off`). At 5 % a race with many stops should show
      one; it may take a couple.
- [ ] With Terminal Damage's suspension damage on, note whether it reacts (it may log the wheel loss too).

## Test 1: a retirement (0.2.3 recheck)

Settings: **Test mode: Stuck wheel nut every stop**, **Stuck wheel nut time: 20 s**, **Retirement chance:
100 %**, **Log pit stops (testing): On**.

- [ ] AI stops are now about as long as yours (~26 s), not ~10 s, before they retire.
- [x] The stuck wheel nut plays out in full (about 26 s), then instead of being released your car is
      retired: a RETIRED notification, the game's retirement (cutscene or results; Skip Cutscenes may skip
      it), and your race ends.
- [ ] The car stays in its box while that happens; it isn't released or driven out.
- [ ] AI cars that pit are retired too and disappear into their garages about 5 s later. Their team-mates
      can still use the box afterwards.
- [ ] No crash, and the session ends normally (results show you as retired / DNF).

Log lines, starting `[immersive-pitstops]`:

- at load: `retirement: status request found (bus post bound)`. `retirement off: ... hits` means the
  pattern isn't on this build: no retirements, the rest works.
- `stuck wheel nut (15.3 s in the box); retirement roll N against 100 %: the car retires when the crew finish`
- `pit status 4 -> 5`, then `the team gives up on the stuck wheel nut (the crew finished, ... s in the box): retired`
- for AI cars: `retired car put in its garage`

## Test 2: a fix

Same, with **Retirement chance: 0 %**: every stuck wheel nut ends with the car released as in 0.2.0, and the
log says `it gets fixed`.

## Test 3: Random stuck nut time

**Stuck wheel nut time: Random**, **Test mode: Stuck wheel nut every stop**, **Retirement chance: 0 %**.
In this mode every wheel mistake plays its own wheel's stuck nut, so a few stops show different lengths.

- [ ] Stuck nuts on different wheels, about 5 s (front left), 15 s (front right), 30 s (rear left) and
      60 s (rear right) longer than a normal stop.
- [ ] Only that wheel's gun man and tyre-on man struggle; the rest hold still; the engineer names the
      right wheel.

## Then

- **Retirement chance** back to 25 % and **Test mode** Off for a normal race.
- Normal stops: stationary times vary instead of 2.4 s; AI times vary too.
- **Test mode: Fast stops (1.6 s)** and **Slow stops (3.0 s)** look acceptable.

## Not in this version

- The crew pushing your retired car into the garage (Terminal Damage's way): your car stays in its box.

## Earlier results

- 0.1.2-0.1.3: the database time loss of a mistake does nothing; mistakes are crew sequences in
  `pitstop_24/25/26.erp`.
- 0.1.4-0.1.6: a longer sequence plays; only the rear right crew loop, the rest hold still in a gap
  between keys; the engineer line is right in Test mode.
- 0.1.6-0.1.8: released 20.7 s after stopping whatever the length (failsafe at 15.77 s + the 5 s release
  wait); not the database's 10 or 7.5.
- 0.2.0: the native hold keeps the failsafe off; works.
