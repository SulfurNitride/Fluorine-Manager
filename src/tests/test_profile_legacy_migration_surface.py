import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ProfileLegacyMigrationSurfaceTests(unittest.TestCase):
    def test_profile_uses_resumable_migration_before_persisting_feature_state(self):
        source = (ROOT / "src/src/profile.cpp").read_text(encoding="utf-8")
        start = source.index("void Profile::findProfileSettings()")
        end = source.index("\nbool Profile::exists()", start)
        migration = source[start:end]

        self.assertIn("g_ProfileWriteBarrier.enterIfAllowed()", migration)
        self.assertIn("ProfileLegacyMigration::pendingOperation", migration)
        self.assertIn("ProfileLegacyMigration::migrate", migration)
        self.assertIn('"saves-to-backup"', migration)
        self.assertIn('"backup-to-saves"', migration)
        self.assertIn('"ini-backup-to-live"', migration)
        self.assertIn("if (!result.succeeded())", migration)
        self.assertIn("Both the profile save directory and its legacy backup exist",
                      migration)
        self.assertNotIn('m_Directory.rename("saves", "_saves")', migration)
        self.assertNotIn('m_Directory.rename("_saves", "saves")', migration)

    def test_production_helper_is_compiled_by_the_focused_test(self):
        cmake = (ROOT / "src/tests/CMakeLists.txt").read_text(encoding="utf-8")
        start = cmake.index("add_executable(test_profilelegacymigration")
        end = cmake.index("add_executable(", start + 15)
        target = cmake[start:end]

        self.assertIn("test_profilelegacymigration.cpp", target)
        self.assertIn("src/src/profilelegacymigration.cpp", target)
        self.assertIn("add_test(NAME test_profilelegacymigration", target)


if __name__ == "__main__":
    unittest.main()
