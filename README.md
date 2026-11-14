# Swofty addons

Addons for F1 25 on [Swofty Launcher](https://f1.swofty.net), each in its own folder with its own README and
licence.

| Addon | What it does |
|---|---|
| [AI Helmets](ai-helmets) | Gives AI drivers their new team's helmet when they move teams, with per-driver and per-team picks, imported helmets and MyTeam support. |
| [Music Replacer](music-replacer) | Replaces the game's 30 soundtrack songs and 3 theme pieces with songs of your own (mp3, m4a, wav, wma, flac). |

## Building

Each addon builds with Visual Studio 2022 (or its Build Tools) with "Desktop development with C++": run its
`build.bat`, then `package.bat`. The package lands in that addon's `dist` folder, ready to drop on the
launcher's window.
