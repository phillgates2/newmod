#!/usr/bin/env python3
"""Optional synthetic ET: Legacy dedicated-server smoke test.

NewMod, Arena.ai, 2026-11-21. This creates disposable stand-ins, not game
media or a playable map. It checks selected initialization, Lua, command,
gravity and restart paths without connecting any clients. The ET: Legacy
engine must be supplied separately; the normal unit suite does not need it.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def make_empty_bsp():
    """A deliberately empty ET BSP with just enough structure to load."""
    lumps = [b""] * 17
    lumps[0] = (
        b'{\n"classname" "worldspawn"\n"message" "NewMod smoke test"\n}\n'
        b'{\n"classname" "script_multiplayer"\n"scriptname" "game_manager"\n}\n'
        b'{\n"classname" "info_player_intermission"\n"origin" "0 0 128"\n}\n\0'
    )
    lumps[1] = struct.pack("<64sii", b"textures/common/caulk", 0, 0)
    lumps[2] = struct.pack("<4f", 0, 0, 1, 0)
    lumps[3] = struct.pack("<9i", 0, -1, -1, -4096, -4096, -4096, 4096, 4096, 4096)
    lumps[4] = struct.pack("<12i", 0, 0, -4096, -4096, -4096, 4096, 4096, 4096, 0, 0, 0, 0)
    lumps[7] = struct.pack("<6f4i", -4096, -4096, -4096, 4096, 4096, 4096, 0, 0, 0, 0)
    header_len = 8 + 17 * 8
    data, table = bytearray(), []
    for lump in lumps:
        while (header_len + len(data)) % 4:
            data.append(0)
        table.append((header_len + len(data), len(lump)))
        data.extend(lump)
    return b"IBSP" + struct.pack("<i", 47) + b"".join(struct.pack("<II", *x) for x in table) + data


def make_empty_tracemap():
    """A synthetic 256x256 trace map: no terrain or usable game geometry."""
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 256, 256, 32, 8)
    pixels = bytearray(b"\0\0\0\xff" * (256 * 256))
    for i, value in enumerate((0, 256, 0, 256, 0, 256)):
        struct.pack_into("<i", pixels, i * 4, value)
    return header + pixels


def create_fixture(base, bot_capable=False):
    etmain, newmod = base / "etmain", base / "newmod"
    etmain.mkdir(parents=True)
    newmod.mkdir()
    character = (
        'characterDef\n{\n mesh "models/test/empty.mdm"\n'
        ' animationGroup "animations/test.anim"\n'
        ' animationScript "animations/test.script"\n}\n'
    )
    # These files are generated loader stand-ins and must not be mistaken for
    # the real maps, models, sounds or gameplay data needed for a release.
    with zipfile.ZipFile(etmain / "pak0.pk3", "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("default.cfg", 'seta com_hunkmegs "128"\n')
        z.writestr("maps/newmod_test.bsp", make_empty_bsp())
        z.writestr("maps/newmod_test.script", "game_manager\n{\n spawn\n {\n }\n}\n")
        z.writestr("maps/newmod_test_tracemap.tga", make_empty_tracemap())
        for team in ("axis", "allied"):
            for cls in ("soldier", "medic", "engineer", "fieldops", "cvops"):
                z.writestr(f"characters/temperate/{team}/{cls}.char", character)
        z.writestr("models/test/empty.mdm", struct.pack("<4sI64s2f5I", b"MDMW", 3, b"test", 0, 0, 0, 100, 0, 100, 100))
        if bot_capable:
            # A single synthetic root bone and frame are enough for the
            # server's bot ClientBegin path. This is not a rendered model.
            mdx = struct.pack("<4sI64s6I", b"MDXW", 2, b"test", 1, 1, 96, 160, 0, 240)
            mdx += struct.pack("<13f6h", *([0.0] * 13), *([0] * 6))
            mdx += struct.pack("<64si2fI", b"root", -1, 0, 0, 0)
            z.writestr("models/test/empty.mdx", mdx)
            z.writestr("animations/test.anim", 'animgroup { animfile "models/test/empty.mdx" { idle 0 1 1 10 0 0 0 } }\n')
        else:
            z.writestr("animations/test.anim", "animgroup { }\n")
        z.writestr("animations/test.script", "DEFINES\n")
        weapon_source = (ROOT / "src/game/bg_weapons.c").read_text()
        weapon_files = set(re.findall(r'filename\s*=\s*"([^\"]+\.weap)"', weapon_source))
        if not weapon_files:
            raise RuntimeError("could not find weapon definition names")
        for name in sorted(weapon_files):
            z.writestr("weapons/" + name, "weaponDef { }\n")

    lua = (
        "local nextFrameReport = 0\n"
        "function et_InitGame(levelTime, randomSeed, restart)\n"
        "  et.G_Print('NEWMOD_LUA_INIT\\n')\nend\n"
        "function et_RunFrame(levelTime)\n"
        "  if levelTime >= nextFrameReport then\n"
        "    et.G_Print('NEWMOD_LUA_FRAME ' .. levelTime .. '\\n')\n"
        "    nextFrameReport = levelTime + 5000\n"
        "  end\nend\n"
        "function et_ConsoleCommand(command)\n"
        "  et.G_Print('NEWMOD_LUA_CONSOLE ' .. command .. '\\n')\n"
        "  return 0\nend\n"
        "function et_Quit()\n  et.G_Print('NEWMOD_LUA_QUIT\\n')\nend\n"
    ).encode()
    (newmod / "smoke.lua").write_bytes(lua)
    digest = hashlib.sha1(lua).hexdigest().upper()
    # A report spanning two pages lets the console test the actual game-map
    # initialization path: the saved query must disappear after map_restart.
    (newmod / "shrubbot.cfg").write_text("".join(
        f"[level]\nlevel = {i}\nname = SmokeLevel{i:02}\nflags = h\n\n"
        for i in range(25)
    ))
    (newmod / "smoke.cfg").write_text(
        'set lua_modules " , ; smoke.lua ;, "\n'
        f'set lua_allowedModules "{digest}"\n'
        # Keep this empty fixture in warmup: automatic match restart would be
        # queued behind the console's wait command and suspend game frames.
        'set g_doWarmup "1"\nset match_minplayers "2"\n'
    )
    return newmod


def available_udp_port():
    # Release the probe before starting the engine; --port can be supplied if
    # another process happens to claim it in the small intervening window.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path, help="ET: Legacy 2.86.2 Linux x86-64 dedicated-server executable")
    parser.add_argument("--module", type=Path, default=ROOT / "build/newmod/qagame.mp.x86_64.so")
    parser.add_argument("--port", type=int, default=0, help="local UDP port (default: choose an available port)")
    parser.add_argument("--timed", action="store_true", help="also wait about 32 seconds for an actual gravity change")
    parser.add_argument("--show-log", action="store_true", help="print the engine output on success too")
    args = parser.parse_args()
    engine, module = args.engine.resolve(), args.module.resolve()
    if not engine.is_file() or not module.is_file():
        parser.error("engine and built qagame module must both exist")
    if not 0 <= args.port <= 65535:
        parser.error("port must be between 0 and 65535")

    with tempfile.TemporaryDirectory(prefix="newmod-etl-smoke-") as temporary:
        root = Path(temporary)
        base, home = root / "base", root / "home"
        home.mkdir()
        newmod = create_fixture(base)
        shutil.copyfile(module, newmod / "qagame.mp.x86_64.so")
        port = args.port or available_udp_port()
        command = [
            str(engine),
            "+set", "fs_basepath", str(base), "+set", "fs_homepath", str(home),
            "+set", "fs_game", "newmod", "+set", "net_ip", "127.0.0.1",
            "+set", "net_port", str(port), "+set", "dedicated", "1",
            "+set", "com_recommendedSet", "1", "+set", "g_gametype", "2",
            "+set", "sv_pure", "0", "+set", "sv_fps", "20", "+set", "omnibot_enable", "0",
            "+exec", "smoke.cfg", "+map", "newmod_test", "+wait", "12",
            "+levlist", "+page", "2",
            "+set", "g_gravity", "925.50", "+crazygravity", "on", "+crazygravity",
        ]
        # The quick check simulates an intervening cvar change. The optional
        # timed check lets the actual 30-second scheduler choose a value.
        command += ["+wait", "640"] if args.timed else ["+set", "g_gravity", "600", "+wait", "12"]
        command += [
            "+g_gravity", "+crazygravity", "off", "+crazygravity", "+g_gravity",
            "+map_restart", "0", "+wait", "12", "+page", "+g_gravity", "+quit",
        ]
        try:
            proc = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, errors="replace", timeout=90)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"Engine could not complete: {exc}", file=sys.stderr)
            return 1
        output = re.sub(r"\x1b\[[0-9;]*m", "", proc.stdout)
        expected = (
            "gamename: newmod", "Lua API: Loading smoke.lua", "NEWMOD_LUA_INIT", "NEWMOD_LUA_FRAME",
            "NEWMOD_LUA_CONSOLE crazygravity", "NEWMOD_LUA_QUIT",
            "crazygravity: enabled", "crazygravity: disabled", "==== ShutdownGame ====",
        )
        problems = []
        if proc.returncode:
            problems.append(f"engine exited {proc.returncode}")
        for marker in expected:
            if marker not in output:
                problems.append(f"missing output: {marker}")
        if output.count("NEWMOD_LUA_INIT") < 2 or output.count("NEWMOD_LUA_QUIT") < 2:
            problems.append("Lua was not initialized and shut down on both sides of map_restart")
        # Require the actual second page before restart and the empty query
        # afterwards; a plain initialization smoke would not test retention.
        page_before = output.find("page 2 of 2")
        restart_at = output.find("==== ShutdownGame ====")
        page_after = output.find("no saved long report", restart_at)
        if not (0 <= page_before < restart_at < page_after):
            problems.append("console pagination was not cleared by map initialization")
        gravity_values = re.findall(r'"g_gravity"\s+is:\s+"([^\"]+)"', output)
        if len(gravity_values) != 3:
            problems.append(f"expected three gravity observations, got {gravity_values}")
        else:
            if args.timed:
                try:
                    if not 0 <= int(gravity_values[0]) < 1200 or gravity_values[0] == "925.50":
                        raise ValueError
                except ValueError:
                    problems.append(f"timed gravity did not change: {gravity_values[0]}")
            elif gravity_values[0] != "600":
                problems.append("the intervening test gravity was not applied")
            if gravity_values[1] != "925.50":
                problems.append("saved gravity was not restored exactly before restart")
            if gravity_values[2] != "800":
                problems.append("the restarted synthetic map did not set its default gravity")
        if re.search(r"\bERROR\b|Lua API: can not open file|disallowed by ACL", output, re.I):
            problems.append("engine output contains an error or the Lua fixture was rejected")
        if args.show_log or problems:
            print(output, file=sys.stderr if problems else sys.stdout)
        if problems:
            print("Smoke test failed: " + "; ".join(problems), file=sys.stderr)
            return 1
        print("ET: Legacy synthetic game/Lua/command/restart smoke passed; no clients joined.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
