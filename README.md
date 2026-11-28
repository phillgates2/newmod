# Overzealous 0.3.0-dev

Overzealous is an **experimental, incomplete** Enemy Territory mod derived from
No Quarter. It incorporates selected Jaymod- and Oz-inspired administrative
commands, not a complete merge of the three mods. The source builds native
Linux (`x86_64` and `i386`), Windows (`x64` and `x86`), and macOS (`universal`
`x86_64` + `arm64`) game, cgame and UI modules for ET: Legacy deployments;
it is **not yet a complete, production-verified release**.

No Quarter's source, the No Quarter Mod Team and its contributors are the
foundation of this work. Jaymod's Bud Byrd and Mike Miscevic, the Oz/ETPub
contributors, id Software, the Lua team and the Omni-Bot contributors are
also credited in [PROVENANCE.md](PROVENANCE.md). See [CHANGES](CHANGES) for
dated changes and [LEGAL](LEGAL) before any deployment or distribution.
Overzealous is not an official No Quarter, Jaymod, Oz or ET: Legacy release.

## What is and is not implemented

* The No Quarter game/client/UI code is the base. A selection of commands
  listed below is adapted to its existing state and permission system.
* An **experimental**, Jaymod-inspired shared-adrenaline tool and an
  Oz-inspired `/adrenother` command are available as opt-in NQ adaptations;
  neither is upstream-equivalent. Jaymod/Oz weapon inventories,
  prediction/protocols, databases and feature sets beyond the NQ base set
  have **not** been merged.
* **All NQ weapons are implemented**: 55 `.weap` definition files cover
  every weapon in the `WP_*` enum — pistols, SMGs, rifles, rifles with
  grenade launchers, the Panzerfaust, flamethrower, mortar, MG42, knife,
  dynamite, satchel, landmine, syringe, binoculars, medkit, ammo pack,
  and more. Their first-person and 3rd-person models (`.md3`/`.mdc`),
  skins, weapon configs, sound effects, and weapon icons have been
  imported from the WET-NoQuarter source into `pak/` and calibrated to
  match the `.weap` file references. The M97 shotgun, molotov fire
  grenade, adrenaline-share syringe, bouncing-betty and poison-gas
  landmines, poison-gas grenade, trip mine, and map mortar are
  Jaymod-sourced additions. NQ's
  64 weapon slots are now full; see
  [pak/MEDIA-PROVENANCE.md](pak/MEDIA-PROVENANCE.md) for asset provenance.
* `!spree` and `!tspree` use NQ's current streaks; `!spreerecord` is only a
  map-local NQ-derived high watermark, not Oz's persistent map and overall
  records. `!status -all` has no Jaymod database statistics. See the
  [cross-mod inventory](docs/import-inventory.md) for missing names and its
  limitations; even a matching name is not proof of behavioral parity.
* Synthetic ET: Legacy 2.86.2 dedicated-server fixtures have exercised map
  restart and selected bot-protocol/client-command paths using placeholder
  assets. They are not real Omni-Bot or real network clients. Client startup,
  downloads, game assets, sound delivery, multi-client gameplay, demos and
  production reliability remain unverified.
* Required Enemy Territory and compatible mod media are **not included**.
  The upstream NQ media notice prohibits use of files within NQ pk3s without
  consent. Obtain necessary permissions and licenses before packaging any
  media; a successful compile is not permission to ship it.
* Selected native ABI values, layouts and calls are tested on Linux x86-64;
  that is not a complete ABI or security audit. Do not use this development
  build on an Internet-facing production server without further review.

## Build and test

All modules compile as **C++** (g++, `-std=gnu++11`): every `.c` source is
compiled with `$(CXX) $(CXXFLAGS)` and linked with `$(CXX)`. The codebase
uses `-fpermissive` to absorb implicit enum/pointer conversions inherited
from the original C; no sources are compiled as C.

On Linux x86-64, install a C/C++ compiler, GNU Make and Python 3; then:

```sh
make -j8                       # default: Lua and Omni-Bot adapters included
make -j8 test                  # fake-engine regression harnesses
make -j8 test-matrix WITH_AUTO_GUID=0  # Lua/Omni-Bot on and off
make -j8 test-matrix WITH_AUTO_GUID=1  # repeat with experimental key fallback
make -j8 all WITH_AUTO_GUID=0          # restore default outputs
```

