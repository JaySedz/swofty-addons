// Written by tools/build_ui.py: the Ferrari pages' layout sets (fp_<states>: r race, q practice /
// qualifying, s race under a safety car, d car page, p pit page); the plugin writes fp_<states> true
// on the 2026 Ferrari when its current layout is one of them.
#pragma once

inline constexpr const char *kFpCombos[] = {
    "d",
    "p",
    "q",
    "qs",
    "r",
    "rd",
    "rdpq",
    "rdpqs",
    "rp",
    "rpq",
    "rqp",
    "rqs",
    "rqsdp",
    "s",
};
