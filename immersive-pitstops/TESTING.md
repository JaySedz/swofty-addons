# Immersive Pitstops 1.0.20: what to test

1.0.19 result: level 3 in a long race is only a warning. 1.0.20 puts long races back to level 4 (10 s, added to the
race time), short races stay level 4 (3 s). Nothing else changed.

## 1.0.19

Pit exit line: a long race gave 10 s at level 4 (the 3 s was a 5-lap race, a short race). 1.0.19 sets long races to
level 3 (expected 5 s) and keeps short races at level 4 (3 s). Check a long race: the first offence should be 5 s.

## 1.0.18

AI loose wheel chance now uses the same slider as yours (0-10 % in 0.1 steps, default 0.3, was 1 %). The "Loose wheel
every stop (testing)" toggle moved to the bottom, above Log pit stops. Check the settings page order and values.

## 1.0.17

1.0.16 result: the call was kept through 15 flashbacks with no false CREW NOT READY [x]; the spread ran from x0.82 to
x1.29 [x]. A loose wheel (roll 0 against 1 %) and then a stuck nut on the stop that fixed it: natural rolls, but too
often for the real thing. 1.0.17: stuck wheel nut 1 % by default; lengths tilted short (5 s 60 %, 10 s 25 %, 20 s
10 %, 30 s 4 %, 60 s 1 %); loose wheel 0.3 % by default, slider 0-10 % in 0.1 steps (the launcher allows at most 100
steps); new toggle "Loose wheel every stop (testing)". Check: the sliders show 1 and 0.3; the load line reads
`loose wheel 0.3 % of your stops; stuck wheel nut 1 %`; with the testing toggle on, a stop leaves a loose wheel.

## 1.0.16

1.0.15 result: More fast stops worked (about 2 s, a few under 2 s) [x]. False CREW NOT READY after called stops: the
call was used up as the crew finished, so after a flashback to before the stop, the repeated stop counted as uncalled.
Also, an alert still queued from an earlier stop could show after a later one. 1.0.16 keeps a done call until you've
been back on track for 30 s, and drops queued alerts when a new pit lane approach starts. Check: call a stop, flash
back to before the pit entry, pit again: no CREW NOT READY (log: `the called stop is done (the call is kept...`, then
`30 s back on track: the call is used up`). An uncalled stop later still gets the alert.

## 1.0.10

