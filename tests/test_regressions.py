"""Overzealous native module regressions, Arena.ai, 2026-11-27.

These tests load the built libraries in fake engine/display harnesses. They
exercise selected behavior, not a complete ET: Legacy multiplayer session.
"""
import os
from pathlib import Path
import platform
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
ARCH = os.environ.get("ARCH", "x86_64")
WITH_LUA = os.environ.get("WITH_LUA", "1") == "1"
WITH_BOT = os.environ.get("WITH_OMNIBOT", "1") == "1"
WITH_AUTO_GUID = os.environ.get("WITH_AUTO_GUID", "0") == "1"
MODDIR = Path(os.environ.get("OVERZEALOUS_TEST_MODDIR", ROOT / "build" / "overzealous"))
MODULES = [MODDIR / f"{name}.mp.{ARCH}.so" for name in ("qagame", "cgame", "ui")]


class NativeRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        missing = [str(path) for path in MODULES if not path.is_file()]
        if missing:
            raise RuntimeError("build the modules first: " + ", ".join(missing))
        cls.tmp = tempfile.TemporaryDirectory(prefix="overzealous-tests-")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def compile_and_run(self, source, arguments=(), cxx=False, definitions=()):
        compiler = shlex.split(os.environ.get("CXX" if cxx else "CC", "c++" if cxx else "cc"))
        output = Path(self.tmp.name) / Path(source).stem
        cmd = compiler + ["-std=gnu++11" if cxx else "-std=gnu11", "-fcommon", "-I", str(ROOT)]
        if cxx:
            cmd += ["-fpermissive"]
        cmd += [f"-D{item}" for item in definitions]
        cmd += [str(ROOT / "tests" / source), "-ldl", "-lm", "-o", str(output)]
        subprocess.run(cmd, cwd=ROOT, check=True)
        subprocess.run([str(output), *(str(arg) for arg in arguments)], cwd=ROOT, check=True, timeout=30)

    def test_invalid_build_variants_are_rejected(self):
        env = dict(os.environ, MAKEFLAGS="", MFLAGS="")
        for variable in ("WITH_LUA", "WITH_OMNIBOT", "WITH_AUTO_GUID"):
            for value in ("", "2", "0 1"):
                with self.subTest(variable=variable, value=value):
                    result = subprocess.run(
                        ["make", "--no-print-directory", "-n", f"{variable}={value}", "all"],
                        cwd=ROOT, env=env, text=True, capture_output=True, timeout=10,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(f"{variable} must be 0 or 1", result.stderr)
        result = subprocess.run(
            ["make", "--no-print-directory", "-n", "ARCH=i386", "all"],
            cwd=ROOT, env=env, text=True, capture_output=True, timeout=10,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("only ARCH=x86_64 is currently supported", result.stderr)

    @unittest.skipUnless(platform.machine() == "x86_64" and ARCH == "x86_64", "x86_64 ET: Legacy 2.86.2 ABI baseline")
    def test_selected_etlegacy_abi_values_and_layouts(self):
        self.compile_and_run("etl_abi_layout.c", definitions=("OMNIBOTS",))

    def test_selected_native_syscalls_and_vm_entrypoints(self):
        self.compile_and_run("native_syscalls.c", MODULES, definitions=("OMNIBOTS",))

    def test_shared_parser_boundaries(self):
        self.compile_and_run("parse_boundaries.c", MODULES)

    def test_game_commands_report_privacy_and_math(self):
        defs = ["GAMEDLL"]
        if WITH_LUA:
            defs.append("ET_LUA")
        if WITH_BOT:
            defs.append("OMNIBOTS")
        if WITH_AUTO_GUID:
            defs.append("AUTO_GUID")
        self.compile_and_run("game_commands.c", MODULES[:1], definitions=defs)

    @unittest.skipUnless(platform.system() == "Linux" and platform.libc_ver()[0] == "glibc",
                         "fault preload uses glibc allocation hooks")
    def test_shrub_config_allocation_and_clock_failures(self):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        library = Path(self.tmp.name) / "fault_preload.so"
        harness = Path(self.tmp.name) / "fault_game"
        defs = ["GAMEDLL"] + (["ET_LUA"] if WITH_LUA else []) + (["OMNIBOTS"] if WITH_BOT else [])
        if WITH_AUTO_GUID:
            defs.append("AUTO_GUID")
        subprocess.run(compiler + ["-std=gnu11", "-shared", "-fPIC", str(ROOT / "tests/fault_preload.c"),
                                   "-o", str(library)], cwd=ROOT, check=True)
        subprocess.run(compiler + ["-std=gnu11", "-fcommon", "-I", str(ROOT)] +
                       [f"-D{item}" for item in defs] +
                       [str(ROOT / "tests/game_commands.c"), "-ldl", "-lm", "-o", str(harness)],
                       cwd=ROOT, check=True)
        env = dict(os.environ, LD_PRELOAD=str(library), OVERZEALOUS_FAULT_ONLY="1")
        subprocess.run([str(harness), str(MODULES[0])], cwd=ROOT, env=env, check=True, timeout=30)

    def test_ui_edit_field_bounds(self):
        self.compile_and_run("ui_text.c", (MODULES[2], MODULES[1]), definitions=("UIDLL",))

    def test_cgame_team_packet_bounds(self):
        self.compile_and_run("cgame_packets.c", (MODULES[1],))

    def test_experimental_share_weapon_in_game_and_prediction(self):
        # Compile separately: game/cgame have different pmoveExt_t layouts.
        game_defs = ["GAMEDLL"]
        if WITH_LUA:
            game_defs.append("ET_LUA")
        if WITH_BOT:
            game_defs.append("OMNIBOTS")
        if WITH_AUTO_GUID:
            game_defs.append("AUTO_GUID")
        self.compile_and_run("weapon_integration.c", (MODULES[0],), definitions=game_defs)
        client_defs = ["CGAMEDLL"] + (["AUTO_GUID"] if WITH_AUTO_GUID else [])
        self.compile_and_run("weapon_integration.c", (MODULES[1],), definitions=client_defs)

    @unittest.skipUnless(WITH_AUTO_GUID, "experimental AUTO_GUID disabled in this build")
    def test_optional_key_packet(self):
        self.compile_and_run("cgame_uid.c", (MODULES[1],))

    @unittest.skipUnless(WITH_BOT, "Omni-Bot disabled in this build")
    def test_omnibot_johnson_mapping(self):
        defs = ["GAMEDLL", "OMNIBOTS"]
        if WITH_LUA:
            defs.append("ET_LUA")
        if WITH_AUTO_GUID:
            defs.append("AUTO_GUID")
        self.compile_and_run("omnibot_weapons.cpp", MODULES[:1], cxx=True, definitions=defs)


if __name__ == "__main__":
    unittest.main()
