# Size references

- `game/`: the game's podium trophies as OBJ (winner's size), exported with `tools\trophy.py export` from your
  `cinematicspodium.erp`, plus the Bahrain cabinet trophy at its original cabinet size. Made from game files, so they
  stay out of git.
- `game/*_textures/`: each trophy's colour, normal and surface maps as PNG (256 x 256, normal maps green-up), named
  in the MTL so the trophies open textured.
- `boxes/`: one box for each trophy, the size of its bounding box: base at 0, centred where the trophy is.

Units are metres. Import with Blender's default OBJ settings and model next to the reference; the front of the trophy
faces Blender's front view (numpad 1). Export with the default OBJ settings and the build tool reads it the right
way round.

| Trophy | Width (m) | Height (m) | Depth (m) |
|---|---|---|---|
| trophy_closed_a (Bahrain) | 0.271 | 0.503 | 0.188 |
| trophy_closed_b | 0.341 | 0.496 | 0.192 |
| trophy_classic_a (unused slot) | 0.190 | 0.587 | 0.182 |
| trophy_modern_a | 0.181 | 0.551 | 0.224 |
| trophy_modern_b | 0.156 | 0.469 | 0.193 |
| trophy_monaco | 0.183 | 0.517 | 0.177 |
| trophy_open_a | 0.199 | 0.464 | 0.199 |
| trophy_open_b | 0.187 | 0.478 | 0.188 |
| trophy_open_c | 0.385 | 0.450 | 0.237 |
| trophy_open_e | 0.260 | 0.486 | 0.260 |
| Bahrain cabinet trophy, cabinet size | 0.275 | 0.655 | 0.282 |

The build tool scales a model to the height in `trophies.json` (`"donor"` = the stock trophy's height), so the size
you build at only matters for proportions.
