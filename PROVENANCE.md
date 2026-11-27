# Provenance and acknowledgements

NewMod is derived directly from No Quarter source by the No Quarter Mod Team
and contributors; it is a distinct, experimental derivative, not their
product or an official endorsement. The source history and author comments
in inherited files are retained. NewMod changes in this workspace are by
Arena.ai Agent Mode, dated 2026-11-21; see [CHANGES](CHANGES).

## Inspected source baselines

| Project | Source/revision | Use in this repository |
| --- | --- | --- |
| WET-NoQuarter | <https://github.com/core-c/WET-NoQuarter>, `a56829de1368e8da1bdf93acdeae43bddeed790e`, `trunk/` | Main source base for `src/game`, `src/cgame`, `src/ui`, `src/sha-1`, `Omnibot/` and `etmain/ui/`. Some files are modified; `CHANGES` identifies them. No NQ media pk3 is included. |
| Jaymod | <https://github.com/budjb/jaymod>, `bbe44e0cca806d6b51260d48d4c11eab2b85bfe9` | Source consulted for command behavior; a subset of its `src/lua/` supplies the bundled Lua 5.1.4 interpreter. Jaymod attributes its source to Bud Byrd and Mike Miscevic and states Apache 2.0 plus original id terms. This is not the Jaymod database or a full Jaymod port. |
| Oz / ETOz | <https://github.com/phillgates2/etoz>, `4975d1eda78a8933745d5876c2d96084f0f6a6ff`, `trunk/` | Source consulted for selected command behavior. The implementation is adapted to NQ; its ratings, statistics and persistent spree systems are not imported. Its inspected SDK license text is retained. |
| ET: Legacy | <https://github.com/etlegacy/etlegacy>, v2.86.2 `fdb40f8706b4e5081bf721cdc5726b2791a6e1f9`; `etlegacy-libs` submodule `ebd7680c7f5c6675fdc3f4f61fa0911e7b6d2b44` | Separate engine checkout used to inspect the native ABI and build a dedicated-server smoke test. Neither that engine source nor its binaries are bundled here. Selected ABI values/layouts are captured in `tests/etl_abi_layout.c`. |

The NQ upstream credits its team, Omni-Bot developers and other contributors
in `licenses/NoQuarter-media-readme.txt`. The inherited Lua interface
`src/game/g_lua.c` itself credits ETPub, especially quad and pheno, and IRATA.
The inherited shrubbot source credits Tony J. White. Preserve the many other
notices and names in the original source headers.

`src/lua/` is Lua 5.1.4 from the Jaymod snapshot, with the Lua.org/PUC-Rio
permission notice in `licenses/Lua-5.1.txt` and `src/lua/lua.h`. The inherited
SHA-1 implementation credits Paul E. Jones, with its notice at
`src/sha-1/license.txt` and `licenses/SHA1-Paul-E-Jones.txt`.

The optional NQ-key code uses a seeded implementation of the MD5
message-digest algorithm, derived from the RSA Data Security, Inc. MD5 Message
Digest Algorithm; the implementation at `src/cgame/md5.c` credits Colin Plumb
and its header retains the RSA notice. MD5 and the inherited random key
construction are not suitable for authentication.

## Legal status

The exact retained license/notice files are catalogued in [LEGAL](LEGAL). The
NQ source license and the terms for Jaymod, the id SDK, the Lua runtime,
Omni-Bot and separate assets must be reviewed together. We have **not**
concluded that all redistribution conditions are satisfied, obtained media
consent, notified the NQ licensor or published a guaranteed source download.
No third-party endorsement or ownership transfer is implied.
