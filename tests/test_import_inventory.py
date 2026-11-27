"""Checks for the static inventory scanner, independent of upstream checkouts."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("import_inventory", ROOT / "tools/import_inventory.py")
scanner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scanner)


class InventoryScannerTests(unittest.TestCase):
    def test_comments_and_quoted_braces(self):
        source = '''void ClientCommand(int clientNum) {
            // Q_stricmp(cmd, "commented")
            const char *s = "url://example/{inside}";
            if (!Q_stricmp(cmd, "active")) { /* } */ nested(); }
        }
        void unrelated() { Q_stricmp(cmd, "after"); }
        '''
        body = scanner.section(scanner.uncomment(source), r"void\s+ClientCommand\s*\(\s*int\s+clientNum\s*\)")
        self.assertEqual(scanner.qcompare_names(body), {"active"})
        self.assertIn("url://example/{inside}", body)
        self.assertIn("one two", scanner.uncomment("one/* comment */two"))

    def test_clean_pinned_checkout_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "source"
            root.mkdir()
            def git(*args):
                return subprocess.run(["git", "-C", str(root), *args], check=True,
                                      capture_output=True, text=True).stdout.strip()
            git("init", "-q")
            git("config", "user.email", "test@example.invalid")
            git("config", "user.name", "Test")
            (root / "source.c").write_text("one\n")
            git("add", "source.c")
            git("commit", "-qm", "fixture")
            revision = git("rev-parse", "HEAD")
            scanner.verify_checkout(root, revision)
            with self.assertRaisesRegex(ValueError, "expected"):
                scanner.verify_checkout(root, "not-the-revision")
            (root / "source.c").write_text("modified\n")
            with self.assertRaisesRegex(ValueError, "modified or untracked"):
                scanner.verify_checkout(root, revision)
            git("checkout", "--", "source.c")
            (root / "untracked.c").write_text("untracked\n")
            with self.assertRaisesRegex(ValueError, "modified or untracked"):
                scanner.verify_checkout(root, revision)
            with self.assertRaisesRegex(ValueError, "missing"):
                scanner.verify_checkout(root / "absent", revision)

    def test_conditional_names_and_war_macros(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d) / "g_shrubbot.c"
            source.write_text('''static const struct g_shrubbot_cmd g_shrubbot_cmds[] = {
                {"help", fn}, // {"removed", fn},
                #ifdef OPTIONAL
                {"clientcmd", fn},
                #endif
                WARCOMMAND(rifle),
                {"", NULL}
            };''')
            self.assertEqual(scanner.shrub(source), {"help", "clientcmd", "riflewar"})

    def test_weapon_ids_and_hash_validation(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            weapons = root / "weapons.h"
            weapons.write_text("typedef enum { WP_NONE, WP_KNIFE, VERYBIGEXPLOSION, WP_TEST, WP_NUM_WEAPONS } weapon_t;")
            self.assertEqual(scanner.weapons(weapons), {"WP_NONE": 0, "WP_KNIFE": 1, "VERYBIGEXPLOSION": 2, "WP_TEST": 3})
            game = root / "game.c"
            header = root / "header.h"
            game.write_text('void ClientCommand(int clientNum) { switch (hash) { case SAY_HASH: break; } if (!Q_stricmp(cmd, "adrenother")) {} }')
            header.write_text(f"#define SAY_HASH {scanner.hash_value('say')}\n")
            self.assertEqual(scanner.game_client(game, header), {"say", "adrenother"})
            header.write_text("#define SAY_HASH 1\n")
            with self.assertRaises(ValueError):
                scanner.game_client(game, header)
            header.write_text(f"#define SAY_HASH {scanner.hash_value('say')} + 1\n")
            with self.assertRaises(ValueError):
                scanner.game_client(game, header)

    def test_jaymod_multiple_constructors_in_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "builtins.cpp").write_text('''
                First::First() : AbstractBuiltin("one") {}
                Second::Second() : AbstractBuiltin("two", true) {}
                // AbstractBuiltin("commented")
            ''')
            self.assertEqual(scanner.jay_shrub(root), {"one", "two"})

    def test_null_osp_handler_is_not_a_server_command(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d) / "ext.c"
            source.write_text('''static const cmd_reference_t aCommandInfo[] = {
                {"local_only", qtrue, qfalse, NULL, "help"},
                {"handled", qtrue, qtrue, SomeFunction, "help"},
                {NULL}
            };''')
            self.assertEqual(scanner.osp(source), {"handled"})


if __name__ == "__main__":
    unittest.main()