The outputs for a default Linux x86-64 build are
`build/overzealous/qagame.mp.x86_64.so`, `build/overzealous/cgame.mp.x86_64.so`,
and `build/overzealous/ui.mp.x86_64.so`. `build/` is generated and ignored.
`make clean` removes it.

### Multi-architecture and cross-platform builds

ET: Legacy resolves native module filenames by platform and CPU architecture
(`Sys_GetDLLName`):

* Linux 64-bit (`PLATFORM=linux ARCH=x86_64`): `<mod>.mp.x86_64.so`
* Linux 32-bit (`PLATFORM=linux ARCH=i386`): `<mod>.mp.i386.so`
* Windows 64-bit (`PLATFORM=windows ARCH=x86_64`): `<mod>_mp_x64.dll`
* Windows 32-bit (`PLATFORM=windows ARCH=i386`): `<mod>_mp_x86.dll`
* macOS Universal (`PLATFORM=macos ARCH=universal`, combining `x86_64` + `arm64`
  Apple Silicon slices): `<mod>_mac`

On Linux x86-64, cross-targets (`linux-i386`, `windows-x86_64`, `windows-i386`,
`macos-x86_64`, `macos-arm64`, `macos-universal`) compile via `zig c++` (auto-bootstrapped
into `.cache/zig-pip/` if `zig` is not already on `PATH`). Linux cross-builds
target `glibc 2.17` with static C++ runtime linkage so `qagame.mp.i386.so` runs
on 32-bit `etlded` hosts without requiring `libstdc++6:i386`.

```sh
make -j8 PLATFORM=linux ARCH=i386        # 32-bit Linux server/client (.mp.i386.so)
make -j8 PLATFORM=windows ARCH=x86_64    # 64-bit Windows (_mp_x64.dll)
make -j8 PLATFORM=windows ARCH=i386      # 32-bit Windows (_mp_x86.dll)
make -j8 PLATFORM=macos ARCH=universal   # Universal macOS x86_64+arm64 (_mac)
make -j8 pk3-all                         # Universal client pk3 + loose server VMs
make -j8 server-dist                     # build/overzealous-server-linux.tar.gz
```

`make pk3-all` builds all five targets, packs all 10 client binaries (`cgame*`
and `ui*` for Linux x86_64/i386, Windows x64/x86, and macOS universal) plus
`pak/` assets into `build/overzealous-all.pk3`, and stages all loose `qagame*`
server modules into `build/overzealous/`. `make server-dist` bundles
`overzealous-all.pk3`, `qagame.mp.i386.so`, and `qagame.mp.x86_64.so` into
`build/overzealous-server-linux.tar.gz`.

`WITH_LUA=0` and `WITH_OMNIBOT=0` disable the respective adapter, e.g.
`make -j8 test WITH_LUA=0 WITH_OMNIBOT=0`. All `WITH_*` values must be
exactly `0` or `1`.
Omni-Bot support is an interface only; the separate bot library is not
included. **Only load trusted Lua scripts**: Lua has filesystem and engine
access and is not sandboxed. The Lua API has received targeted fixes, not a
complete security audit. Lua module allowlist hashes now cover all file bytes;
previously configured hashes for binary chunks containing NUL bytes may need
updating. SHA-1 allowlisting is not a substitute for trusted code.

The inherited NQ-key XP fallback can be compiled and tested separately:

```sh
make -j8 test WITH_AUTO_GUID=1  # experimental; off by default
make -j8 all WITH_AUTO_GUID=0   # restore the default output set
```

Use matching game and cgame builds if experimenting with this fallback. It
uses a weak, client-supplied, spoofable key, **not authentication**; do not
use it to grant privileges or treat it as proof of player identity. If its key
file cannot be opened for writing, no shared fallback key is sent. The engine
API does not report short writes to this client code. The unrelated upstream
`SYSTEM_CHECK` option is not enabled or supported by this Makefile.

Build options have separate object directories, but each `make all` copies the
chosen set into `build/overzealous/`; do not run different build variants in
parallel against that shared output directory.

To reproduce the optional server smoke tests, supply an ET: Legacy 2.86.2
Linux x86-64 dedicated-server executable and build the default Lua/Omni-Bot
module:

