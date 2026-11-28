#!/usr/bin/env python3
"""Optional ET: Legacy server smoke with synthetic bots, not real clients.

Overzealous, Arena.ai, 2026-11-27. Compiles a disposable Omni-Bot ABI fixture,
creates an empty map and placeholder data, then forces game state and invokes
synthetic Lua damage. It tests selected lifecycle/dispatch paths only; it
is not an actual bot library, playable game, prediction or multiplayer test.
Requires a separately supplied ET: Legacy 2.86.2 dedicated-server executable,
a C++ compiler and the default Overzealous build with Lua and Omni-Bot enabled.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

from etlegacy_smoke import ROOT, available_udp_port, create_fixture


LUA = r'''
local savedSpawnCount = nil
local savedScore = nil
local savedThirdCount = nil
local savedThirdScore = nil
local savedThirdRespawnTime = nil
local function inLimbo(n)
  -- NQ PMF_LIMBO is bit 14. Lua 5.1 has no built-in bit library.
  return math.floor(et.gentity_get(n, 'ps.pm_flags') / 16384) % 2 == 1
end
function et_InitGame() et.G_Print('FIXTURE_LUA_INIT\n') end
function et_ClientConnect(n, first, bot)
  et.G_Print('FIXTURE_CONNECT ' .. n .. ' ' .. first .. ' ' .. bot .. '\n')
  if bot == 1 then
    local id = n == 1 and 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' or
               n == 2 and 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' or 'cccccccccccccccccccccccccccccccc'
    local info = et.Info_SetValueForKey(et.trap_GetUserinfo(n), 'cl_guid', id)
    et.trap_SetUserinfo(n, info)
  end
end
function et_ClientBegin(n) et.G_Print('FIXTURE_BEGIN ' .. n .. '\n') end
function et_ClientDisconnect(n) et.G_Print('FIXTURE_DISCONNECT ' .. n .. '\n') end
function et_ConsoleCommand(command)
  if command == 'fixtureplaying' then
    -- Test-only override, not a real match start or eligibility test.
    et.trap_Cvar_Set('gamestate', '0')
    et.G_Print('FIXTURE_FORCED_PLAYING\n')
    return 1
  elseif command == 'fixturewarmup' then
    -- Exercise the command in warmup without pretending to start a match.
    et.trap_Cvar_Set('gamestate', '2')
    et.G_Print('FIXTURE_FORCED_WARMUP\n')
    return 1
  elseif command == 'fixturemutecheck' then
    -- The stored nickname deliberately differs from this bot's current name.
    assert(et.gentity_get(3, 'pers.cl_guid') == 'cccccccccccccccccccccccccccccccc')
    assert(et.gentity_get(3, 'sess.muted') == 1)
    assert(et.gentity_get(3, 'sess.auto_mute_time') == -1)
    et.G_Print('FIXTURE_RESTORED_MUTE\n')
    return 1
  elseif command == 'fixtureunmutecheck' then
    assert(et.gentity_get(3, 'sess.muted') == 0)
    et.G_Print('FIXTURE_UNMUTED\n')
    return 1
  elseif command == 'fixturekill' then
    assert(et.trap_Cvar_Get('gamestate') == '0')
    assert(et.gentity_get(1, 'sess.sessionTeam') == 1)
    assert(et.gentity_get(2, 'sess.sessionTeam') == 2)
    assert(et.gentity_get(2, 'health') > 0)
    et.G_Print('FIXTURE_DAMAGE_BEGIN\n')
    -- DAMAGE_NM_SPLAT, MOD_UNKNOWN: directly exercises damage/death without
    -- map geometry, client inputs, projectile simulation or real gameplay.
    et.G_Damage(2, 1, 1, 120, 128, 0)
    assert(et.gentity_get(1, 'sess.killingSpree') == 1)
    assert(et.gentity_get(2, 'health') <= 0 and not inLimbo(2))
    et.G_Print('FIXTURE_DAMAGE_END\n')
    return 1
  elseif command == 'fixturelimbocheck' then
    assert(inLimbo(2))
    savedSpawnCount = et.gentity_get(2, 'saved_persistant', 4)
    savedScore = et.gentity_get(2, 'saved_persistant', 0)
    et.G_Print('FIXTURE_LIMBO_CHECK\n')
    return 1
  elseif command == 'fixturelimbostill' then
    assert(inLimbo(2))
    et.G_Print('FIXTURE_LIMBO_STILL\n')
    return 1
  elseif command == 'fixturespawncheck' then
    assert(savedSpawnCount and savedScore)
    assert(not inLimbo(2) and et.gentity_get(2, 'health') > 0)
    assert(et.gentity_get(2, 'ps.persistant', 4) == savedSpawnCount + 1)
    assert(et.gentity_get(2, 'ps.persistant', 0) == savedScore)
    et.G_Print('FIXTURE_SPAWN_CHECK\n')
    return 1
  elseif command == 'fixturekillthird' then
    assert(et.gentity_get(2, 'sess.sessionTeam') == 2 and et.gentity_get(3, 'sess.sessionTeam') == 1)
    assert(et.gentity_get(3, 'health') > 0)
    et.G_Damage(3, 2, 2, 120, 128, 0)
    assert(et.gentity_get(3, 'health') <= 0 and not inLimbo(3))
    et.G_Print('FIXTURE_THIRD_DAMAGE\n')
    return 1
  elseif command == 'fixturethirdlimbo' then
    assert(inLimbo(3))
    savedThirdCount = et.gentity_get(3, 'saved_persistant', 4)
    savedThirdScore = et.gentity_get(3, 'saved_persistant', 0)
    savedThirdRespawnTime = et.gentity_get(3, 'respawnTime')
    et.G_Print('FIXTURE_THIRD_LIMBO\n')
    return 1
  elseif command == 'fixturemasscheck' then
    assert(savedThirdCount and savedThirdScore)
    assert(not inLimbo(3) and et.gentity_get(3, 'health') > 0)
    assert(et.gentity_get(3, 'ps.persistant', 4) == savedThirdCount + 1)
    assert(et.gentity_get(3, 'ps.persistant', 0) == savedThirdScore)
    -- ClientSpawn sets respawnTime to current match time. The old death
    -- deadline is later, showing !spawn intentionally bypassed that wait.
    assert(et.gentity_get(3, 'respawnTime') < savedThirdRespawnTime)
    et.G_Print('FIXTURE_MASS_SPAWN_CHECK\n')
    return 1
  elseif command == 'fixturequotename' then
    et.gentity_set(1, 'pers.netname', 'Seen Bot')
    et.G_Print('FIXTURE_QUOTED_NAME\n')
    return 1
  elseif command == 'fixturemalname' then
    -- A trusted test script sets a hostile display name directly so that a
    -- shortcut receives unsanitized data even if the engine sanitizes chat.
    et.gentity_set(1, 'pers.netname', 'Bad;quit')
    et.G_Print('FIXTURE_BAD_NAME\n')
    return 1
  elseif command == 'fixturenameback' then
    et.gentity_set(1, 'pers.netname', 'SeenBot')
    et.G_Print('FIXTURE_RESTORED_NAME\n')
    return 1
  elseif command == 'fixtureflush' then
    -- The custom command above appends its execution to the end of the
    -- engine buffer. Queue the second stage behind it, not ahead of it.
    et.trap_SendConsoleCommand(et.EXEC_APPEND, 'exec finish.cfg\n')
    return 1
  end
  return 0
end
'''


RUN = '''wait 220
bot fixtureadd SeenBot
bot fixtureadd WarnBot
bot fixtureadd FillerBot
wait 10
fixturemutecheck
set g_shrubbot invalid-shrub.cfg
wait 2
readconfig
fixturemutecheck
set g_shrubbot shrubbot.cfg
wait 2
bot fixtureclient 1 "say !fixtureecho afterbadreload"
unmute 3
fixtureunmutecheck
unmute DisconnectedAlias
readconfig
fixtureunmutecheck
setlevel 1 1
readconfig
fixturequotename
bot fixtureclient 1 "say !fixturequoted"
fixturenameback
warn 2 cache-secret
dewarn 2
fixtureplaying
wait 2
fixturekill
spreerecord
bot fixturelimbo 2
fixturelimbocheck
bot fixtureclient 3 "say !spawn 2"
fixturelimbostill
pause
bot fixtureclient 1 "say !spawn 2"
fixturelimbostill
unpause
bot fixtureclient 1 "say !spawn 2"
fixturelimbostill
wait 230
fixturelimbostill
bot fixtureclient 1 "say !spawn 2"
fixturespawncheck
fixturekillthird
bot fixturelimbo 3
fixturethirdlimbo
fixturewarmup
wait 2
bot fixtureclient 1 "say !spawn"
fixturemasscheck
fixtureplaying
wait 2
bot fixtureclient 1 "say !fixtureecho safeclient"
bot fixtureclient 1 "say !fixtureecho café"
bot fixtureclient 1 "say !fixtureecho badclient;quit"
bot fixtureclient 1 "say !fixturejoin /bad"
bot fixtureclient 3 "say !fixtureecho denied"
bot fixtureclient 1 "say !fixturewho"
fixturemalname
bot fixtureclient 1 "say !fixturewho"
fixturenameback
bot fixtureremove 1
wait 10
seen SeenBot
spreerecord
fixtureecho good
fixtureecho "bad;quit"
fixturejoin /bad
fixtureflush
'''

FINISH = '''map_restart 0
wait 20
seen SeenBot
dewarn 2
spreerecord
quit
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path, help="ET: Legacy 2.86.2 Linux x86-64 dedicated server")
    parser.add_argument("--module", type=Path, default=ROOT / "build/overzealous/qagame.mp.x86_64.so")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"), help="C++ compiler command")
    parser.add_argument("--port", type=int, default=0, help="local UDP port (default: choose an available port)")
    parser.add_argument("--show-log", action="store_true", help="print engine output on success too")
    args = parser.parse_args()
    engine, module = args.engine.resolve(), args.module.resolve()
    if not engine.is_file() or not module.is_file():
        parser.error("engine and built qagame module must both exist")
    if not 0 <= args.port <= 65535:
        parser.error("port must be between 0 and 65535")
    try:
        compiler = shlex.split(args.cxx)
    except ValueError as exc:
        parser.error(f"invalid compiler command: {exc}")
    if not compiler:
        parser.error("compiler command must not be empty")

    with tempfile.TemporaryDirectory(prefix="overzealous-etl-bot-smoke-") as temporary:
        root = Path(temporary)
        base, home, botdir = root / "base", root / "home", root / "bot"
        home.mkdir()
        botdir.mkdir()
        overzealous_dir = create_fixture(base, bot_capable=True)
        shutil.copyfile(module, overzealous_dir / "qagame.mp.x86_64.so")
        try:
            subprocess.run(compiler + ["-std=gnu++11", "-fPIC", "-shared", "-I", str(ROOT),
                                       str(ROOT / "tests/fixture_omnibot.cpp"), "-o", str(botdir / "omnibot_et.so")],
                           check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
        except (OSError, subprocess.SubprocessError) as exc:
            output = getattr(exc, "output", "") or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            print(f"Could not build synthetic bot fixture: {exc}\n{output}", file=sys.stderr)
            return 1

        lua = LUA.encode()
        (overzealous_dir / "smoke.lua").write_bytes(lua)
        digest = hashlib.sha1(lua).hexdigest().upper()
        (overzealous_dir / "smoke.cfg").write_text(
            f'set lua_modules "smoke.lua"\nset lua_allowedModules "{digest}"\n'
            'set g_doWarmup "1"\nset match_minplayers "8"\nset g_tyranny "1"\nset g_shortcuts "1"\n'
        )
        with (overzealous_dir / "shrubbot.cfg").open("a") as config:
            config.write(
                "[admin]\nname = SeenBot\nguid = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nlevel = 1\nflags = %\n\n"
                "[mute]\nname = HistoricalAlias\nguid = cccccccccccccccccccccccccccccccc\nexpires = 0\n\n"
                "[mute]\nname = DisconnectedAlias\nguid = dddddddddddddddddddddddddddddddd\nexpires = 0\n\n"
                "[command]\ncommand = fixtureecho\nexec = echo NM_CUSTOM_[1]; echo NM_CUSTOM_DONE\n"
                "levels = 1\ndesc = synthetic\n\n"
                "[command]\ncommand = fixturejoin\nexec = echo NM_JOIN_BEGIN; echo /[1]\n"
                "levels = 1\ndesc = synthetic\n\n"
                "[command]\ncommand = fixturewho\nexec = echo NM_WHO_PREFIX; echo [n]\n"
                "levels = 1\ndesc = synthetic\n\n"
                "[command]\ncommand = fixturequoted\nexec = \"echo \\\"NM_QUOTED_[n]\\\"\"\n"
                "levels = 1\ndesc = synthetic\n\n"
            )
        # A malformed reload must retain the live permissions, bans and
        # custom commands. The original is selected again before restart.
        (overzealous_dir / "invalid-shrub.cfg").write_text(
            "[command]\ncommand = fixtureecho\nlevels = " + "0 " * 33 + "\n"
        )
        (overzealous_dir / "run.cfg").write_text(RUN)
        (overzealous_dir / "finish.cfg").write_text(FINISH)
        # Keep the startup command count below ET: Legacy's 32-line limit;
        # the staged cfg files hold the test actions and their waits.
        command = [
            str(engine), "+set", "fs_basepath", str(base), "+set", "fs_homepath", str(home),
            "+set", "fs_game", "overzealous", "+set", "net_ip", "127.0.0.1",
            "+set", "net_port", str(args.port or available_udp_port()),
            "+set", "dedicated", "1", "+set", "com_recommendedSet", "1", "+set", "g_gametype", "2",
            "+set", "sv_pure", "0", "+set", "sv_fps", "20",
            "+set", "omnibot_enable", "1", "+set", "omnibot_path", str(botdir),
            "+exec", "smoke.cfg", "+map", "overzealous_test", "+exec", "run.cfg",
        ]
        try:
            proc = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, errors="replace", timeout=65)
            output = proc.stdout
        except (OSError, subprocess.TimeoutExpired) as exc:
            output = getattr(exc, "stdout", "") or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            print(output, file=sys.stderr)
            print(f"Engine could not complete: {exc}", file=sys.stderr)
            return 1
        output = re.sub(r"\x1b\[[0-9;]*m", "", output)
        problems = []
        if proc.returncode:
            problems.append(f"engine exited {proc.returncode}")
        restart = output.find("==== ShutdownGame ====")
        if restart < 0:
            problems.append("no map shutdown/restart observed")
            restart = len(output)
        before, after = output[:restart], output[restart:]
        expected_before = (
            "FIXTURE_BOT_INIT", "FIXTURE_ADD SeenBot 1", "FIXTURE_ADD WarnBot 2", "FIXTURE_ADD FillerBot 3",
            "FIXTURE_BEGIN 1", "FIXTURE_BEGIN 2", "FIXTURE_BEGIN 3", "FIXTURE_RESTORED_MUTE",
            "invalid or excessive command levels; previous configuration retained", "NM_CUSTOM_afterbadreload",
            "FIXTURE_UNMUTED", "removed mute for FillerBot", "removed mute for DisconnectedAlias",
            "FIXTURE_QUOTED_NAME", "NM_QUOTED_Seen Bot", "warnings: 1 saved", "cache-secret",
            "FIXTURE_FORCED_PLAYING", "FIXTURE_DAMAGE_BEGIN", "FIXTURE_DAMAGE_END",
            "FIXTURE_LIMBO 2 0 1", "FIXTURE_LIMBO_CHECK", "FIXTURE_CLIENT_COMMAND 3",
            "FIXTURE_LIMBO_STILL", "FIXTURE_CLIENT_COMMAND 1", "FIXTURE_SPAWN_CHECK",
            "FIXTURE_THIRD_DAMAGE", "FIXTURE_LIMBO 3 0 1", "FIXTURE_THIRD_LIMBO", "FIXTURE_FORCED_WARMUP",
            "FIXTURE_MASS_SPAWN_CHECK", "FIXTURE_CLIENT_COMMAND 1 say !fixtureecho safeclient", "FIXTURE_CLIENT_COMMAND 1 say !fixtureecho café",
            "FIXTURE_CLIENT_COMMAND 1 say !fixtureecho badclient;quit", "FIXTURE_CLIENT_COMMAND 1 say !fixturejoin /bad",
            "FIXTURE_CLIENT_COMMAND 3 say !fixtureecho denied", "FIXTURE_BAD_NAME", "FIXTURE_RESTORED_NAME",
            "FIXTURE_DISCONNECT 1", "seen: SeenBot disconnected", "SeenBot: !fixtureecho badclient quit",
            "NM_CUSTOM_good", "NM_CUSTOM_safeclient", "NM_CUSTOM_café", "NM_CUSTOM_badclient", "NM_CUSTOM_DONE",
            "NM_WHO_PREFIX",
        )
        expected_after = (
            "FIXTURE_BOT_INIT", "FIXTURE_CONNECT 2 0 1", "FIXTURE_BEGIN 2", "FIXTURE_BEGIN 3",
            "seen: no visible matching admin found", "warnings: 0 saved",
            "spreerecord: no record this map", "Server quit",
        )
        for marker in expected_before:
            if marker not in before:
                problems.append(f"missing before restart: {marker}")
        for marker in expected_after:
            if marker not in after:
                problems.append(f"missing after restart: {marker}")
        # The unauthorized request, paused request, unpausing request and
        # post-countdown check must each leave the target in limbo.
        if before.count("FIXTURE_LIMBO_STILL") != 4 or "FIGHT!" not in before:
            problems.append("spawn pause/unpause lifecycle was not exercised")
        if before.count("FIXTURE_FORCED_PLAYING") != 2:
            problems.append("warmup test did not restore playing state")
        record = "spreerecord: this map: SeenBot, 1 kill (not persistent)"
        disconnect = before.find("FIXTURE_DISCONNECT 1")
        first_record, last_record = before.find(record), before.rfind(record)
        if not (0 <= first_record < disconnect < last_record):
            problems.append("populated spree was not retained after actual ClientDisconnect")
        if before.count("custom command: unsafe or oversized substitution") != 2:
            problems.append("unsafe separator/comment custom substitutions were not both rejected")
        # ET: Legacy 2.86.2 sanitizes semicolons in client arguments before
        # dispatch; the first, now-safe token executes as 'badclient'. The
        # hostile shortcut bypasses that engine sanitation and must not queue
        # the configured prefix. A comment boundary and denied level must not
        # queue either, and none of these commands may leak past the restart.
        if ("NM_CUSTOM_denied" in output or "NM_JOIN_BEGIN" in output or
            before.count("NM_WHO_PREFIX") != 1 or before.count("NM_CUSTOM_DONE") != 5 or
            "NM_WHO_PREFIX" in after or "NM_CUSTOM_" in after):
            problems.append("custom command execution was unsafe or occurred after restart")
        if "cache-secret" in after or "seen: SeenBot disconnected" in after:
            problems.append("warning or seen history survived the completed restart")
        if re.search(r"\bERROR\b|Lua API: can not open file|disallowed by ACL|Cbuf_AddText: overflow|"
                     r"couldn't exec (?:smoke|run|finish|preset_high)\.cfg|can't find map", output, re.I):
            problems.append("engine or Lua output contains an error")
        if args.show_log or problems:
            print(output, file=sys.stderr if problems else sys.stdout)
        if problems:
            print("Bot smoke test failed: " + "; ".join(problems), file=sys.stderr)
            return 1
        print("ET: Legacy synthetic bot/lifecycle/custom-command/restart smoke passed; no real clients joined.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
