import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ProfileRenameSurfaceTests(unittest.TestCase):
    def test_profile_adopts_only_a_committed_rename(self):
        source = (ROOT / "src/src/profile.cpp").read_text(encoding="utf-8")
        self.assertIn("SettingsWriteBarrier::Concurrency::Serialized", source)
        start = source.index("bool Profile::tryRename")
        end = source.index("\nQString keyName", start)
        rename = source[start:end]

        self.assertIn("ProfileRename::apply", rename)
        self.assertIn("if (!result.succeeded())", rename)
        self.assertLess(rename.index("if (!result.succeeded())"),
                        rename.index("m_Directory.setPath(result.targetPath)"))
        self.assertIn("g_ProfileSettings.extract(previousSettings)", rename)
        self.assertIn("g_ProfileSettings.insert(std::move(registration))", rename)
        self.assertIn("m_Settings = replacement", rename)
        self.assertIn("Status::RollbackFailed", rename)
        self.assertIn("suppressWritesForFailedRollback()", rename)
        self.assertNotIn("profileDir.rename", rename)

    def test_dialog_updates_ui_and_notifies_plugins_after_success(self):
        source = (ROOT / "src/src/profilesdialog.cpp").read_text(encoding="utf-8")
        start = source.index("void ProfilesDialog::on_renameButton_clicked")
        end = source.index("\nvoid ProfilesDialog::on_invalidationBox", start)
        handler = source[start:end]

        call = re.search(r"if \(currentProfile->tryRename\(name, &error, &restartRequired\)\)", handler)
        self.assertIsNotNone(call)
        update = handler.index("setText(currentProfile->name())")
        notify = handler.index("emit profileRenamed")
        self.assertLess(call.start(), update)
        self.assertLess(update, notify)
        self.assertIn("tryAcquireQuiescentConfigurationLease", handler)
        self.assertIn("restartRequired", handler)
        self.assertIn("m_FatalFailure = true", handler)
        self.assertIn("m_FatalLease.emplace(std::move(configurationLease))", handler)

        main_window = (ROOT / "src/src/mainwindow.cpp").read_text(encoding="utf-8")
        self.assertGreaterEqual(main_window.count("fatalFailure()"), 3)
        self.assertGreaterEqual(main_window.count("failStopAfterSettingsRollback("), 5)


if __name__ == "__main__":
    unittest.main()
