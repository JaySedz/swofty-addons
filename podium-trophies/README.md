# Podium Trophies

A podium trophy for every track. Podium Trophies is a trophy engine: it adds trophies to the game as podium trophy
types of their own, and pairs them with tracks, base game or modded.

- **Players** pick a trophy for each track on the launcher's **Trophies** page. A track nobody pairs keeps the game's
  trophy.
- **Track makers** ship a trophy inside their own track addon (a `podium-trophies` folder). With Podium Trophies
  installed, their track gets it automatically; without it, the track keeps a stock trophy. Nobody has to wait for
  this addon to be updated.
- This addon ships its own trophies too: **Bahrain** (the game's Bahrain career cabinet trophy) and **Austria** (a
  wooden Red Bull Ring trophy made in Blender and Substance Painter).

No own offsets, nothing in the game's code: the engine adds resources to the podium archives and sets each paired
track's `trophyType` in the database.

## For players

1. Install Podium Trophies and turn it on.
2. Open **Trophies** in the launcher's sidebar. Every track in the profile is listed with the trophy it gets. Click a
   track and pick a trophy, **Game's own trophy**, or **Automatic** (the trophy its own addon or this addon names for
   it).
3. Press Play. Only the trophies that are paired go into the game.

Modded tracks show up after the profile has been built once (press Play once after installing a track). Monaco isn't
listed: its podium has its own trophy and scene.

## For track makers: ship your track's trophy

1. Model the trophy in Blender next to a stock one from `reference\` (see [reference/README.md](reference/README.md)
   for their sizes), texture it, and export an OBJ with its MTL and PNGs.
2. Write a `trophies.json` beside it (paths are relative to it):

   ```json
   { "trophies": [ {
       "id": "trophy_hockenheim",
       "name": "German GP",
       "grip": "trophy_modern_b",
       "source": "hockenheim_trophy.obj",
       "tracks": ["hockenheim"],
       "preview": "hockenheim_trophy.png"
   } ] }
   ```

   - `id` is the trophy type: `trophy_` and lower-case letters, digits or `_`. Make it unique, such as
     `trophy_<your track code>`. When two packs use one id, the first in load order keeps it.
   - `grip` is the stock trophy whose hand pose the winner takes it with (each trophy type has its own grip; the rest
     of the podium animation is the same for all). Pick the one closest to yours where the hand holds it, or `"none"`
     for no hand pose (as Monaco's). Your trophy is scaled to that trophy's height unless you give `"height"` in
     metres.
   - `donor` (optional) is the stock trophy whose model layout yours takes. Leave it out: the default,
     `trophy_closed_a`, holds the most vertices (see [Limits](#limits)).
   - `tracks` are the track codes it goes to automatically.
   - `preview` (optional) is a picture shown in the trophy menu.
   - Textures come from the MTL (`map_Kd`, `map_Bump`), or from `materials` (see [Your own textures](#your-own-textures)).
3. With the game closed, build the pack into your track addon:

   ```powershell
   python tools\build_pack.py path\to\trophies.json path\to\your-track-addon\podium-trophies
   ```

   The tools are in this addon's GitHub repository (`tools\`). They need Python 3 with numpy and Pillow, and read
   the game's stock podium archive (set `F1_25` if the game isn't
   in `E:\Steam\steamapps\common\F1 25`).
4. Recommend Podium Trophies in your addon's `registry.json` (beside `swofty.json`; launcher 1.6 or later), so the
   Addon Browser offers it at install and it never stops Play:

   ```json
   { "recommends": [ { "id": "sedz.podium-trophies", "for": "its podium trophy" } ] }
   ```

The pack holds your trophy's model and textures as game resources, and names any game materials it reuses (copied
from the player's own game files at build, never shipped). Players without Podium Trophies never load it.

## How it works

- `tracks.Track_N.trophyType` (and `TrackGeneralData_0.trophyType` in each season's track data) names a podium trophy
  type: `trophy_open_a/b/c/e`, `trophy_closed_a/b`, `trophy_modern_a/b`, `trophy_monaco`, `trophy_classic_a` (no
  track uses it), and the F2 types `trophy_f2_a/b/c`.
- The game spawns the trophy by that string. The podium scene, `podiums2019.scene` (binary XML), has a scene of that
  name: one `MultipartActor` whose mesh is `eaid://environment_package/core_assets/objects/idf/trophy/<type>_01`. Each
  of the five trophy sequences (`podium2019_trophy_seq01..05`) has the winner's clip `~driver_placed_first_<type>`,
  with the hand animation made for that trophy. No type name is in the game's code, so new types work.
- A trophy model draws its geometry four times, one per model node: `trophy_1st`, `trophy_ENG` (the constructors'
  trophy), `trophy_2nd`, `trophy_3rd`. 2nd and 3rd are smaller.

