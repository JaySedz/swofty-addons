# Immersive Pitstops 0.1.2: what to test

All values were checked against the stock database (every op's row and field exists), and the package
builds. Nothing has been tried in game yet. Install `dist\sedz.immersive-pitstops.swoftyaddon`, let the
launcher rebuild, and send the launcher log if the build fails.

## Quick tests with Test mode

The **Test mode** setting forces every stop, for you and the AI, so you don't have to wait for luck. It
changes the database, so the launcher rebuilds after each change. Set it back to **Off** before racing.

| Test mode | Every stop | Checks |
|---|---|---|
| Fast stops (1.6 s) | 1.6 s, no mistakes | The 1.6 s animation in section 2 |
| Slow stops (3.0 s) | 3.0 s, no mistakes | That slow stops look normal |
| Big mistake every stop | A stuck wheel nut (the big mistake sliders, 30-60 s by default) | Whether the car stays held in the box (section 3) |

A short race (5 laps) in Driver Career with one stop per test mode is enough. Set the big mistake
sliders lower (for example 10-20 s) if you don't want to wait a minute per stop. If **Big mistake every
stop** gives normal stops, the game probably doesn't treat 100 % as "always"; tell me and I'll look at it.

## 1. Does the build apply?

- [ ] The launcher builds the profile without an error naming `sedz.immersive-pitstops`.
- [ ] Changing **Big mistakes** or the sliders triggers a rebuild that also passes.

## 2. Normal stops (Driver Career and My Team)

Do several stops (a long race with a 2-3 stop strategy, or a few short races).

- [ ] Your stationary times vary instead of always reading 2.4 s.
- [ ] Most land around 2.2-2.8 s, with an occasional sub-2.0 s stop.
- [ ] A fast stop (about 1.6-2.0 s) animates acceptably: crew in sync, no wheels popping on, no car
      released before the wheels are on. (Below the stock 2.1 s base the game switches to a faster crew
      sequence; this is the main visual risk.)
- [ ] A slow stop near 2.9-3.0 s looks normal.

Note your pit crew training level (My Team: facilities; Driver Career starts at 50 %), since it picks
the band. Driver Career at 50 % uses 1.80-2.85 s with a 10-14 % mistake chance.

## 3. Mistakes

- [ ] Small mistakes (0.3-3 s) show up now and then, with the crew radio line.
- [ ] A big mistake (stuck wheel nut, 30-60 s by default): **does the car stay held in the box** for the
      whole time, with the crew still working on the wheel, and then get released normally?
      Possible bad outcomes: the car is released early, the crew animation freezes or loops, the car is
      released with the wheel not on, or the stop never ends.
- [ ] How often big mistakes come up across the whole field. Rough estimate: at the 50 % band about
      1 stop in 70 has one on **Rare**, which is about one every other race across 20 cars. If that's too
      often, try fewer and tell me; if it's never, the mistake roll works differently from what's assumed.
- [ ] With **Big mistakes: Off**, no stop gains more than about 3 s from a mistake.

To force one for the box test, use **Test mode: Big mistake every stop** (above).

## 4. AI teams

- [ ] AI stationary times vary too (watch pit stop times in the race's stop notifications or the
      timing screens / broadcast graphics if they show them).
- [ ] An AI car that gets a big mistake loses the expected 30-60 s.
- [ ] If AI stops don't vary, turn on **Widen team pit times too**, rebuild and check again. That tells
      us which table the AI use. (That option also affects Grand Prix and other modes.)

## 5. Other modes

- [ ] Grand Prix with **Widen team pit times too** off: stops should look like stock (no mistakes).
      If they also vary, the training bands are read outside the careers as well; note it.

## What to send back

The launcher log if a build fails, and for each check: works / doesn't, plus any odd stop times or
animations you saw (a clip helps for the big mistake and the 1.6 s stop).
