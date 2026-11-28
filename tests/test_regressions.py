"""Overzealous native module regressions, Arena.ai, 2026-11-27.

These tests load the built libraries in fake engine/display harnesses. They
exercise selected behavior, not a complete ET: Legacy multiplayer session.
"""
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile

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
        for bad_args, expected_msg in (
            (["ARCH=ppc"], "Unsupported ARCH 'ppc'"),
            (["PLATFORM=freebsd"], "Unsupported PLATFORM: freebsd"),
            (["PLATFORM=linux", "ARCH=universal"], "Unsupported ARCH 'universal'"),
        ):
            with self.subTest(bad_args=bad_args):
                result = subprocess.run(
                    ["make", "--no-print-directory", "-n", *bad_args, "all"],
                    cwd=ROOT, env=env, text=True, capture_output=True, timeout=10,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_msg, result.stderr)

        for good_args in (
            ["PLATFORM=linux", "ARCH=x86_64"],
            ["PLATFORM=linux", "ARCH=i386"],
            ["PLATFORM=windows", "ARCH=x86_64"],
            ["PLATFORM=windows", "ARCH=i386"],
            ["PLATFORM=macos", "ARCH=universal"],
        ):
            with self.subTest(good_args=good_args):
                result = subprocess.run(
                    ["make", "--no-print-directory", "-n", *good_args, "all"],
                    cwd=ROOT, env=env, text=True, capture_output=True, timeout=10,
                )
                self.assertEqual(result.returncode, 0, result.stderr)

    def _find_zig(self):
        env_zig = os.environ.get("ZIG")
        if env_zig and Path(env_zig).is_file():
            return env_zig
        which_zig = shutil.which("zig")
        if which_zig:
            return which_zig
        for candidate in (
            ROOT / ".cache/zig-pip/ziglang/zig",
            ROOT / ".tools/zig-pip/extracted/ziglang/zig",
        ):
            if candidate.is_file():
                return str(candidate)
        return None

    def test_cross_target_abi_layouts(self):
        zig = self._find_zig()
        if not zig:
            self.skipTest("zig cross-compiler not installed")
        for target in (
            "x86_64-linux-gnu.2.17",
            "x86-linux-gnu.2.17",
            "x86_64-windows-gnu",
            "x86-windows-gnu",
            "x86_64-macos",
            "aarch64-macos",
        ):
            with self.subTest(target=target):
                obj_out = Path(self.tmp.name) / f"abi-{target}.o"
                subprocess.run(
                    [zig, "cc", "-target", target, "-std=gnu11", "-I", str(ROOT),
                     "-DOMNIBOTS", "-c", str(ROOT / "tests/etl_abi_layout.c"),
                     "-o", str(obj_out)],
                    cwd=ROOT, check=True, timeout=30,
                )

    def test_cross_platform_artifacts_when_built(self):
        i386_game = MODDIR / "qagame.mp.i386.so"
        if i386_game.is_file():
            data = i386_game.read_bytes()
            self.assertEqual(data[:5], b"\x7fELF\x01")  # ELF32
            e_machine = struct.unpack_from("<H", data, 18)[0]
            self.assertEqual(e_machine, 3)  # EM_386
            if shutil.which("readelf"):
                dyn = subprocess.run(
                    ["readelf", "--dyn-syms", "-W", str(i386_game)],
                    cwd=ROOT, text=True, capture_output=True, check=True,
                ).stdout
                self.assertIn(" vmMain", dyn)
                self.assertIn(" dllEntry", dyn)
                for line in dyn.splitlines():
                    parts = line.split()
                    if len(parts) >= 8 and parts[6] == "UND":
                        self.assertIn("@GLIBC_", parts[7], f"unversioned undefined symbol: {line}")
                        ver = tuple(int(x) for x in parts[7].split("@GLIBC_")[1].split("."))
                        self.assertLessEqual(ver, (2, 17), f"GLIBC too new in {parts[7]}")

        for dll_name, expected_machine in (
            ("qagame_mp_x64.dll", 0x8664),
            ("qagame_mp_x86.dll", 0x014C),
            ("cgame_mp_x64.dll", 0x8664),
            ("cgame_mp_x86.dll", 0x014C),
            ("ui_mp_x64.dll", 0x8664),
            ("ui_mp_x86.dll", 0x014C),
        ):
            dll_path = MODDIR / dll_name
            if dll_path.is_file():
                data = dll_path.read_bytes()
                pe_off = struct.unpack_from("<I", data, 0x3C)[0]
                self.assertEqual(data[pe_off:pe_off + 4], b"PE\x00\x00")
                machine = struct.unpack_from("<H", data, pe_off + 4)[0]
                self.assertEqual(machine, expected_machine, dll_name)

        for mac_name in ("qagame_mac", "cgame_mac", "ui_mac"):
            mac_path = MODDIR / mac_name
            if mac_path.is_file():
                data = mac_path.read_bytes()
                magic, narch = struct.unpack_from(">II", data, 0)
                self.assertEqual(magic, 0xCAFEBABE, mac_name)
                self.assertEqual(narch, 2, mac_name)

        pk3_all = ROOT / "build" / "overzealous-all.pk3"
        if pk3_all.is_file():
            with zipfile.ZipFile(pk3_all) as zf:
                names = set(zf.namelist())
                for req in (
                    "cgame.mp.x86_64.so", "ui.mp.x86_64.so",
                    "cgame.mp.i386.so", "ui.mp.i386.so",
                    "cgame_mp_x64.dll", "ui_mp_x64.dll",
                    "cgame_mp_x86.dll", "ui_mp_x86.dll",
                    "cgame_mac", "ui_mac", "gameinfo.txt",
                ):
                    self.assertIn(req, names)
                self.assertFalse([n for n in names if "qagame" in n])

    @unittest.skipUnless(platform.machine() == "x86_64" and ARCH == "x86_64", "x86_64 ET: Legacy 2.86.2 ABI baseline")
    def test_selected_etlegacy_abi_values_and_layouts(self):
        self.compile_and_run("etl_abi_layout.c", definitions=("OMNIBOTS",))

    def test_selected_native_syscalls_and_vm_entrypoints(self):
        self.compile_and_run("native_syscalls.c", MODULES, definitions=("OMNIBOTS",))

    def test_shared_parser_boundaries(self):
        self.compile_and_run("parse_boundaries.c", MODULES)

    def test_preparsed_script_params_functional(self):
        # Functional: call the real PreParse_* functions in qagame with
        # mapscript-style parameters (gotomarker included, the oasis
        # crash case) and verify the strings land in the slots the
        # runtime actions read. Segfaults on the buggy build.
        defs = ["GAMEDLL"]
        if WITH_LUA:
            defs.append("ET_LUA")
        if WITH_BOT:
            defs.append("OMNIBOTS")
        if WITH_AUTO_GUID:
            defs.append("AUTO_GUID")
        self.compile_and_run("preparse_params.c", MODULES[:1], definitions=defs)

    def test_preparsed_script_params_alloc_matches_copy(self):
        # Regression: every PreParse_* in g_script_actions.c allocated its
        # first string into data_char[01] -- an octal literal, i.e. index 1,
        # not 0 -- then Q_strncpyz() copied into the still-NULL data_char[0],
        # segfaulting in libc strncpy at map start (e.g. gotomarker on
        # oasis). The allocation slot must match the copy slot.
        source = (ROOT / "src/game/g_script_actions.c").read_text()
        self.assertNotIn("data_char[01]", source)
        pairs = re.findall(
            r"data_char\[(\d+)\]\s*=\s*\(char\*\)G_Alloc\(\s*len \+ 1\s*\);\s*"
            r"Q_strncpyz\(\s*preparsed_params->data_char\[(\d+)\]",
            source,
        )
        self.assertEqual(len(pairs), 17, pairs)
        for alloc_idx, copy_idx in pairs:
            self.assertEqual(
                alloc_idx, copy_idx,
                f"preparsed_params string allocated into data_char[{alloc_idx}] "
                f"but copied into data_char[{copy_idx}]",
            )
        # Q_strncpyz must turn a NULL destination into a clean fatal error
        # instead of a segfault inside strncpy.
        qshared = (ROOT / "src/game/q_shared.c").read_text()
        self.assertIn("Q_strncpyz: NULL dest", qshared)

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
