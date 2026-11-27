# Jaymod weapon media provenance

This directory (`pak/`) contains game media imported from Jaymod for the
M97 shotgun, landmine variants (bouncing-betty, poison-gas), and the
poison-gas grenade. NewMod imports these as four real weapon IDs
(`WP_M97`, `WP_POISON_GAS`, `WP_LANDMINE_BBETTY`, `WP_LANDMINE_PGAS`)
after widening the engine weapon-inventory to 128 bits.

## Source

| Media | Jaymod path | Jaymod revision |
| --- | --- | --- |
| M97 shotgun models (`v_m97*.md3`, `m97_3rd.md3`, `m97_pickup.md3`, skins, TGAs, `weapon.cfg`) | `pak/models/weapons2/m97/` | `bbe44e0cca806d6b51260d48d4c11eab2b85bfe9` |
| Shotgun shell model | `pak/models/weapons2/shells/shotgun.md3` | same |
| Landmine bouncing-betty models (skins, markers, icon) | `pak/models/multiplayer/landmine_bbetty/` | same |
| Landmine poison-gas models (skins, markers, icon) | `pak/models/multiplayer/landmine_pgas/` | same |
| Poison-gas cannister models (skins, icon) | `pak/models/multiplayer/poison_gas/` | same |
| Poison fluid shader | `pak/models/multiplayer/poison/fluid.tga` | same |
| M97 weapon sounds (`shotgun_fire.wav`, `shotgun_far.wav`, `shotgun_fire_last.wav`, `shotgun_reload.wav`, `shotgun_reloadpump.wav`, `shotgun_cock.wav`) | `pak/sound/weapons/m97/` | same |
| Poison-gas cough sound | `pak/sound/poison_gas/cough.wav` | same |
| Weapon definitions (`.weap`) | `pak/weapons/{m97,landmine,landmine_bbetty,landmine_pgas,poison_gas}.weap` | same |

## Inherited NQ models

The new `.weap` files reference some models that are inherited from the
No Quarter base game (not Jaymod additions). These are provided by the
compiled base-game pk3 files and are not shipped in this source tree:

- `models/multiplayer/landmine/*` (base landmine geometry, `weapon.cfg`)
- `models/multiplayer/smokebomb/*` (smoke-bomb geometry used by poison gas)
- `models/weapons2/mp40/v_mp40_flash.mdc` (muzzle flash, also referenced by NQ's own `mp40.weap`)
- `models/multiplayer/mine_marker/*.md3` (mine markers, also referenced by NQ's own `landmine.weap`)

## License

Jaymod attributes its source and media to Bud Byrd and Mike Miscevic and
states the Apache License, Version 2.0 plus the original id Software terms.
The full Jaymod license text is retained at `licenses/Jaymod-LICENSE.txt`.

The imported `.md3` geometry, `.skin`, `.tga`, `.jpg`, `.wav`, and `.weap`
files are Jaymod-origin game media. They are distributed here under the same
terms that apply to the Jaymod source snapshot NewMod consults, pending the
licensing review recorded in `PROVENANCE.md` and `LEGAL`. The NQ base models
that some `.weap` files reference remain subject to the NQ media notice at
`licenses/NoQuarter-media-readme.txt`.

## Wiring

- `WP_M97` (weapon ID 64): 11-pellet shotgun, fired via `Weapon_M97` with
  `EV_M97` client event for pellet-spread tracers.
- `WP_POISON_GAS` (weapon ID 65): thrown grenade that spawns a poison-gas
  cloud via `G_PoisonGasExplode`, applying periodic `MOD_POISON_GAS` radius
  damage every 1500ms.
- `WP_LANDMINE_BBETTY` (weapon ID 66): landmine variant that leaps into the
  air before exploding via `G_BouncingBettyTrip`.
- `WP_LANDMINE_PGAS` (weapon ID 67): landmine variant that spawns a
  poison-gas cloud via `G_PoisonGasTrip` when triggered.
