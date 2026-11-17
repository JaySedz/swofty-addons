# AI Helmets for F1 25 (Swofty Launcher)

Helmets for the AI drivers, the way Helmet Exchange does it for your own driver:

- **A helmet for movers.** When an AI driver moves to another team in a career (Silly Season, Career Mode
  Editor, the game's own market, or a one-weekend injury or sickness stand-in from another addon), they stop
  wearing their old team's sponsors and wear the helmet set for their new team: Simplicity by default, or any
  stock design, driver's helmet or helmet you imported. It changes at runtime, with no restart, and they get their
  own helmet back if they return.
- **Each driver's helmet per team.** Give any AI driver a helmet of your choice at a team.
- **Your own helmets.** Import helmet `.erp` files (team helmet mods); each becomes a helmet of its own
  (named "… (AIH)"), and no driver's helmet is replaced.

## Install

1. Run `build.bat`, then `package.bat` (Visual Studio 2022 with "Desktop development with C++").
2. Drop `dist\sedz.ai-helmets.swoftyaddon` on the launcher window, or run
   `swofty addons install dist\sedz.ai-helmets.swoftyaddon`, and turn the addon on.
3. Press **Play**.

## The AI Helmets page

- **Teams** tab: a table of the teams with Driver 1, Driver 2 and the helmet newcomers wear. While the game
  runs, it shows the loaded career's line-up (after its first Team HQ): a driver who moved is listed under his
  new team, "from" his old one. When the game closes it goes back to the original line-ups.
- **Click a team** to open its panel:
  - **Drivers who join <team> wear**: the helmet a driver who moves there wears (default: Simplicity), and
    **Import helmet for new <team> drivers…**, which imports a `.erp` and makes it that helmet.
  - **One card per driver**: their helmet at that team (their own, the team default, a stock design, another
    driver's helmet, another addon's or one you imported), and **Import helmet for <name>…**, which imports
    a `.erp` and gives it to them at that team.
- When the game is closed the table shows the stock line-ups (worked out from the drivers' own helmet rows), so
  a seat the player takes in a career still shows its real driver. The driver you race as is marked "(You)"; their
  helmet is never changed by AI Helmets.
- **Free agents** tab: every other driver in the game database (F2 and F3 drivers, reserves, legends, addon
  drivers), with the same search. Click one to set the helmet they wear at whichever team they join (default: that
  team's newcomer helmet) or to import one for them. A driver with no F1 helmet of their own who is seated after a
  career's first Team HQ counts as joining from the free agents.
- **Imported helmets** tab: the default newcomer helmet for every team, and your imported helmets (the
  helmet shell each is built on, and Remove).
- **What you can import** (no other addon needed):
  - a helmet archive: an `.erp` whose resources are a driver's helmet, such as `<first>_<last>_helmet.erp`. It
    is built on the helmet row its own resources name (`drivers/male/2026-redbull_max_verstappen/...` → Max
    Verstappen's 2026 helmet), whatever the file is called; helmets imported before that, on Lewis Hamilton's 2026
    row, are moved onto theirs at the next Play;
  - a driver's body archive (`<first>_<last>.erp`: suit, gloves and boots, with the helmet's textures but no helmet
    model), which some helmet mods include. Its helmet textures are converted onto that driver's stock helmet;
  - a **"career helmet" mod**: a repaint of the game's helmet template (`teamname_charname/.../charname_helmet_d`),
    usually a replacement `game_package/markdown/markdown_system.erp`. AI Helmets **converts** it into a helmet of
    its own: it copies Lewis Hamilton's 2026 helmet archive from your game files, renames its folder to a
    same-length one (`a001-ferrari_lewis_hamilton`, `a002-…`), and puts the mod's helmet, wing and visor textures
    in it. The result goes into the game at the next Play with its own asset group;
  - a **design archive**: a whole helmet design from the livery package, such as the SERPs template's
    `player_helmet.erp` (`unassigned_livery18_helmet_*` textures), which most overtake.gg helmets are painted on.
    It is converted the same way;
  - a **pack** (`.zip`, `.7z`, `.rar`) holding any of these; every helmet in it is imported. A real driver's
    helmet comes as several archives (his helmet archive, and his body archive with the same helmet textures):
    they become **one** helmet, converted from the helmet archive onto that driver's stock helmet. Whole helmets
    of their own (two SERPs designs in one pack) stay separate, but copies of one design are one helmet: archives
    whose helmet texture is the same byte for byte (a SERPs mod's `character_helmet.nefx2.sm51.erp`, its
    `_amd` twin and any loose copy) are imported once, since only their helmet textures are used. A pack's
    `.mipmaps` files (`F1 25/character_package/drivers/male/<driver>/textures/*.mipmaps`, its textures'
    high-resolution levels) go with them: each helmet's textures are pointed at a same-length folder of its own
    (`a001-redbull_max_verstappen`, `b001-…` for helmet archives used as they are), where the files are put at
    Play, so the stock driver's own files stay untouched. Without them the game reports "Error loading streamable
    mipmap" and the helmet stays at its lower resolution. Helmets imported before 1.0.3 have none: import the
    pack again.
- **Converted helmets are built on the helmet they were painted for.** Every helmet shares one shell
  (`helmet_bell_hp7`), but each has its own visor attachments mesh with its own UVs, so a visor texture only lines
  up on the archive it was made for. AI Helmets reads the mod's visor attachments model (else the folder of its
  helmet texture) and copies that stock archive from your game files. The SERPs template, for example, carries
  Lewis Hamilton's 2020 replica attachments (`replica_hamilton20a`), so SERPs helmets are built on that, including
  SERPs designs that ship only their textures (`unassigned_livery18_*`, no attachments model) and those whose
  attachments model is named `unassigned_livery18`: the stock `unassigned_livery18` design ("The story is yours")
  has a different visor mesh, and its colour masks (`helmet_m`) would show its pattern through the mod's. When it
  can't tell (career-helmet mods name no helmet of their own) or the game doesn't have that helmet, it falls back
  to Lewis Hamilton's 2026 helmet. The import message says which one it used.
- Everything is made ready to use as it is imported. Helmets an earlier version imported as they were (packs,
  career-helmet mods and design archives, white in game) are converted automatically at the next Play; the teams and
  drivers using them keep them. Helmets already converted onto Hamilton's 2026 helmet stay on it: import the mod
  again to rebuild it on the helmet it was made for.
- **MyTeam** tab (0.7.0): your two MyTeam drivers, the one you drive included, from the loaded MyTeam career
  (or the last one you played). Set a helmet for every driver you sign, or one per driver, or import one. With
  nothing set they keep their own helmets: the automatic newcomer helmet never applies to your team. The team is
  found from the core's career state; the log line `MyTeam career: ...` shows what was found.
- **Imported helmets** tab: the default newcomer helmet, and a searchable list of your helmets. Click one to
  remove it, or, for a helmet archive, to set **Made for** (the driver whose helmet file it was made from; it has to
  match, or the game draws it white). Converted career-helmet mods have nothing to set.
- Helmet Exchange's helmets, when you have it, are listed by name in their own group. AI Helmets doesn't need it.
- **Import helmet…** at the top imports into your helmets. A helmet archive named `<first>_<last>_helmet.erp` is
  built on that driver's helmet shell.
- Picks and team helmets made while the game runs show at the next Team HQ or session start. An imported helmet
  is added to the game the next time you press **Play**.

**Team helmet after a move** (Addons page, or ADDON SETTINGS in game) turns the automatic part on or off.

## How it works

- A driver who moves wears the helmet set for his new team on the page, else the default newcomer helmet
  (`game/tier01_livery76`, "Simplicity"). 0.5.0 dropped the tinted placeholders: the game ignored their colours
  for AI drivers. Import a team helmet mod instead.
- Once the game database has loaded, the plugin reads the drivers, teams, looks and helmet rows from it. A
  driver's looks are rows like `2026-ferrari_lewis_hamilton`, one per season and team; a helmet change goes
  on all of them. It keeps the list in `looks.json`.
- **Helmets change at runtime, with no restart.** At every Team HQ and session start the plugin reads the
  career's line-up. For an AI driver racing away from their home team (or with a manual pick) it changes the
  helmet inside the game's look records in memory: every field holding one of their own helmets
  (`(helmet id << 16) | 1`, at +0x18 of the record) gets the new helmet's id instead. A driver who goes home, leaves the grid or whose
  career unloads gets their own helmet back. Moves made mid-season by other addons (injury or sickness
  replacements, one-weekend reserves) are picked up at the next Team HQ or session start.
- The look records are found the way Helmet Exchange (MIT, by giggles) finds the player's: the game manager's
  systems map gives the EntrantsSystem (`k_Live_EntrantsSystemKey`), which holds 4 lists of look records at
  `+0x298`. Only the helmet changes; suits and gloves stay as they are.
- Picks (`"picks": {"game/<driver>|<team key>": helmet}`) use the same mechanism: a driver's helmet at one team.
- History: 0.2.x layered looks after boot (the core refuses that); 0.3.0 switched drivers into shared team looks
  (the game crashed after a race); 0.3.1 added a look per driver and team (the session load hung). 0.4.0 adds
  no looks and switches none. 0.3.0's 21 team looks are still registered so a career that points at them loads.
- The plugin writes `catalogue.json` (the career's AI drivers and the game's helmets) for the page.

### Changes in 0.2.0

- Placeholders use a real helmet row. 0.1.0 named `helmet_tier01_livery03`, which the game doesn't have, so
  the core refused all 21 placeholders.
- Drivers and teams come from the game database. 0.1.0 asked the registries, which list only addon drivers,
  so it found no stock drivers and changed nothing.
- Helmet changes go on the driver's real look rows (`<year>-<team>_<driver>`), not on `game/<driver>`.
- Imported helmets are built on a real helmet row, e.g. `game/2026-ferrari_charles_leclerc`.

Look for `[ai-helmets]` lines in `swofty/loader.log`.

## Limits

- **Drivers made by other addons** are listed like any other driver and get a team's helmet when signed, as
  long as their own helmet is theirs alone. Driver Manager's drivers (database rows `dm_<id>`) are read from its
  data by the page (0.8.1), which shows their names and hands their helmets to the plugin; drivers an addon
  registers come from their looks (0.8.0). A helmet is changed by swapping it in every look record that holds it,
  so it must be in that driver's look only: one who wears a stock design another driver also wears, another
  driver's helmet, or a helmet AI Helmets hands out keeps it, and the log says why. Give each created driver a
  design no one else uses.
- **More Driver Models' drivers** (Vettel, Schumacher, Button and the others it adds) all get a copy of the game's
  free-agent look (`mdm_<driver>`), all with one helmet (`unassigned_livery18`, "The Story Is Yours"), so their
  helmets couldn't be changed one by one. AI Helmets now changes the helmet in that driver's own look record only,
  so team helmets, picks and MyTeam work for them like for any driver, and two of them in one team can wear
  different helmets. Nothing to set up: it works from the first game start. How a record names its look row (a
  pointer to the row's label, the label, or a hash of it) is found from the rows, and taken only when the records it
  names carry their rows' helmets; when it isn't found, nothing is changed and the log says
  `look records: no place names their rows`, with a few records for working it out.
- **Looks other addons link to their driver by his label** (a season pack's `bdx_..._look`) count as his own when no
  other look wears their helmet.
- **The runtime helmet change works on build 25251167** (tested: 14 moved drivers changed at Team HQ, seen in the grid reveal). It relies on the game's in-memory look records, which
  Swofty doesn't document; a game update can move them. The plugin then logs why ("no EntrantsSystem", "his
  helmet is in none of the look records") and changes nothing.
- The line-up is read from the game's entrant rows as Swofty's experimental (L2) game headers lay them out. A
  game update can move them; the plugin then logs "line-up not readable" and changes nothing.
- A move made after the plugin's Team HQ check (another addon's, in the same visit) is seen at the next session
  start, so the new helmet can be one session late.
- A driver's home team is the team their own helmet row is made for (`2026-mclaren_lando_norris` → McLaren), so
  moves made before the addon first ran count too (0.2.1). Drivers with no such row (rookies, F2-only drivers,
  addon drivers) fall back to the first team the addon sees them at.
- Teams other than the stock 2025 and 2026 ones (year packs, added teams) appear on the page once the game has
  shown them in a career line-up.
- Only the helmet changes. Suits and gloves come from the driver's look as before.
- Stock designs are worn in their own stock colours: the game ignores colour changes for AI drivers.
- Long freezes on entering Team HQ with Helmet Exchange installed come from Helmet Exchange rendering a
  thumbnail for each of its helmets (about 0.4 s each), not from this addon.

## Building

```
build.bat       builds native\ai_helmets.dll, the zstd decoder and bin\ai-helmets-helper.exe
build_deps.bat  builds libarchive, zlib, liblzma and bzip2 into build\deps (build.bat runs it once)
package.bat     packs dist\sedz.ai-helmets.swoftyaddon
tools\build-erptool.bat   builds tools\erptool.exe (list, strings, dump, hashcheck, convert): a development aid
```

`native/include` is the Swofty SDK (MIT). `mixin.hpp` has one change so MSVC builds it (`&bound::addr`), and
MSVC needs `/Zc:preprocessor`. `third_party/zstd` is the zstd 1.5.6 decoder (BSD). The helper unpacks helmet
packs with `third_party/libarchive` 3.7.7 (zip, 7z and rar readers only), built with `third_party/zlib` 1.3.1,
`third_party/xz` 5.6.3 (liblzma) and `third_party/bzip2` 1.0.8; it starts no other programs. Licences are in
THIRD_PARTY_NOTICES.md. build_deps.bat needs the CMake and Ninja that come with Visual Studio's C++ workload.

The archive format follows Swofty's `packages/formats/src/erp.rs`.
