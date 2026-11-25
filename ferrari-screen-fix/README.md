# Ferrari Screen Fix

**Status: 0.1.1, a test build.**

The 2026 Ferrari's steering wheel screen is flat in the game's model, but its texture coordinates aren't straight:
rows are squeezed near the top and the edges sit higher than the middle. Whatever the wheel shows (the game's own
screen, or a dash addon such as Driver Display) bows, and part of the top is lost. Other cars map their screens
straight. This addon rewrites the Ferrari screen's texture coordinates (102 vertices over three detail levels) as a
straight mapping.

How: one `replace-resource` op on the model's shared texture coordinate buffer
(`ferrari.idf?lod=all&vb=9` in `2025_asset_groups/f1_2026_vehicle_package/teams/ferrari/wep/ferrari.erp`), built
from the game's own buffer by `tools/build_fix.py`. Nothing else in the car changes.

**After a game update** that changes the 2026 Ferrari model, rebuild with `tools/build_fix.py` before using it: the
shipped buffer is a copy of this build's, and a changed model would no longer match it.

With Driver Display, turn its "Ferrari wheel correction" setting off while this is installed: the screen no longer
needs bending.
