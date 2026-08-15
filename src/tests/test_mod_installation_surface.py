import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ModInstallationSurfaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.installation = (ROOT / "src/src/installationmanager.cpp").read_text()
        cls.core = (ROOT / "src/src/organizercore.cpp").read_text()
        cls.mod_info = (ROOT / "src/src/modinforegular.cpp").read_text()
        cls.omod = (
            ROOT / "libs/installer_omod/src/OMODFrameworkWrapper.cpp"
        ).read_text()

    def test_overwrite_prompt_does_not_mutate_live_mod(self):
        begin = self.installation.index(
            "InstallationResult InstallationManager::testOverwrite"
        )
        end = self.installation.index(
            "bool InstallationManager::ensureValidModName", begin
        )
        prompt = self.installation[begin:end]
        for forbidden in ("shellDelete", "copyDir", "mkdir(", "emit modReplaced"):
            self.assertNotIn(forbidden, prompt)

    def test_simple_install_publishes_only_after_complete_stage(self):
        begin = self.installation.index(
            "InstallationResult InstallationManager::doInstall"
        )
        end = self.installation.index(
            "bool InstallationManager::wasCancelled", begin
        )
        install = self.installation[begin:end]
        stage = install.index("transaction->stagePath()")
        extract = install.index("extractFiles(targetDirectory")
        metadata = install.index("settingsFile.sync()")
        metadata_status = install.index(
            "settingsFile.status() != QSettings::NoError", metadata
        )
        publish = install.index("transaction->publish()")
        publish_failure = install.index("if (!publication)", publish)
        notify = install.index("emit modReplaced")
        flush = install.index("flushMetaForTransaction")
        retire = install.index("retireMetadataWriter", publish)
        self.assertLess(stage, extract)
        self.assertLess(extract, metadata)
        self.assertLess(metadata, publish)
        self.assertLess(metadata, metadata_status)
        self.assertLess(metadata_status, publish)
        self.assertLess(publish, publish_failure)
        self.assertLess(publish_failure, notify)
        self.assertLess(publish, notify)
        self.assertLess(flush, stage)
        self.assertGreater(install.count("flushMetaForTransaction"), 1)
        self.assertLess(publish, retire)
        self.assertLess(retire, notify)
        self.assertNotIn("canonicalPath()", install)
        self.assertNotIn("bool const merge = false", install)

    def test_custom_installer_uses_deferred_transaction_lifecycle(self):
        begin = self.core.index("MOBase::IModInterface* OrganizerCore::createMod")
        end = self.core.index("void OrganizerCore::modDataChanged", begin)
        create_mod = self.core[begin:end]
        self.assertIn("m_CustomInstallerActive", create_mod)
        self.assertIn("m_CustomInstallationTransaction", create_mod)
        self.assertIn("Mode::Replace", create_mod)
        self.assertIn("Mode::Merge", create_mod)
        self.assertIn("finishCustomInstaller", create_mod)

        install_begin = self.installation.index("InstallationResult InstallationManager::install")
        custom = self.installation[install_begin:]
        installer_call = custom.index("installerCustom->install")
        self.assertLess(
            custom.index("m_CustomInstallerBegin"),
            installer_call,
        )
        self.assertLess(
            installer_call,
            custom.index("m_CustomInstallerFinish", installer_call),
        )
        finish_call = custom.index("m_CustomInstallerFinish", installer_call)
        self.assertLess(
            finish_call,
            custom.index("customFilesystemChanged", finish_call),
        )

        finish_begin = self.core.index("bool OrganizerCore::finishCustomInstaller")
        finish_end = self.core.index(
            "MOBase::IModInterface* OrganizerCore::createMod", finish_begin
        )
        finish = self.core[finish_begin:finish_end]
        metadata = finish.index("prepareStagedMetadata")
        repository_guard = finish.index("if (!repository.isEmpty())", metadata)
        repository_set = finish.index("setRepository(repository)", repository_guard)
        save = finish.index("flushMetaForTransaction", metadata)
        sync = finish.index("stagedSettings.sync()", save)
        publish = finish.index("m_CustomInstallationTransaction->publish()", sync)
        retire = finish.index("retireMetadataWriter()", publish)
        self.assertLess(metadata, save)
        self.assertLess(repository_guard, repository_set)
        self.assertLess(repository_set, save)
        self.assertLess(save, sync)
        self.assertLess(sync, publish)
        self.assertLess(publish, retire)
        self.assertIn("publication.filesystemChanged()", finish)
        self.assertIn("m_CustomPreviousMod->retireMetadataWriter()", finish)
        self.assertGreater(finish.count("flushMetaForTransaction"), 1)

    def test_omod_move_failures_abort_the_candidate(self):
        for source in ('data + "/*.*"', 'plugins + "/*.*"'):
            move = self.omod.index(f"MOBase::shellMove({source}")
            failure = self.omod.index("return EInstallResult::RESULT_FAILED", move)
            success = self.omod.index("MOBase::log::debug", move)
            self.assertLess(success, failure)

    def test_retired_mod_model_cannot_write_into_replacement_generation(self):
        constructor = self.mod_info.index("ModInfoRegular::ModInfoRegular")
        capture = self.mod_info.index("m_PathIdentityCaptured = true", constructor)
        save = self.mod_info.index("void ModInfoRegular::saveMetaImpl")
        generation = self.mod_info.index("m_PathInode", save)
        settings = self.mod_info.index("QSettings metaFile", save)
        suppress = self.mod_info.index("m_WritesSuppressed = true", save)
        self.assertLess(capture, save)
        self.assertLess(generation, suppress)
        self.assertLess(suppress, settings)


if __name__ == "__main__":
    unittest.main()