```sh
python3 tests/etlegacy_smoke.py /path/to/etlded.x86_64
python3 tests/etlegacy_smoke.py /path/to/etlded.x86_64 --timed
python3 tests/etlegacy_bot_smoke.py /path/to/etlded.x86_64
```

The scripts create and remove temporary fixtures with synthetic stand-ins and
bind a local UDP port. The first checks game/Lua startup, console commands,
gravity restoration and restart; `--timed` exercises the 30-second scheduler.
The second compiles a small **synthetic bot-protocol fixture**, not Omni-Bot,
and exercises server-side bot lifecycle, client-chat dispatch, spawn, mute,
custom-command reload, warnings and restart. No real clients join either
test. Their empty map and placeholder assets are **not playable** or release
media. Use `--show-log` to inspect output and `--port` to select a port.

## Deployment prerequisites

For dedicated-server deployment (including LinuxGSM `etserver`, which runs the
32-bit `linux-i386` `etlded` binary by default, as well as 64-bit `etlded`),
note two ET / ET: Legacy engine rules:

1. **Dedicated servers never extract `qagame` from `.pk3` archives.** `etlded`
   loads `qagame` directly from the mod directory on disk (`<fs_homepath>/<fs_game>/`
   or `<fs_basepath>/<fs_game>/`). Placing only `.pk3` files in the mod folder
   causes `Sys_LoadDll(.../qagame.mp.i386.so)... failed: cannot open shared object file`.
2. **Architecture must match the `etlded` binary.** A 32-bit `linux-i386`
   dedicated server requires `qagame.mp.i386.so`; a 64-bit `linux-x86_64`
   dedicated server requires `qagame.mp.x86_64.so`. Shipping both loose `.so`
   files alongside `overzealous-all.pk3` allows either server binary to load the
   mod, while clients on Linux (32/64-bit), Windows (32/64-bit), and macOS
   (Intel/Apple Silicon) extract their matching `cgame`/`ui` modules from
   `overzealous-all.pk3`.

Example Linux dedicated-server deployment into a mod folder (such as
`overzealous` or `ozmod`; `etmain` is rejected):

```sh
tar -xzf build/overzealous-server-linux.tar.gz -C /path/to/server/ozmod/
# Ensures ozmod/ contains:
#   overzealous-all.pk3
#   qagame.mp.i386.so
#   qagame.mp.x86_64.so
```

The game advertises `gamename=overzealous`, `mod_version=0.3.0-dev` and
`GAME_VERSION` `Overzealous 0.3`; an NQ, Jaymod or Oz client is not a matching
Overzealous client. Respect the original engine/version checks and the license
conditions in `LEGAL`; do not bypass them to make incompatible clients load.

Do not distribute NQ pk3 contents or other assets without the necessary
consent. The upstream package checks still issue NQ media warnings; they are
not proof that all required or authorized assets are installed. Verify the
server logs, because an engine process can exit successfully after an error.

## Experimental teammate adrenaline

Both features are disabled by the inherited `g_medics` default of `65`. Bits
are combined with the server's existing settings; these numbers intentionally
**do not** match Jaymod/Oz because their bits conflict with NQ settings.

* `g_medics` bit `8192` enables `WP_ADRENALINE_SHARE` (Overzealous ID 63). An
  active level-4 medic also needs NQ's `g_adrenaline` medic bit `256`, the
  self-adrenaline weapon, and war mode off. The tool is an alternate to
  self-adrenaline and shares its needle pool and NQ charge timing. It applies
  NQ adrenaline to an eligible living teammate in syringe range; a miss or
  rejected target refunds a needle consumed by movement. Matching Overzealous
  game/cgame builds are required, including when this feature is disabled:
  movement fire-event tagging also affects ordinary weapons. Bot behavior,
  packet-loss corrections, effects and real-client gameplay are unverified.
* `g_medics` bit `16384` enables `/adrenother`: a level-4 medic holding the
  revival syringe can apply NQ adrenaline to an eligible teammate, consuming
  syringe ammo and a full medic charge (subject to NQ insanity settings).
  This is a stricter adaptation, not Oz's full implementation.
* For example, preserving the default `65`, `g_medics 8257` enables only the
  shared tool; `g_medics 24641` enables both. Set `g_adrenaline 256` to enable
  NQ medic adrenaline for the shared tool, combining any other desired bits.
  These examples are for controlled experiments, not deployment guidance.
