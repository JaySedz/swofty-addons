# Konnersport 2026

Adds Konnersport as a 12th AI team to the 2026 season, the way the game adds it to the 2025 one:
Aiden Jackson (#89) and Callie Mayer (#66) in the 2026 car with Konnersport's livery, and Arron Barnes and
Noah Visser as reserves. A 2026 Driver Career or Grand Prix then fields 12 teams and 24 cars.

## What it changes

Grid:

- A new car, `konnersport_2026`, copied from the 2026 FOM car (the My Team body), performance tier 2 as the
  game's `konnersport_2025`, with Konnersport's names.
- A 2026 livery preset, `konnersport_vehicle_customisation_2026`: one of 16 of the game's 28 2026 livery templates (16 since 1.0.8: launchers now take at most 16 choices in a setting)
  (`livery_titlesponsor_liveryNN_2026`, the **Car livery** setting, template 12 by default) in Konnersport's
  colours. The game's Konnersport livery is painted for the 2025 car: on the 2026 car it races white, or
  misaligned once made compatible.
- Two entrants added to the `f1_2026` grid list in the game's `konnersport` team. The 2026 career line-up
  (`f1_career_2026`) reads that list, so a new career includes them.
- Konnersport's two reserves added to `f1_career_2026_reserve_drivers`.
- The `f1_26` entrant setup mode fields up to 24 cars instead of 22.
- The `f1_26` world switches to the game's My Team world, which has 24 grid slots and 12 garages
  (the standard world has 22 and 11).

2026 career data, each a copy of Cadillac's 2026 row with Konnersport's own 2025 values:

- Driver specialisation (`f1_2026` set): Jackson and Mayer with Konnersport's garage livery.
- Accolades, initial renown and form, and the team's primary sponsor (Butler Global).
- Driver market rating range, so the team signs and loses drivers like the others.
- R&D resource points: linked to the same starting row as in 2025 (`user_defined`, 1150 points).

The game already has 2026 rows for Konnersport's recognition modifiers and for both drivers' accolades,
recognition and specialist AI.

## Limits

- **Start a new 2026 career.** An existing career keeps the line-up it was created with.
- **My Team stays at 24 cars.** My Team draws its own 2 seats first and fields 24, so Konnersport is left
  out there. A grid with both needs 26 cars, which needs extra grid slots, garages and pit boxes in every
  track's world: not in this version.
- Use realistic performance and a random grid. The equal performance grid and the weighted random grid
  are still sized for 22 cars.
- The 2026 constructors' standings that set the garage order have no Konnersport row; the game puts teams
  without a standing last.

## Building

Run `package.bat`. The package is `dist\sedz.konnersport-2026.swoftyaddon`.
