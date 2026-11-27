# Weapon media provenance

This directory (`pak/`) contains game media imported from the WET-NoQuarter
source and from Jaymod. NewMod imports the full No Quarter weapon set —
55 `.weap` definition files plus the corresponding model, sound, and icon
assets — alongside the Jaymod-sourced M97 shotgun and landmine/poison-gas
variants that extend the base NQ weapon list.

## WET-NoQuarter (base weapon set)

Source: <https://github.com/core-c/WET-NoQuarter>, revision
`a56829de1368e8da1bdf93acdeae43bddeed790e`, `trunk/assets/`.

| Media | NQ source path | Imported to |
| --- | --- | --- |
| Weapon definitions (`.weap`) | `trunk/assets/weapons/*.weap` (51 files) | `pak/weapons/` |
| First-person weapon models (`.md3`, `.mdc`, `.skin`, `.cfg`) | `trunk/assets/models/weapons2/*` | `pak/models/weapons2/` |
| Multiplayer/weapon-item models | `trunk/assets/models/multiplayer/*` | `pak/models/multiplayer/` |
| Ammo/projectile models | `trunk/assets/models/ammo/*` | `pak/models/ammo/` |
| Weapon sounds (`.wav`) | `trunk/assets/sound/weapons/*` | `pak/sound/weapons/` |
| Misc sounds (`bag_toss`, etc.) | `trunk/assets/sound/misc/*` | `pak/sound/misc/` |
| Weapon icons (`.tga`) | `trunk/assets/icons/iconw_*` | `pak/icons/` |

The 51 `.weap` files cover every weapon referenced in `bg_weapons.c`'s
`BG_RegisterWeaponFromWeaponFile` switch: MP34, StG44, adrenaline, akimbo
variants, ammo pack, mortar variants, BAR, bazooka, binoculars, Browning,
colt, dynamite, FG42, flamethrower, foot, GPG40, grenade, Johnson, K43,
Kar98, knife, knife_kabar, landmine, luger, M1 Garand variants, M7,
medpack, MG42, mortar, MP40, Panzerfaust, pineapple, pliers, poison,
satchel variants, shotgun, silenced variants, smoke grenade, smoke marker,
Sten, syringe, Thompson, and venom.

`smoketrail.weap` (the artillery white-phosphorus smoke trail, `WP_SMOKETRAIL`)
is not in the NQ source repository; it is provided by the compiled
base-game pk3 and resolved by the engine at runtime.

## Jaymod (extended weapons)

Source: <https://github.com/budjb/jaymod>, revision
`bbe44e0cca806d6b51260d48d4c11eab2b85bfe9`.

| Media | Jaymod path | Imported to |
| --- | --- | --- |
| M97 shotgun models (`v_m97*.md3`, `m97_3rd.md3`, `m97_pickup.md3`, skins, TGAs, `weapon.cfg`) | `pak/models/weapons2/m97/` | same |
| Shotgun shell model | `pak/models/weapons2/shells/shotgun.md3` | same |
| Landmine bouncing-betty models (skins, markers, icon) | `pak/models/multiplayer/landmine_bbetty/` | same |
| Landmine poison-gas models (skins, markers, icon) | `pak/models/multiplayer/landmine_pgas/` | same |
| Poison-gas cannister models (skins, icon) | `pak/models/multiplayer/poison_gas/` | same |
| Poison fluid shader | `pak/models/multiplayer/poison/fluid.tga` | same |
| M97 weapon sounds | `pak/sound/weapons/m97/` | same |
| Poison-gas cough sound | `pak/sound/poison_gas/cough.wav` | same |
| Weapon definitions (`.weap`) | `pak/weapons/{m97,landmine_bbetty,landmine_pgas,poison_gas}.weap` | same |

## Inherited NQ models from base-game pk3

Many `.weap` files reference models that are inherited from the No Quarter
base game (not in the NQ source repository). These are provided by the
compiled base-game pk3 files and are **not** shipped in this source tree.
The engine resolves them at runtime from the base-game pk3. Key examples:

- `models/multiplayer/landmine/*` (base landmine geometry, `weapon.cfg`)
- `models/multiplayer/smokebomb/*` (smoke-bomb geometry used by poison gas and smoke grenade)
- `models/weapons2/mp40/v_mp40_flash.mdc` (muzzle flash, shared across weapons)
- `models/multiplayer/mine_marker/*.md3` (mine markers)
- `models/multiplayer/panzerfaust/*` (Panzerfaust 3rd-person model)
- `models/multiplayer/dynamite/*` (dynamite 3rd-person model)
- `models/multiplayer/satchel/light*` (satchel detonator lights)
- Sounds for weapons whose source-repo directories are absent (colt, luger,
  mp40, sten, thompson, fg42, k43, garand, mg42, rocket, grenade, dynamite)

The 26 weapon-selected icons (`_1_select` variants) not present in the NQ
source icons directory are likewise provided by the base-game pk3.

## License

WET-NoQuarter is derived from id Software's Enemy Territory under the
original id Software terms. Jaymod attributes its source and media to Bud
Byrd and Mike Miscevic and states the Apache License, Version 2.0 plus the
original id Software terms. The full Jaymod license text is retained at
`licenses/Jaymod-LICENSE.txt`.

The imported `.md3` geometry, `.mdc`, `.skin`, `.tga`, `.jpg`, `.wav`, and
`.weap` files are NQ- and Jaymod-origin game media. They are distributed
here under the terms that apply to the source snapshots NewMod consults,
pending the licensing review recorded in `PROVENANCE.md` and `LEGAL`. The
NQ base models that some `.weap` files reference remain subject to the NQ
media notice at `licenses/NoQuarter-media-readme.txt`.

## Wiring

- **Base NQ weapons** (weapon IDs 1–63): registered via
  `BG_RegisterWeaponFromWeaponFile` in `bg_weapons.c`, which maps each
  `WP_*` enum value to its `.weap` filename. All 55 `.weap` files in
  `pak/weapons/` correspond to entries in this switch.
- **`WP_M97`** (weapon ID 64): 11-pellet shotgun, fired via `Weapon_M97`
  with `EV_M97` client event for pellet-spread tracers.
- **`WP_POISON_GAS`** (weapon ID 65): thrown grenade that spawns a
  poison-gas cloud via `G_PoisonGasExplode`, applying periodic
  `MOD_POISON_GAS` radius damage every 1500ms.
- **`WP_LANDMINE_BBETTY`** (weapon ID 66): landmine variant that leaps
  into the air before exploding via `G_BouncingBettyTrip`.
- **`WP_LANDMINE_PGAS`** (weapon ID 67): landmine variant that spawns a
  poison-gas cloud via `G_PoisonGasTrip` when triggered.