* `/knives` reports the caller's current NQ team-specific throwing-knife clip;
  it is unavailable while spectating, following or in limbo. It is not Oz's
  full throwing-knife implementation.

## Admin system

The shrubbot admin system has been extended with **fine-grained permission
flags** so individual privileges can be granted or denied independently of
the existing level hierarchy. Seven new `SBF_CAN_*` flags control whether
an admin of a given level may:

| Flag | Grants |
| --- | --- |
| `SBF_CAN_KICK` | Kick players |
| `SBF_CAN_MUTE` | Mute players |
| `SBF_CAN_WARN` | Issue warnings |
| `SBF_CAN_UNBAN` | Remove bans |
| `SBF_CAN_VIEW_USERINFO` | View detailed userinfo (GUID/IP) |
| `SBF_CAN_SETLEVEL` | Change player levels |
| `SBF_CAN_PUTTEAM` | Move players between teams |

The permission check is **deny-only**: a flag that is absent from an
admin's config does not restrict them (the default is allow). A flag
that is present denies the action. The server console is always permitted
regardless of flags. Existing `SBF_*` flags (`SBF_IMMUNITY`, `SBF_NOVOTE`,
etc.) are unchanged.

Two new console commands manage the engine IP-ban list:

* `/ban <name|client#> [reason]` — temporarily ban a player via
  `Svcmd_Ban_f`, creating a shrubbot temp-ban record with a reason.
* `/unban <ban#>` — remove a ban by its shrubbot ban number via
  `Svcmd_Unban_f`, expiring it immediately and writing the config.

`G_nm_firegod` accepts an optional duration argument:
`!firegod [slot-or-name] [duration|noclip]`. A positive integer in the
third argument sets a duration in seconds (clamped to 3600) for flaming
god mode; `noclip` (or no third argument) retains the legacy toggle.

## Bot callvote commands

Two bot-related commands are available in the normal `!vote` list when
Omni-Bot support is compiled in (`WITH_OMNIBOT=1`):

| Command | Behavior |
| --- | --- |
| `!vote bots <on\|off>` | Toggles `omnibot_enable`, turning bot presence on or off. |
| `!vote difficulty <0-100>` | Sets `g_botDifficulty` (default 50), controlling bot skill level. |

Both are gated by permission cvars: `vote_allow_bots` and
`vote_allow_difficulty` (both default to `1`). Referees can always
use these commands regardless of the cvar. Bot difficulty applies to
`client->sess.skill[SK_*]` levels used by the Omni-Bot interface.

## Selected commands

These use the existing NQ shrubbot dispatcher, level hierarchy and flags.
`g_shrubbot` defaults to `shrubbot.cfg` and must not be empty; a missing file
loads NQ's limited default levels. Configure the appropriate flags in the
shrubbot configuration. Destructive commands marked **tyranny** also require
`g_tyranny`; immutable and higher-level targets are protected in the new
mass actions. Permissions are case-sensitive.