1.0.9 result: the engineer's pit window call readied the crew [x]; a plugin stuck nut (rear right, 5 s, cut exactly)
[x]. Its STUCK WHEEL NUT alert and the earlier CREW NOT READY were shown in the pit lane and not seen. 1.0.10 shows
your stop's alerts once you're released (as the loose wheel's). Check both appear after the stop.

## 1.0.9

1.0.7 log: the engineer's damage call counted [x]. The safety car (car 22) parks in "garage 99" with no live garage:
1.0.8's stricter check would have switched the box hold off for the session when it pulled in. 1.0.9 skips the
safety car. Check a race: no `off for this session` line, box hold and stuck nuts as before.

## 1.0.8

1.0.8 (fallbacks for other game copies, EA app): the box hold no longer writes when the launcher can't report the
car's garage (it used to fall back to "the values look sane"); stuck wheel nuts stay off while the box hold can't run;
the retirement and crew call-out message types must lie in the game's image. On Steam nothing should change: check
the load lines and a normal race are as 1.0.7.

## 1.0.7

1.0.7: the engineer's box calls are the full list from the game's English subtitles (143 lines: pit window, strategy,
damage, DRS, punctures, weather, safety car, traffic, fuel, "box this lap, mind the speed limit"). Fixed on the way:
the safety car "stay out" lines (eng_sc_pitstrat_03/04) and the pit window countdowns (eng_pit_open_10-30) no
longer count. Any engineer call that still leaves the crew held: the log line before the stop names its stem.

## 1.0.6

1.0.6: no BOX CONFIRMED popup any more; only CREW NOT READY shows, when the crew aren't ready.

## 1.0.5

1.0.4 result: two accepted strategy changes (front wing damage, slow puncture) left the crew held. The engineer asks
with eng_query_strat_NN and confirms your accept with eng_vc_cnfrm_NN; 1.0.5 counts a confirm within 30 s of his
question as calling the stop (log `the new strategy accepted (...)`), and his slow puncture calls (eng_slwpunc).

## 1.0.4

1.0.3 result: the engineer's mid-race call (eng_pit_open_04, the pit window opening) wasn't counted; 1.0.4 counts
every eng_pit_open line. The CREW NOT READY popup was queued and shown by the launcher at the pit entry (log), but
you didn't see it: check where it appears.

## 1.0.3

1.0.2 result: the safety car strategy call (eng_sc_pitstrat_02) wasn't counted as a call; 1.0.3 counts every
safety car pit strategy line except the "stay out" ones.

## 1.0.2

1.0.2: the engineer calling you in (strategy change "box this lap", box under the safety car, shredded tyre) counts
as calling your stop (log `the engineer calls you in (...)`). Ferrari Mode: in a Ferrari the crew are held on every
race stop, called or not (log `held (Ferrari Mode: the crew are never ready)`).

## 1.0.1

1.0.0 was approved by the launcher's review. 1.0.1 applies its suggestions: compat.launcher >=1.10.0 (before that
Game_PitLane_UpdateMovement isn't there and the DLL would be dropped while the 60 s sequences still apply), the
LICENSE copyright (was "AI Helmets contributors"), and Terminal Damage's copyright notice and license
(LICENSE-terminal-damage) shipped with the addon. No code changes.

## 1.0.0 (release)

The release build is 1.0.18's code, renumbered 1.0.0 (the 1.0.x numbers before were test builds). 1.0.18 result:
pit exit penalty 3 s from the first offence [x].

## 1.0.18

1.0.16 result: pit exit line level 3 = 2 s, level 4 (the stock second offence) = 3 s with the engineer's call [x].
1.0.18: the first offence is level 4 too (3 s + radio from the start); the plugin's 5 s override (1.0.17) is removed.
Still in: no loose wheel after a stuck nut on the same stop (1.0.17).

## 1.0.17

1.0.15/1.0.16 results: Ferrari Mode works [x] (your Ferrari: a stuck nut and a loose wheel on the same stop;
team-mate a loose wheel and DNF). The pit exit line now gives a penalty [x] but 2 s. 1.0.17: no loose wheel after a
stuck nut on the same stop; the pit exit line penalty goes out as 5 s (log `time penalty: car N, reason R, 2 s (just
left the pit lane: the pit exit line, 5 s)`). Cross the line once and send the log: the reason code lets the
plugin tie it to the rule exactly.

## 1.0.16

1.0.16: crossing the pit exit white line in a race gives a 5 s time penalty on the first offence (stock: a
warning). Check: cross it once in a race: a 5 s penalty notice and the penalty on the timing; a second crossing a
drive-through (stock second level).

## 1.0.15

1.0.13/1.0.14 results: 100 %: stuck nuts on both rear wheels [x] (a real 60 s roll; a 20 s roll cut exactly, then
retired) . 3 %: no stuck nuts, stop times 1.8-3 s [x]. Ferrari Mode couldn't be checked from the log. 1.0.15 logs
every Ferrari stop's roll while Ferrari Mode is on: `car N (Ferrari Mode): <sequence>, stuck nut roll R against 50 %`
(and the loose wheel). Wing replacement stops get no stuck nut roll.

## 1.0.14

1.0.14: **Ferrari Mode** (Ferrari's stuck nut and loose wheel chances at 50 %), and **Test mode** removed (the
sliders go to 100 %). Check Ferrari Mode on: Ferrari stops get stuck nuts about every other stop (log `stuck wheel
nut` for the Ferrari cars), other teams at the slider's chance.

## 1.0.13

1.0.12 result: 50 % mistakes with stock stop times: 18 stops, no mistakes. The game's mistake chance gives the addon
no control. 1.0.13: the game's mistakes are stock again (its own chances), and the plugin rolls its own stuck wheel
nuts (**Stuck wheel nut chance (%)**, 3 by default) by swapping the stop's sequence. Check with 100 %, Log pit
stops on: every stop `stuck wheel nut (F1StartJobDoStandardStopR?ON), N s this time`, a STUCK WHEEL NUT popup on
yours, the rear crew fighting the wheel, `comes free after` lines, the stop ending normally. Then a race at 3 %.
Stop time spread is unchanged (every stop still varies).

## 1.0.12

Why no crew mistakes with the mod? Stock F1 25 gives you one every few races; with the mod, none in ~140 stops.
Two Test modes, one race each, Stuck wheel nut chance Normal, Log pit stops on, several stops (yours and the AI's):
- **50 % mistakes, stock stop times**: expect mistakes on about half the stops if the game honours the chance.
- **50 % mistakes, mod stop times**: if this one has none, the widened stop times keep mistakes out.
In the log: `crew sequence: F1StartJob...` names ending in ON/OFF (wheel) or MON/MOFF (wing) are mistakes.

## 1.0.11

1.0.10 results: Normal (15 stops) and High (24 stops): no crew mistakes at all. Across every log since 0.2.10, about
140 stops at normal chances gave zero crew mistakes; mistakes only ever came at 100 %. High also retired AI car 19
after a front wing change slowed to x0.51 ran 17.7 s (taken for a stuck nut). 1.0.11 fixes that: the retirement roll
needs a rear stuck nut sequence on that stop.

## 1.0.10

1.0.9 result: mistakes back [x] (6 RRON stuck nuts, AI wing mistakes), cuts at 5, 10 and 20 s [x]. So the rear
crew's animation inside the front files switched the game's mistakes off. 1.0.10: rear stuck nuts only, front
mistakes and files untouched; High = all four rear mistakes + twice the mistake chance; Always = a mistake every
stop (stuck nut when it's a rear one). Check a race on Normal and one on High: crew mistakes happen at all, stuck
nuts only on the rear wheels.

## 1.0.9

1.0.8 result: no crew mistakes at Always, same save and a fresh race (mistake data identical to 1.0.4). 1.0.9 test:
the front wheels' files hold their own stuck nut again (1.0.4's files, front stuck nuts back for this test); only
the rear files (gun man loop) and the plugin differ from 1.0.4. If mistakes come back, the rear animation inside
the front files was what switched the game's mistakes off.

## 1.0.8

1.0.7 result: still no crew mistakes at Always (AI wing ones included); loose wheel at 1 % worked. 1.0.8: the
mistake data is exactly 1.0.4's (the last version with mistakes); only the erp files differ (the front wheels' files
hold the rear stuck nut). If mistakes come back, the shared engineer lines were the cause (on a front mistake the
engineer now names the front wheel while the rear crew struggles, to be fixed). If they don't, the cause is elsewhere:
note how you load the race (same save each time, or a fresh weekend).

## 1.0.7

1.0.6 result: at Always no crew mistakes at all, the AI's wing mistakes included (1.0.4 had them on every stop).
The only mistake-table change since 1.0.4: the front rows got the rear wheels' lines and event nodes. 1.0.7 gives
the front rows their own event nodes back (the spoken lines stay the rear wheel's). Check Always: mistakes again
(`F1StartJob...ON`, AI `FrontWingReplaceMON`). If still none, the shared lines are the problem too.

## 1.0.6

1.0.5 result: at Always, no mistake at all on 2 stops. In 1.0.4 every Always stop was the front left mistake
(FLON), so the game seems to keep picking that one, and 1.0.5 had renamed its animation to the rear left's (RLON):
the game then played no mistake. 1.0.6 keeps every mistake's own animation name and puts the rear stuck nut inside
the front wheels' files instead (High and Always). Check Always again: a stuck nut every stop, on a rear wheel, the
engineer naming the rear wheel, `comes free after` lines. Also note which sequence names play
(`crew sequence: F1StartJob...ON`): if it's always FLON, Normal (rear mistakes only) may hardly ever trigger.
The addon icon is packaged from this version.

## 1.0.5

1.0.4 result: rolled lengths and the cut work [x] (5, 10, 30, 60 s seen; once the clock didn't read sanely and the
stuck nut played in full, the fallback). 1.0.5: stuck nuts on the rear wheels only (High and Always map front
mistakes to the rear wheel on the same side), and the gun man loops his own failed gun attempt with its sound (rear
right: from frame 76, before 1.0.5 he only stood by). Check with Always: only rear stuck nuts, engineer names a rear
wheel, and the gun man visibly retries with the gun each loop.

## 1.0.4

1.0.4: every stuck nut is a 60 s animation cut short by the plugin to a rolled length (5 s 45 %, 10 s 30 %, 20 s
14 %, 30 s 7 %, 60 s 4 %). Test with Stuck wheel nut chance: Always and Log pit stops on. Per stuck nut the log says
`wheel-on mistake (...): if it's a stuck nut, N s this time`, then `the stuck nut comes free after N s (sequence a ->
b)`. Watch the moment it comes free: is it seamless (no snap of the crew or the wheel), and does the stop finish
normally? If the log says `the crew's sequence clock doesn't read sanely`, the clock offset is wrong on this build.

## 1.0.3

1.0.3: **Stuck wheel nut chance** also has **Off** (no stuck nuts; the game's own short mistakes).

## 1.0.2

1.0.2: **Stuck wheel nut chance** replaces the on/off setting: Normal (default; rear wheels going on only, ~3 % of
stops), High (all eight wheel mistakes, ~12 %), Always (every stop; was Test mode's "stuck wheel nut every stop",
now gone from Test mode). Check Always gives a stuck nut every stop on all four wheels with the right engineer line,
and Normal only ever shows rear wheel stuck nuts (front wheel mistakes stay the game's short ones).


1.0.1: new setting **Call your stop** (On by default; Off = crew always ready). No extra wait in the box (0 s):
an uncalled stop's crew run out as soon as you stop. The run-out was sped up; it now plays at the crew's normal
pace. With Log pit stops on, look for `the crew's run-out was at x...; played at normal pace` (the rate the game
used). If that line never appears, the speed-up isn't in that rate and needs another look. With Call your stop
Off: no `CREW NOT READY`, no hold lines.


1.0.0 fixes five settings (previously options): stuck wheel nut time Random, box hold on, longest hold 120 s,
crew not ready 1 s (in the box), stop time spread 30 %. Log pit stops is off by default: turn it on for test logs.
The rear right only stuck nut files (10-30 s) are gone; Test mode's stuck nut plays each wheel's own.


Install `dist\sedz.immersive-pitstops.swoftyaddon` and send the launcher log after each test.

0.2.16: your stuck-nut retirement is the game's own again (as 0.2.14). The 0.2.15 hand-off below is switched
off: in its test the push never started, because Terminal Damage 0.76.4's `Ai MessagePitStateChanged handler`
and `VehicleControllerSystem::setController` patterns find nothing on the 2026-11-16 game build (every log since
2026-11-20). The torn-off wheel then flailed on its tethers when the jacks dropped, Terminal Damage showed
SUSPENSION FAILURE, and the game's retirement teleported the car into the garage. Test A comes back once
Terminal Damage is updated for this build.

- [ ] Stuck-nut retirement (Test 1 settings): no wheel comes off, no SUSPENSION FAILURE, RETIRED as in 0.2.14.

## Test G: the crew stay in the garage (0.2.23)

0.2.27 result: held in the garage for the set time (3 s) [x], your stop matched to your car [x], but then the crew
were frozen again at the car for the same time (`held still first (not called)`): the old in-box freeze, which in
0.2.26 only stayed off because the stop was matched to car 16. 0.2.28: no second freeze when the crew were held in
the garage. Expect no `held still first` on a garage-held stop.

0.2.26 result: the hold works [x]: crew held in the garage, called out after 8 s in the box. In the log your crew's
start came 0.6 s after car 16 began entering its box and was matched to car 16 (wrong speed, and your stop not
marked as lengthened, 14.5 s in the box against the 15.8 s stuck nut limit). 0.2.27 matches a stop to its car by the
crew first, and marks a held stop as lengthened by the wait plus the run-out. Check: `car 21's crew start (...)`
appears for your held stop, and a longer **Crew not ready** (say 15 s) causes no `stuck wheel nut` line.

0.2.25 result: first stop of the session with other cars in the lane, so the plugin didn't know which crew was yours
(`not sure it's yours`) and didn't hold the run-out. The run-out is the game's normal call ~2.4 s before the car
reaches its box. 0.2.26 knows your crew from the start: the crews are laid out in garage order (checked in the log for
cars 0, 16 and 21), and every crew plays its idle sequence at the session load. Expect `held in the garage` on the
first stop too.

0.2.24 result: the crew still came out early and stood watching. The log: with the call-out held back, the crew
still played `F1InGarageToReady` (their run-out) ~8 s before the car reached its box: something else calls them as
the car gets close. 0.2.25 holds that run-out at its first frame (in the garage) until you have waited in the box for
the delay. Log lines:
- `car 21's crew start their run-out (F1InGarageToReady...): held in the garage` (or `not sure it's yours`, if
  other cars were in the pit lane before your first stop; then it isn't held),
- `car 21's crew start (F1StartJob...)`: note whether this comes before the crew have run out (the car was put in
  its box without them) or only after the release,
- `the crew are called out (the crew's wait in the box is over ...)`.

0.2.23 result: the crew stayed in the garage down the lane [x], but the pit lane brought the car into its box at
~8.9 s without waiting, started the crew's sequence, and the crew ran out at once (`the car is in its box; ... 0.1 s
standing`). 0.2.24 also freezes that sequence's start for the delay, so the crew should wait in the garage while you
sit in the box, then run out: log `the crew are ready, the stop starts` and `the crew are called out (the crew's wait in
the box is over ...)` together. Does the crew wait in the garage, and how does the run-out look?

0.2.20-0.2.22 held the crew still in the box. 0.2.23 keeps them in the garage: the game's call-out to the crew is held
back and sent late. Test mode Off, Crew not ready 8 s.

- [ ] At load: `crew hold: the crew's call-out message found from the launcher's bindings`.
- [ ] Pit without calling: `entered the pit lane without calling: the crew stay in the garage`. The crew stay in the
      garage as you drive down the lane. Where does your car stop: at its box, or somewhere before it?
- [ ] After 8 s standing: `the crew are called out (you waited; ...)`, the crew run out and do the stop. Or `(the car
      is in its box ...)` if the car went into the box first: then the crew run out to a waiting car.
- [ ] Call first (Box This Lap): normal stop, no hold lines.
- [ ] No crew left behind and nothing stuck on the next stops (yours and your team-mate's).
- [ ] With Terminal Damage on: its own broken-car handling unchanged.

## Test C: call your stop and stop spread (0.2.20)

0.2.20 result: the crew freeze on an uncalled stop works [x]. Stop spread works but was too narrow: five stops at
x0.93-x1.03 gave 2.4-2.6 s. 0.2.21 spreads evenly over +-15 % and fumbles about one stop in six (2.8-3.3 s for a
2.4 s stop). Check a handful of stops again.

0.2.22: **Widen team pit times too** removed (the `pit_lane.pit_times` table lists 2018 teams and changed nothing
in a career). Try **Stop time spread: 30 %** too: about 1.7-3.1 s for a 2.4 s stop, fumbles up to about 4 s.

0.2.19 result: the pit lane's crew-ready byte doesn't stop the car (`into its box ... didn't hold the car`), and
every stop was the same length (the game rolls one stop time per driver per session). 0.2.20 does both through the
crew's own animation update (launcher binding `Sess_PitStopUpdateJob_Update`). Test mode Off.

- [ ] At load: `crew speed per stop: 15 % spread; crew update job found`.
- [ ] Pit without calling: CREW NOT READY at the pit entry, then log `car N's crew start (...): speed x..., held
      still first (not called)`. In the box the crew stand still about 8 s, then `the crew are ready, the stop
      starts` and a normal stop. Does the freeze look acceptable (pose, car on its jacks or not)?
- [ ] Call first (Box This Lap): no freeze.
- [ ] Several stops: different stop times (the pit lane HUD's stop time and the log's `pit status 4 -> 5` gaps),
      mostly near your usual time, a few quicker or slower. AI stops too (`speed x...` lines for other cars).
- [ ] No stuck nut retirement on a frozen stop (a frozen 8 s + stop stays under the stuck nut limit).

## Test R: retirement found from the launcher's bindings (0.2.17)

Needs launcher 1.10.26 (it has `Game_Game_RetireLocalPlayer` and `g_Game_MessageBus`). Test 1 settings.

- [ ] At load: `retirement: status request found from the launcher's bindings (bus post bound)`. On an older
      launcher: `launcher bindings ... missing`, then `found by the byte pattern` (as before).
- [ ] Your stuck-nut retirement and the AI's work as in 0.2.16 (RETIRED, the game's retirement; AI cars to
      their garages).

## Test A (parked): stuck-nut retirement with Terminal Damage's garage push (0.2.15)

Settings: **Test mode: Stuck wheel nut every stop**, **Stuck wheel nut time: Random** (or 20 s),
**Retirement chance: 100 %**, **Log pit stops: On**. Terminal Damage on, with its keep control / retire in
the pit box / garage push options on. A race; pit.

- [ ] At load: `Terminal Damage found (your retirements use its push into the garage)`.
- [ ] The stuck nut plays out, then the stuck wheel comes off on the jacks: log `the wheel is taken off for
      Terminal Damage's push into the garage` and `car N's <corner> wheel comes off`. The corner should be the
      one the engineer named.
- [ ] Log `Terminal Damage rates the car broken (state 2); back to its box state for the push`, then Terminal
      Damage's own lines (`[td-probe] garage: car in the box, spawning your crew to push it in`), and
      `Terminal Damage state 3`.
- [ ] The crew push your car into the garage and Terminal Damage retires it.
- [ ] If instead the log says `Terminal Damage didn't rate the car broken` or `didn't start its push`, the
      game's own retirement follows (as in 0.2.14). Send the log: the hand-over needs another way in.
- [ ] Terminal Damage off: the game's own retirement as before.

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

- Without Terminal Damage, your retired car stays in its box (the game's own retirement).

## Earlier results

- 0.1.2-0.1.3: the database time loss of a mistake does nothing; mistakes are crew sequences in
  `pitstop_24/25/26.erp`.
- 0.1.4-0.1.6: a longer sequence plays; only the rear right crew loop, the rest hold still in a gap
  between keys; the engineer line is right in Test mode.
- 0.1.6-0.1.8: released 20.7 s after stopping whatever the length (failsafe at 15.77 s + the 5 s release
  wait); not the database's 10 or 7.5.
- 0.2.0: the native hold keeps the failsafe off; works.
