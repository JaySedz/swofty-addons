# Testing Podium Trophies

0.1.5 adds `"grip"`: a trophy takes `trophy_closed_a`'s model layout and another stock trophy's hand pose. Austria now
uses that (closed_a layout, modern_b grip, as before at 0.469 m): check it still sits in the winner's hand as in
0.1.3.

0.1.4 was the first build of the trophy engine: a **Trophies** page in the launcher, and a helper program that adds
the paired trophies at each Play. It ships two trophies, Bahrain (the career cabinet trophy) and Austria (the wooden
Red Bull Ring trophy), both as trophy types of their own. 0.1.3 proved the new types in game; this test is about the
engine around them.

## Before you start

1. Close the game, then run `package.bat`. It prints `trophy_bahrain: ...`, `trophy_austria: ...`, a cargo
   `Finished` line and `packed dist\sedz.podium-trophies.swoftyaddon`.
2. Install `dist\sedz.podium-trophies.swoftyaddon` in the launcher over 0.1.3 and turn it on.

## 1. The Trophies page

- [ ] **Trophies** shows in the sidebar, and opens without an error. An error such as "Podium Trophies is not
      running" or "did not answer": copy `helper.log` from the launcher's cache folder
      (`cache\addons\sedz.podium-trophies\`) and the message.
- [ ] Every track of the profile is listed; Bahrain (and its short and 2024 layouts) shows **Bahrain GP (career
      cabinet trophy)**, Austria shows **Austrian GP**, the rest **Game's trophy**. Monaco isn't listed.
- [ ] A modded track you have installed is listed with its addon's name under it. If the page says to press Play
      once, it is listed after the first Play.
- [ ] Click Jeddah, pick **Bahrain GP** in the menu. Its tag changes to the Bahrain trophy. Pick **Automatic** again
      afterwards to put it back.

## 2. The build

Press Play and read the build lines. Expected, with no picks changed:

- `sedz.podium-trophies: 2 trophies on N tracks` and a line a track (`bahrain -> trophy_bahrain`, `austria -> trophy_austria`, and their other layouts).
- `built into the game files` lines for the seven podium archives. Nothing about a conflict on
  `podiums2019.scene`.
- No database error on a `tracks.Track[...]` or `TrackDataSet` line.

## 3. In game

- [ ] **Austria**: your wooden trophy, as in 0.1.3.
- [ ] **Bahrain**: the cabinet trophy, now as `trophy_bahrain` (before it replaced `trophy_closed_a`).
- [ ] **Jeddah**: the stock trophy.
- [ ] Then pair Jeddah with the Bahrain trophy on the Trophies page, Play, and finish a Jeddah race: the Bahrain
      trophy shows there. This proves a pick on the page reaches the game.
- [ ] Optional: pick **Game's own trophy** for Austria, Play: the build says 1 trophy, and Austria has the stock
      trophy again.

## 4. A modded track (optional)

Give a modded track one of the trophies on the Trophies page (Hockenheim with the Austrian trophy, say), Play, and
race it. Expected: that trophy on its podium. If the build warns `found no season data for it`, note the track's
addon: the engine set only its track row, and the test shows whether that is enough.

## After the test

Turning the addon off and pressing Play puts the stock archives and database values back.

Report: the launcher version, the build lines, which checks failed, and `helper.log` if the page or the build
failed.

## Limit test: 50 trophy types

`test\build.bat` builds `dist\sedz.trophy-test-pack.swoftyaddon`, a trophy pack in an addon of its own (as a track
maker would ship one): a trophy type for every track code but Monaco, `trophy_test_<code>`, each the game's career
cabinet trophy of that country (Las Vegas and Madrid get the F1 championship trophy, Sochi and Hanoi the F2 one), plus
eight extras (challenge and esports trophies). With Podium Trophies' own Bahrain and Austria that is 50.

1. Install it next to Podium Trophies and turn both on. On the Trophies page every track now shows its test trophy
   (Bahrain and Austria keep Podium Trophies' own).
2. **39 types:** press Play. The build says `39 trophy types in the podium scene, 42 tracks paired`.
3. **50 types:** turn on **Build every trophy (test)** in Podium Trophies' settings and press Play: `50 trophy types
   in the podium scene`. The extras aren't paired with a track, but they are in the scene and the archive.

At each step:

- [ ] The game starts and reaches the menus.
- [ ] A podium shows its trophy, such as Jeddah (Saudi cabinet trophy), Silverstone or Monza. One race is enough;
      more tracks show more types.
- [ ] Note the podium's load time against a stock podium, and any stutter.

If 50 fails and 39 works, the limit is in between: tell me, and I'll make the test pack take a count.

Every test trophy uses `trophy_closed_a`'s model layout (room for the cabinet trophies' 18,500 vertices) and the hand
pose of its track's stock trophy (`"grip"`), at that trophy's height. So each winner should hold its trophy as they
held the stock one.

Turn the test pack off afterwards: it isn't for release.
