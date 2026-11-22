# Immersive Pitstops 0.2.4: what to test

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
