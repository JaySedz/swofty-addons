# Music Replacer

Replace F1 25's music with songs of your own.

Open **Music Replacer** in the launcher's sidebar. It has two tabs:

- **Soundtrack**: the game's 30 licensed songs. They play in the menus, practice sessions and time trial when
  **Music Type** is set to **Soundtrack** in the game's Audio settings.
- **Theme**: the 3 front-end theme pieces by Ian Livingstone. They play in the menus when Music Type is
  **Theme**.

Click a song, then **Replace…**, and pick an mp3, m4a, aac, wav, wma or flac file. It is converted straight
away, in the background: the song's button shows how far it has got (Converting…, Compressing… 40%), and
several songs picked in a row are converted one after another. The new song plays from the next time you press **Play**. **Use the game's song** puts the original back.

## Quality

Every song is saved as 160 kbps Opus, the game's own format, about 1.2 MB a minute.

## How it works

- Songs are decoded with Windows' own decoders and encoded with libopus, built into the helper (it starts no
  other program),
  then packed as Wwise Opus `.wem` files (codec 0x3041: format, hash, seek table, packets), the layout the
  game's songs use. They are kept in the addon's data folder.
- A streamed song's opening (its header and first 11 packets, as the game's own) goes in `music.bnk`; a theme
  piece goes in whole.
- When you press Play, the addon rebuilds the game's two music banks (`metadata.bnk` and `music.bnk`) from the
  clean copies the launcher keeps, with your songs in, and puts them in the game folder with the songs. With
  nothing replaced, it puts nothing: the game's own files stay.
- A replaced song plays at its full length; the game moves on to the next song near its end, as it does with
  its own.

## Limits

- Only one addon can replace the game's music banks. Remove other addons that change
  `audio/2025/soundbanks/music.bnk` or `metadata.bnk`.
- The banks are rebuilt from the game version the launcher knows; after a game update, press Play again.
- Ten more soundtrack songs and three older theme pieces are named in the game's text, but the game has no
  audio for them, so they are not listed.

## Building

Run `build.bat` (Visual Studio 2022 with "Desktop development with C++"), then `package.bat`. The package is
`dist\sedz.music-replacer.swoftyaddon`. The first build compiles `third_party/libopus` (1.5.2, BSD; see
THIRD_PARTY_NOTICES.md) with `build_deps.bat`, which needs the CMake and Ninja that come with Visual Studio.