| Command | Flag | Behavior |
| --- | --- | --- |
| `!putteam`, `!lol` | `p`, `x` | Aliases for NQ `!put` and `!nade`; `!lol` requires tyranny. |
| `!giba` | `Q` | Gib eligible players; tyranny. |
| `!flinga`, `!throwa`, `!launcha` | `L` | Mass NQ fling/throw/launch; tyranny. |
| `!panzerwar`, `!sniperwar`, `!riflewar` `[on\|off]` | `q` | Query or toggle the corresponding NQ `nq_War` bits. |
| `!spec <slot-or-name>` | `Q` | Follow an eligible player, subject to spectator locks. |
| `!crazygravity [on\|off]` | `q` | Warn after 25 seconds, choose gravity 0–1199 after 30, and restore the prior cvar string on off or shutdown. A new map can subsequently set its own gravity. |
| `!shake [slot-or-name] [seconds]` | `A` | Targeted camera shake, 1–60 seconds; tyranny. |
| `!spawn [slot-or-name\|all\|-1]` | `%` | Immediately reinforce eligible active-team players **already in limbo**; no argument means all. Tyranny. Not Oz's direct respawn. |
| `!splat <slot-or-name>`, `!splata` | `g`, `Q` | Apply splat damage to eligible player(s); tyranny. Splat bypasses spawn invulnerability, not god mode or noclip. |
| `!smite [slot-or-name]` | `U` | Apply NQ fire to eligible players; tyranny. |
| `!firegod [slot-or-name] [duration\|noclip]` | `e` | Toggle flaming god mode for a player: a new fire grants invulnerability for the burn duration, a repeat toggles it back to mortal damage. The optional third argument sets a duration in seconds (clamped to 3600) or `noclip` for the legacy toggle; tyranny. Adapted from Oz; NQ applies burn damage from its short timer, so it is not Oz's separate timer model. |
| `!news [map]` | `W` | Play an existing `sound/vo/<map>/news_<map>.wav`, with a 10-second cooldown and resource checks. |
| `!about`, `!status [-all]`, `!lsplayers`, `!spree`, `!tspree [count]` | `h`, `i`, `i`, `h`, `h` | Display selected mod, player, resource or current-streak information. |
| `!spreerecord` | `E` | NQ-derived map-local high watermark recorded on enemy kills during play; resets on map init. Warmup streaks may contribute to an NQ streak carried into play. |
| `!stats`, `!ftime` | `t`, `C` | NQ MP40/Thompson accuracy and server-local time. |
| `!chicken`, `!crybaby` | `W` | Play a specified Jaymod sound if separately installed and available; these assets are not bundled. |
| `!banlist`, `!baninfo`, `!levlist`, `!levinfo` | `B`, `B`, `s`, `s` | Display NQ ban/level records; `!baninfo` reveals GUID/IP only with the separate `f` flag. |
| `!userinfo`, `!userlist`, `!page`, `!seen` | `e`, `s`, `h`, `i` | Limited NQ-backed player/admin reports, not Jaymod's user database. |
| `!warn <slot-or-name> <reason>`, `!dewarn <slot-or-name> [number]` | `R` | Warn, list, or remove reasons from a bounded, connection-local cache. `/warnings` shows only the caller's cache. |
| `!report <slot-or-name> <reason>` | `h` | Write a report and notify eligible admins; see privacy notes below. |

`!spawn` uses NQ's reinforcement path, checks hierarchy and immutable targets,
works in warmup/countdown/waiting and ordinary play, and refuses paused or
unpausing matches, intermission, restart/transition states and playing LMS
rounds. It deliberately bypasses the reinforcement wave and `respawnTime`
waits, including the usual 800 ms death-animation delay; use it only when that
interruption is acceptable. The phase and timing checks have synthetic tests,
not real multiplayer or objective/map-script validation.

Warning history stores at most eight sanitized, 159-byte reasons per client.
Additional warnings are delivered but not saved. The cache resets on connect,
disconnect and map initialization; it has no Oz persistence, decay or
automatic punishment. **Warning text is not private:** `!warn` typed in
ordinary chat is normally broadcast, and `g_logAdmin` may retain command
arguments independently of the cache. Lua hooks, engine logs, demos, and other
plugins may also record them; restrict access and retention. The module's
admin-log formatting removes control characters to keep one line per entry,
but does not redact warning text or protect other logs.

`!userlist` filters only by `-name` and `-level`; `!seen` covers visible NQ
admin records and up to 128 recent disconnects on this map, not every player.
It holds a GUID, sanitized name and disconnect time in memory until eviction
or map reset. GUIDs are spoofable lookup keys, not authentication. `!page`
rebuilds a live report and rechecks permissions, retaining a normalized name
filter in memory until replaced, disconnect or map init. Treat these filters
and NQ records as potentially identifying information.

`!report` can also be sent directly as a client console command. For names
containing spaces, use a slot number. It accepts at most three reports per map
connection. `g_ReportFile` defaults to `reports.log` in the engine's writable
game filesystem; set it to an empty string to disable reports.
`g_viewreport_level` defaults to `5`: connected admins must have a positive
level and meet the threshold to receive a notification. Reports contain
names, GUIDs and reasons; restrict access to and retention of that file.

**Privacy limitation:** Overzealous intercepts report commands before its own chat,
admin log and Lua client-command hook. ET: Legacy can log or record the raw
client command in engine debug logs or server-side demos *before* the game
module receives it, and other plugins or proxies may also retain it. This is
not end-to-end private messaging; protect those logs and recordings as well.

## Shrubbot configuration compatibility and security

Treat the shrubbot file as **privileged server configuration**. Custom
`[command]` templates execute through the server console; their configured
commands do not undergo nested users' permission checks. Only trusted
operators should be able to edit the file or trigger a reload.

