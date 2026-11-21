# Immersive Pitstops

Career pit stops in F1 25 come out at almost exactly the same time every lap: a clean stop with the stock
crew is 2.4 s, every time. This addon spreads them out. Most stops land between about 2.2 and 2.8 s, a good
crew now and then turns one in 1.6 to 2.0 s, small fumbles happen more often, and once in a while a wheel
nut sticks and the car sits in the box for 30 s to a minute.

It changes only game database values: no native code, no game offsets.

## What it changes

Pit crew training bands (`pit_crew_training.probabilities`), F1 set. The game picks a band by the team's
pit crew training level, for your team and for the AI teams:

| Training level | Stock stop | New stop | Stock mistake chance | New mistake chance |
|---|---|---|---|---|
| up to 20 % | 2.50-2.54 s | 2.00-2.95 s | 21-30 % | 18-25 % |
| up to 40 % | 2.45-2.50 s | 1.90-2.90 s | 11-20 % | 14-18 % |
| up to 60 % | 2.40-2.45 s | 1.80-2.85 s | 6-10 % | 10-14 % |
| up to 80 % | 2.30-2.40 s | 1.70-2.80 s | 1-5 % | 7-10 % |
| up to 100 % | 2.24-2.30 s | 1.60-2.75 s | 0 % | 4-7 % |
| equal performance | 2.24 s | 1.80-2.85 s | 0 % | 8-10 % |

Crew mistakes (`pit_crew_training.mistakes`):

| Mistake | Stock | New |
|---|---|---|
| Slow wheel removal (4) | 0.5-1 s | 0.3-1.5 s |
| Slow wheel attachment (front right, rear left) | 0.5-2 s | 0.5-3 s |
| Slow wheel attachment, rear right | 0.5-2 s | **stuck wheel nut**: 30-60 s (Big mistakes: Rare or Often) |
| Slow wheel attachment, front left | 0.5-2 s | 0.5-3 s, or **stuck wheel nut** with Big mistakes: Often |
| Front wing removal / attachment | 1-2 s / 1-3 s | unchanged |

The F2 bands are unchanged. Mistakes stay switched on only in My Team and Driver Career, as in the stock game.

## Settings

- **Big mistakes**: Off, Rare (default) or Often. A crew mistake picks one of eight wheel mistakes; Rare makes
  one of them a stuck wheel nut, Often makes two.
- **Big mistake, shortest / longest (s)**: how much time a stuck wheel nut adds, 30-60 s by default.
- **Widen team pit times too**: also widens each team's range in `pit_lane.pit_times`. That table is read
  outside the careers too, so Grand Prix changes as well. Off by default.

- **Test mode**: for testing only. Forces every stop to 1.6 s, to 3.0 s, or to have a stuck wheel nut.
  Off by default.

Changing a setting needs a rebuild, which the launcher does on the next launch.

## Limits

- The stop schedule is rolled when a session loads, so a change applies from the next session.
- How often big mistakes come up depends on the game's mistake roll, which is not fully known yet; see
  [TESTING.md](TESTING.md).