At build, the launcher helper (`bin\podium-trophies.exe`, source in `helper\`):

1. reads this addon's pack (`pack\`) and the `podium-trophies\` folder of every enabled addon;
2. pairs each track in the profile with a trophy: the player's pick, else a trophy the track's own addon names for
   it, else one this addon names, else none;
3. adds each paired trophy's resources to `cinematicspodium.erp` (its model and textures; game materials it reuses
   are copied from the game's archives);
4. adds a scene and five winner's clips for each paired trophy, the clips copied from its grip trophy's, to the clean
   `podiums2019.scene` and sequences, and replaces them in all seven podium archives that carry the scene (and the two
   that carry the sequences), with TSMD size headers made from the edited files plus the stock headers' margin;
5. sets `trophyType` on each paired track's `tracks.Track` row and on its row in every season data set that sets it.
   For a base track the data sets come from `tools\track_trophies.json` (read from the stock database); for a
   modded track, from its addon's files (the base track a track package copies, and the data sets its own ops name),
   with `when` on its addon, so turning the track off drops them.

Nothing is changed while no track is paired. The scene and sequences are edited from the launcher's clean copy of the
game files at every Play.

## Model format

Read from the podium and cabinet trophies (`tools/gfxmesh.py` has the full layout):

| Buffer | Format | Contents |
|---|---|---|
| `?lod=all&vb=0` | float3, 12 bytes | position, metres, Y up, base at y = 0 |
| `?lod=all&vb=1` | half4, 8 bytes | normal (w = 0) |
| `?lod=all&vb=2` | half4, 8 bytes | tangent, w = handedness (bitangent = w x cross(normal, tangent), V up) |
| `?lod=all&vb=3` | half4, 8 bytes | UV0 and UV1, V down |
| `?lod=all&vb=4` | rgba8, 4 bytes | vertex colour; only classic_a, closed_b, modern_b, monaco, open_b |
| `?lod=all&ib=0` | u16 | triangle list, counter-clockwise front faces |

Each buffer resource has a 16-byte head (kind, stride, count, format) and the data. The mesh descriptor
(`?lod=0&mesh`, GfxMeshRes) lists the submeshes (index range, bounding sphere, material slot), the node records
(model node -> submeshes), the draws, the buffer names and the vertex elements. Its parser rebuilds all 1,958 real
meshes in the podium, cabinet, F2 car and motorbike archives byte for byte. The motorbikes addon hides car parts it
doesn't use by zeroing their whole mesh descriptor; 216 of those are all zero bytes.

The model (`?model`) keeps its bounding sphere at 0x10 and its box at 0x24; the material binding
(`.emb?context=default`) maps the mesh's slot names (`01`, `02`, ...) to materials.

## Tools

Python 3 with numpy. The game folder is `E:\Steam\steamapps\common\F1 25`; set `F1_25` for another.

```powershell
python tools\trophy.py export "<F1 25>\2025_asset_groups\animation_package\scenes\cinematics\cinematicspodium.erp" trophy_closed_a_01 closed_a.obj --instance trophy_1st
python tools\trophy.py export "<F1 25>\2025_asset_groups\environment_package\core_assets\objects\gen_trophies_agdescs\trophy_country_career_champion_bah_a.erp" bah_a bahrain.obj
python tools\trophy.py build trophy_closed_a my_trophy.obj out --height 0.5 --material 01=eaid://...material
python tools\trophy.py selftest
```

- **export** writes an OBJ and MTL: one object per model node, one `usemtl` group per submesh, named after the material
  slot. The MTL names each slot's game material. It opens upright in Blender with the default OBJ import.
- **build** puts an OBJ (or `archive.erp:model`) into a podium trophy slot. Each `usemtl` name is a material slot. One
  model goes to all four placings and shares one index range; an OBJ whose objects are named after the donor's nodes
  keeps one per placing. When the new model fits in the donor's buffers they keep their sizes and the archive stays on
  the in-game loader; otherwise they grow and the launcher rewrites the archive into the game files. Bahrain's trophy
  is 18,484 vertices, against closed_a's 6,103.
- **export** also writes the textures (see [Your own textures](#your-own-textures)).
- **selftest** rebuilds every mesh descriptor, then runs export -> OBJ -> build on three trophies. With the OBJ's
  `#vtg` lines every buffer comes back identical. Without them, the way a 3D program writes the file back, positions,
  normals, UVs, colours, indices and the material binding are identical. Tangents are recomputed (median cos 0.9994,
  handedness 100% the same) and so are the bounding spheres.
- **tools\build_pack.py** builds a trophy pack from a `trophies.json` (see
  [For track makers](#for-track-makers-ship-your-tracks-trophy)). `package.bat` builds this addon's own pack
  (`pack\`, from `trophies.json`) and the helper, and packs `dist\sedz.podium-trophies.swoftyaddon`. It needs Rust
  (`winget install Rustlang.Rustup`) as well as Python.
- **tools\newtype.py** names a trophy's model resources after its type; **tools\bxml.py** reads and writes the
  binary XML scene and sequences and their TSMD size headers (every stock scene and sequence in the podium archive
  round-trips byte for byte, headers included). The helper does the same edits in Rust (`helper\src\bxml.rs`), and
  its output matches the Python edits byte for byte.
- **tools\track_trophies.json** lists each stock track's trophy type and the data sets that set it.

`pack\` is made from the game's own files, so it stays out of git. The Bahrain trophy's materials and textures are
copied into the podium archive from the game's cabinet archive on the player's machine, so no game texture ships in
the addon. The geometry does ship, as raw buffers.

## Your own textures

Each material slot (`usemtl` name) of an OBJ can use a game material or textures of your own:

- In Blender: give the material an Image Texture on Base Color (and a Normal Map node with its image, if you have
  one) and export the OBJ with materials. The build tool reads the MTL's `map_Kd` and `map_Bump` / `norm` and makes a
  material for that slot.
- Or in `trophies.json`, under the entry's `materials` (paths relative to it; see `_example`):
  `"body": {"colour": "models/body_d.png", "normal": "models/body_n.png", "surface": "metal"}`.

| Map | Game format | What to give it |
|---|---|---|
| `colour` | BC1 (54) | the base colour, any PNG |
| `normal` | BC5 (65) | a tangent-space normal map. Blender and Substance's OpenGL preset are green-up (`"normal_style": "opengl"`, the default); the game stores green-down, so the tool flips it. Leave it out for a flat surface. |
| `surface` | BC1 (52) | `"metal"` (default: the cabinet trophy's polished gold body), `"plain"` (its cloth ribbon), an `[r, g, b]` value, or a PNG. Blue is metalness (255 metal, 0 not); red and green look like occlusion and gloss. |

Each custom material is a copy of the Bahrain cup's material (`trophy_closed_a_01_01`, shader
`object_opaque_velocity`) with its three textures swapped for new ones. The textures go into the podium archive whole,
every mip down to 4 x 4, as two-fragment textures like the podium's flags, so no loose `.mipmaps` file is written.
New texture names keep the stock ones' length (`pt<hash>_<track>_<slot>...`), because the material stores each path
with its length.

`trophy.py export` writes each material's colour, normal and surface maps next to the OBJ
(`<name>_textures/*.game.png`, normal maps green-up) and names them in the MTL, so the model opens textured in
Blender. Paint over a copy under a new name: a material whose maps are all `*.game.png` keeps its game material. One
with a map of its own uses all of its maps, so an export with only its colour map changed keeps the game's normal map.
Blender doesn't keep the MTL's `# surface map:` line, so after a trip through Blender the surface is `"metal"` unless
`trophies.json` names one.

`python tools\texture.py` re-encodes the Bahrain cup's three maps and checks the error (0.00, 0.02 and 0.01 levels).

## Limits

- OBJ only, with triangles or polygons (fan-triangulated). The u16 index buffer reaches 65,535 vertices, and some
  donors hold the model more than once, because they draw 2nd and 3rd with smaller geometry instead of scaling it.
  The default donor, `trophy_closed_a`, holds it once; the grip doesn't count:

  | Donor | Copies | Vertices a model, at most |
  |---|---|---|
  | `trophy_closed_a`, `trophy_modern_a` | 1 | 65,535 |
  | `trophy_open_a`, `trophy_open_b`, `trophy_open_e`, `trophy_modern_b`, `trophy_classic_a` | 3 | 21,845 |
  | `trophy_open_c`, `trophy_closed_b` | 4 | 16,383 |
- Textures: power-of-two sizes up to 2048 (others are resized to the nearest). The game's own textures export at
  256 x 256, the size kept inside the archives; their larger mips are in Oodle-packed `.2025.mipmaps` files this tool
  can't read.
- The surface map's red and green channels are inferred (occlusion and gloss), not confirmed.
- One LOD (the podium trophies have one).
- Trophies work at every podium but Monaco's, which has its own trophy and archive.
- Build packs with the game closed: while it runs, the game folder holds the launcher's modded podium archives, and
  `build_pack.py` stops (`trophy.stock_check`).
- A pack is built against the stock podium archive of one game build. A game update that changes the donor trophy's
  vertex layout would need the pack rebuilt; the scene and sequences don't.
- The helper is a program the launcher runs, so uploads of this addon go to Swofty's manual review.

See [TESTING.md](TESTING.md) for the in-game checks.