The loader stages a new snapshot and keeps the previous in-memory levels,
admins, bans, mutes and commands if reading or parsing fails. A missing file
on a fresh install loads NQ's limited defaults; an invalid file with no prior
snapshot does not. The file limit is 32 MiB, fields must fit their target
buffers, integers must be valid 32-bit values and a command's `levels` list
may contain at most 32 nonnegative numbers. Unknown fields/sections,
unterminated quotes or block comments, oversized tokens and embedded NULs are
rejected rather than partially loaded. Ignored `nq_noq` sections do not
modify the preceding record. A zero-filled short read is detected, but the
engine read API does not report an actual byte count, so not every read
failure can be detected. Review old configs and back them up before migration.

The inherited writer is still **not an atomic, checked disk transaction**:
I/O failures or interruption can damage the on-disk file. Back it up. To
prevent values becoming config syntax, written names and other data fields
are quoted; control bytes and backslashes become spaces and double quotes
become apostrophes. Trusted `exec` templates preserve paired quotes, but the
writer refuses to open the file if a template has unbalanced quotes, control
bytes or a trailing backslash; watch the server console for that error.

Custom `[1]`–`[9]` arguments and enabled `g_shortcuts` substitutions are
bounded and checked before being appended to a trusted template. Unsafe
separators, quotes, controls, backslashes, comment boundaries, malformed or
overlong UTF-8, unquoted substituted spaces and oversized expansions are
rejected. Configured semicolon-separated commands are still allowed; these
checks do not make an untrusted template safe. ET: Legacy may sanitize a
client's semicolons before this module sees the command. `COM_ParseExt` also
now treats high bytes as token bytes, discards oversized tokens and rejects
unterminated quoted strings/comments in all three modules. This changes
compatibility beyond shrubbot and has not been validated against every asset.

NQ mute records are restored by nonempty GUID rather than historical display
name, including after a rename. A successful reload clears an absent/expired
purely shrub-owned mute; other or combined mute sources are retained
conservatively because NQ has only one timer. GUIDs remain spoofable lookup
keys, not authentication, and these inherited mute records are separate from
the connection-local warning cache described above.

NQ XP save remains disabled by default (`g_XPSave 0`). For experiments with
it, configure an existing writable `g_XPSaveDirectory`; normal saves reject
unsafe GUID filename components. A server using the optional NQ-key fallback
also needs the corresponding directory with the `_nq` suffix. Back up XP
files before testing or migrating; the inherited binary save format is
platform-dependent.

## Tests and release checklist

The suite exercises selected syscall marshalling and layouts, 64-player team
packet limits, parser boundaries in all three modules, full-capacity shrubbot
lists and reload failures, custom templates, mute restoration, reports,
command permissions, warning lifecycle, gravity, death-handler paths, UI
bounds, Lua, XP paths and optional key packets. It also exercises shared
adrenaline and weapon prediction/event paths with fake engine callbacks, the
actual `!firegod` toggle (including the duration argument) and the actual
ban-creation handlers (duration saturation, GUID length, IPv4 validation,
temporary-ban duration and host/bot/hierarchy rejection). The populated
warning/seen map-init test deliberately interrupts the initializer after its
entry resets; it is not a completed map load. The optional engine fixtures
complete a restart using unplayable stand-ins and synthetic bots, and
exercise actual mute/unmute records across a reload. The build matrix runs
all four Lua/Omni-Bot combinations. These are **not** end-to-end
multiplayer tests.

To regenerate [the inventory](docs/import-inventory.md), acquire clean Git
checkouts at the revisions in [PROVENANCE.md](PROVENANCE.md) as
`.arena/upstreams/noquarter`, `.arena/upstreams/etoz` and
`.arena/upstreams/jaymod`, then run `python3 tools/import_inventory.py`.
The scanner verifies the checked-out revisions and reports name/ID differences
only; conditional or dynamically registered surfaces may be missed.

Before a release: finish remaining feature work, review inherited
code and Lua for security, validate real compatible assets and actual clients,
run gameplay/download/demo tests, resolve all licensing and provenance issues,
retain required notices, make source available and notify the relevant
licensors as required. See [LEGAL](LEGAL) and [PROVENANCE.md](PROVENANCE.md).
