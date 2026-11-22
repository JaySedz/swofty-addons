# Immersive Pitstops 0.2.1: what to test

Install `dist\sedz.immersive-pitstops.swoftyaddon` and send the launcher log after each test. The
retirement in 0.2.1 hasn't been tried in game yet.

## Where things stand

- [x] 0.2.0: the pit box hold works: held 68 s with a 60 s stuck wheel nut, then dropped and released
      normally (2026-11-22). The crew's small jump at the mistake is gone with it.
- The "go" radio call after a long stop is parked (not needed).

## Test 1: a retirement

Settings: **Test mode: Stuck wheel nut every stop**, **Stuck wheel nut time: 30 s**, **Retirement chance:
100 %**, **Give up after: 15 s**, **Log pit stops (testing): On**.

- [ ] About 15 s into the stop your car is retired: a RETIRED notification, the game's retirement
      (cutscene or results; Skip Cutscenes may skip it), and your race ends.
- [ ] The car stays in its box while that happens; it isn't released or driven out.
- [ ] AI cars that pit are retired too and disappear into their garages about 5 s later. Their team-mates
      can still use the box afterwards.
- [ ] No crash, and the session ends normally (results show you as retired / DNF).

Log lines, starting `[immersive-pitstops]`:

- at load: `retirement: status request found (bus post bound)`. `retirement off: ... hits` means the
  pattern isn't on this build: no retirements, the rest works.
- `stuck wheel nut (8.0 s in the box); retirement roll N against 100 %: the car will retire`
- `the team gives up on the stuck wheel nut (in the box, 15.0 s in the box): retired`
- for AI cars: `retired car put in its garage`

## Test 2: a fix

Same, with **Retirement chance: 0 %**: every stuck wheel nut ends with the car released as in 0.2.0, and the
log says `it gets fixed`.

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
