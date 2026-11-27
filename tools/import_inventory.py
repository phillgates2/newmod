#!/usr/bin/env python3
"""Generate a name/ID inventory from the pinned source checkouts in PROVENANCE.

NewMod, Arena.ai, 2026-11-21. This is a static, preprocessor-independent scan,
not a parity or security test. It intentionally reports conditional entries and
server-forwarded client names. Acquire the three pinned upstream sources
separately; the NewMod tree is scanned locally. See docs/import-inventory.md
for limitations.
"""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
LEX = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/)', re.S)
PINNED = {
    "noquarter": "a56829de1368e8da1bdf93acdeae43bddeed790e",
    "etoz": "4975d1eda78a8933745d5876c2d96084f0f6a6ff",
    "jaymod": "bbe44e0cca806d6b51260d48d4c11eab2b85bfe9",
}


def uncomment(text):
    # C replaces a comment with whitespace; deleting it can merge tokens.
    def replace(match):
        token = match[0]
        return " " + "\n" * token.count("\n") if token.startswith(("//", "/*")) else token
    return LEX.sub(replace, text)


def verify_checkout(path, expected):
    """Refuse to label a different or modified checkout as the pinned source."""
    def git(*args):
        result = subprocess.run(["git", "-C", str(path), *args], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode:
            raise ValueError(f"cannot inspect {path}: {result.stderr.strip() or 'git failed'}")
        return result.stdout.strip()
    if not path.is_dir():
        raise ValueError(f"missing source checkout: {path}")
    if Path(git("rev-parse", "--show-toplevel")).resolve() != path.resolve():
        raise ValueError(f"{path} is not a checkout root")
    actual = git("rev-parse", "HEAD")
    if actual != expected:
        raise ValueError(f"{path} is at {actual}, expected {expected}")
    if git("status", "--porcelain", "--untracked-files=all"):
        raise ValueError(f"{path} has modified or untracked files; use a clean pinned checkout")


def read(path):
    return uncomment(path.read_text(errors="replace"))


def section(text, marker):
    """Return a C brace-delimited function/initializer, ignoring literals."""
    match = re.search(marker, text)
    if not match:
        raise ValueError(f"not found: {marker}")
    pos = text.index("{", match.end())
    depth = 0
    token = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')
    for match in token.finditer(text, pos):
        if match[0] == "{":
            depth += 1
        elif match[0] == "}":
            depth -= 1
            if not depth:
                return text[pos:match.end()]
    raise ValueError(f"unclosed: {marker}")


def c_table(text, marker):
    table = section(text, marker)
    return {n.lower() for n in re.findall(r'\{\s*"([^"]+)"\s*,', table) if n}


def shrub(path):
    table = section(read(path), r'static\s+const\s+struct\s+g_shrubbot_cmd\s+g_shrubbot_cmds\s*\[\s*\]\s*=')
    names = {n.lower() for n in re.findall(r'\{\s*"([a-zA-Z0-9_]+)"\s*,', table)}
    names.update(n.lower() + "war" for n in re.findall(r'WARCOMMAND\s*\(\s*([a-zA-Z0-9_]+)\s*\)', table))
    return names


def jay_shrub(path):
    result = set()
    for source in path.glob("*.cpp"):
        result.update(name.lower() for name in re.findall(r'AbstractBuiltin\s*\(\s*"([^"]+)"', read(source)))
    return result


def qcompare_names(body):
    # Both !Q_stricmp(cmd, "x") and Q_stricmp (cmd, "x") == 0.
    return {x.lower() for x in re.findall(r'Q_stricmp\s*\(\s*cmd\s*,\s*"([^"]+)"', body)}


def hash_value(name):
    return sum(ord(char.lower()) * (119 + index) for index, char in enumerate(name))


def game_client(path, bg_header):
    body = section(read(path), r'void\s+ClientCommand\s*\(\s*int\s+clientNum\s*\)')
    names = qcompare_names(body)
    constants = dict(re.findall(r'^\s*#define\s+(\w+)_HASH\s+([^\n]+)', read(bg_header), re.M))
    for macro in re.findall(r'case\s+(\w+)_HASH\s*:', body):
        name = macro.lower()
        # Avoid silently equating a macro or a numeric fragment of an
        # expression with a differently spelled command.
        value = constants.get(macro, "").strip()
        if not re.fullmatch(r'\d+', value) or hash_value(name) != int(value):
            raise ValueError(f"unknown command hash spelling: {path}: {macro}")
        names.add(name)
    return names


def osp(path):
    table = section(read(path), r'static\s+const\s+cmd_reference_t\s+aCommandInfo\s*\[\s*\]\s*=')
    # A NULL handler is only advertised; it is not handled by G_commandCheck.
    return {name.lower() for name, handler in re.findall(
        r'\{\s*"([^"]+)"\s*,\s*\w+\s*,\s*\w+\s*,\s*([^,]+),', table)
            if handler.strip() != "NULL"}


def game_console(path):
    return qcompare_names(section(read(path), r'qboolean\s+ConsoleCommand\s*\(\s*void\s*\)'))


def client_console(path):
    text = read(path)
    local = c_table(text, r'static\s+consoleCommand_t\s+commands\s*\[\s*\]\s*=')
    forwarded = {n.lower() for n in re.findall(r'trap_AddCommand\s*\(\s*"([^"]+)"', text)}
    return local, forwarded


def weapons(path):
    text = read(path)
    start = text.index("WP_NONE")
    end = text.index("} weapon_t", start)
    enum = text[start:end]
    values = {}
    value = -1
    for item in enum.split(","):
        match = re.match(r'\s*(WP_[A-Z0-9_]+|VERYBIGEXPLOSION)\s*(?:=\s*(\d+))?\s*$', item)
        if not match:
            raise ValueError(f"could not parse weapon enum item in {path}: {item[:100]!r}")
        value = int(match[2]) if match[2] else value + 1
        if match[1] != "WP_NUM_WEAPONS":
            values[match[1]] = value
    return values


def fmt_set(names):
    return ", ".join(f"`{name}`" for name in sorted(names)) or "None found in scanned surface."


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, default=ROOT / ".arena/upstreams")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/import-inventory.md")
    args = parser.parse_args()
    for name, revision in PINNED.items():
        try:
            verify_checkout(args.sources / name, revision)
        except ValueError as error:
            parser.error(str(error))
    nq, oz, jay = (args.sources / "noquarter/trunk/src", args.sources / "etoz/trunk/src", args.sources / "jaymod/src")
    mods = {
        "NQ": nq, "Oz": oz, "Jaymod": jay, "NewMod": ROOT / "src",
    }
    shrubs = {"NQ": shrub(nq / "game/g_shrubbot.c"), "Oz": shrub(oz / "game/g_shrubbot.c"),
              "Jaymod": jay_shrub(jay / "game/cmd"), "NewMod": shrub(ROOT / "src/game/g_shrubbot.c")}
    weapon_tables = {name: weapons(base / ("bgame/bg_public.h" if name == "Jaymod" else "game/bg_public.h"))
                     for name, base in mods.items()}
    client_handlers, osp_handlers, consoles, local, forwarded = {}, {}, {}, {}, {}
    for name, base in mods.items():
        suffix = "cpp" if name == "Jaymod" else "c"
        client_handlers[name] = game_client(base / f"game/g_cmds.{suffix}",
                                            base / ("bgame/bg_public.h" if name == "Jaymod" else "game/bg_public.h"))
        osp_handlers[name] = osp(base / f"game/g_cmds_ext.{suffix}")
        consoles[name] = game_console(base / f"game/g_svcmds.{suffix}")
        local[name], forwarded[name] = client_console(base / f"cgame/cg_consolecmds.{suffix}")

    out = ["# Cross-mod import inventory", "", "Generated by `tools/import_inventory.py` on 2026-11-21 from the pinned",
           "revisions in [PROVENANCE](../PROVENANCE.md). This is a **name/ID scan, not",
           "a behavioral parity claim**. A matched name can have different arguments,",
           "permissions, persistence, protocol, timing or effects. Conditional `#if`",
           "entries are included even when this Makefile disables them; dynamic Lua,",
           "server-defined shrub commands, UI scripts, map scripts, votes and engine",
           "commands are not exhaustively enumerated. The game-client surface also",
           "includes OSP handlers; cgame registrations may merely forward to a server.", ""]
    out += ["## Administrative command names", "", "| Name | NQ | Oz | Jaymod | NewMod |", "| --- | --- | --- | --- | --- |"]
    for name in sorted(set().union(*shrubs.values())):
        out.append("| `!" + name + "` | " + " | ".join("yes" if name in shrubs[mod] else "—" for mod in mods) + " |")
    missing = (shrubs["Oz"] | shrubs["Jaymod"]) - shrubs["NewMod"]
    out += ["", "Upstream admin names absent from the NewMod table: " + fmt_set(missing), "",
            "Partial adaptations include `!seen`, `!userlist`, `!userinfo`, `!page`,",
            "`!banlist`, `!baninfo`, `!levlist`, `!levinfo`, `!status`, `!spree`,",
            "`!tspree` and `!spreerecord`; they use NQ records and do not implement",
            "Jaymod/Oz databases, historical fields or exact ACL semantics. `!warn`, `!dewarn` and",
            "`/warnings` use a bounded connection-local cache; they do not implement",
            "Oz persistent warnings, decay or punishment. `!spawn` is a restricted",
            "limbo-only reinforcement adapter, not Oz's direct respawn; it bypasses",
            "wave/death-animation waits and rejects paused or transition states.",
            "`clientcmd` is conditional.", ""]
    out += ["## Weapons and network IDs", "", "IDs are local enum positions, not a translation table. `VERYBIGEXPLOSION`,",
            "dummy IDs and deployed/alternate modes are included because they consume",
            "or affect shared slots. Equal names and a similar item are **not** proof of",
            "equivalent firing, damage, media, animations, selection or network behavior.", "",
            "| Enum | NQ | Oz | Jaymod | NewMod |", "| --- | ---: | ---: | ---: | ---: |"]
    for name in sorted(set().union(*(set(t) for t in weapon_tables.values()))):
        out.append("| `" + name + "` | " + " | ".join(str(weapon_tables[mod].get(name, "—")) for mod in mods) + " |")
    out += ["", "The experimental shared adrenaline weapon is NewMod ID 63 and is not",
            "Jaymod parity. The NQ-derived shared player state has two 32-bit inventory",
            "words and 64 ammo/clip slots. Do not add IDs past 63 or renumber shared",
            "structures without a complete engine, client, save/demo and bot audit.", ""]
    for title, sets in [("Server handling of player/client commands (ClientCommand + non-NULL OSP handlers)",
                         {n: client_handlers[n] | osp_handlers[n] for n in mods}),
                        ("Dedicated server console command comparisons", consoles),
                        ("Client-side local console command comparisons", local),
                        ("Client-registered forwarded command comparisons", forwarded)]:
        out += ["## " + title, "", "Name differences only, case-insensitive. These lists include optional code",
                "and may omit registration/dispatch implemented elsewhere.", ""]
        for name in ("NQ", "Oz", "Jaymod"):
            out += [f"- {name} names in the scanned surface but absent from NewMod's corresponding surface: "
                    + fmt_set(sets[name] - sets["NewMod"])]
        out += [""]
    out += ["## Remaining integration work", "",
            "1. For every shared and missing command, compare arguments, flag/ACL checks,",
            "   moderation hierarchy, storage, lifecycle, remote surfaces and output;",
            "   add differential tests rather than treating this name table as done.",
            "2. Design durable Jaymod user/level/ban/ACL migration and Oz aliases,",
            "   warning, rating/ATB and spree storage; document privacy/retention and",
            "   atomic writes. Do not equate client-supplied GUIDs with authentication.",
            "3. Review Oz `cvarget`, `cvarset`, `pconexec` and conditional NQ",
            "   `clientcmd` for consent, authority, injection, disclosure and abuse. Do",
            "   not import unrestricted remote execution just to populate names.",
            "4. Specify a wire-compatible representation for all additional weapons,",
            "   keeping engine ABI and existing IDs intact. Integrate game, movement,",
            "   prediction, snapshots, item indexes, UI, stats, bots, bots' translation,",
            "   demos and death/respawn together; obtain authorized media.",
            "5. Run real clients and multiplayer, restart/reconnect, spectators/limbo,",
            "   packet-loss prediction, sound, downloads and demo playback. Synthetic",
            "   fixtures and successful dedicated-server startup do not prove these.", ""]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(out))
    print(f"Wrote {args.output}; scanned {len(missing)} missing upstream admin names.")


if __name__ == "__main__":
    main()
