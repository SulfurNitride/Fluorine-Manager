import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src" / "src"
GAMEBRYO = ROOT / "libs" / "game_bethesda" / "src" / "gamebryo"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class WineSaveSurfaceTest(unittest.TestCase):
    def test_wineprefix_facades_use_the_transactional_state_machine(self):
        source = (SRC / "wineprefix.cpp").read_text(encoding="utf-8")
        deploy = function_body(source, "WinePrefix::deployProfileSaves")
        bind = function_body(source, "WinePrefix::prepareProfileSavesBindTarget")
        sync = function_body(source, "WinePrefix::syncSavesBack")
        rollback = function_body(source, "WinePrefix::rollbackProfileSaves")

        self.assertIn("WineSaveDeployment::deployLinks", deploy)
        self.assertIn("WineSaveDeployment::prepareBindTarget", bind)
        self.assertIn("WineSaveDeployment::synchronizeAndRestore", sync)
        self.assertIn("WineSaveDeployment::rollbackLinks", rollback)
        for body in (deploy, bind, sync, rollback):
            self.assertNotIn("removeRecursively", body)
            self.assertNotIn("QFile::remove", body)

        stale = function_body(source, "WinePrefix::restoreStaleBackups")
        self.assertIn("QLockFile prefixLease", stale)
        self.assertLess(stale.index("prefixLease.tryLock"),
                        stale.index("hasPersistedSessionLease"))

    def test_launch_rejects_failed_save_deployment(self):
        source = (SRC / "organizercore.cpp").read_text(encoding="utf-8")
        before = function_body(source, "OrganizerCore::beforeRun")
        deploy_start = before.index("if (!prefix.deployProfileSaves")
        deploy_end = before.index("// Ensure the prefix INI", deploy_start)
        self.assertIn("return false;", before[deploy_start:deploy_end])
        self.assertLess(before.index("prefix.deployProfileSaves"),
                        before.index("WineSaveRouting::activate"))
        self.assertIn("useProton &&", before)
        self.assertIn("m_SaveDeploymentLocks.insert", before)
        self.assertIn("SaveDeploymentMode::ManagedLinks", before)
        self.assertIn("SaveDeploymentMode::BindMount", before)
        self.assertIn("WineSaveRouting::activate", before)
        self.assertIn("WineSaveRouting::routeFor", before)
        self.assertIn("WineSaveRouting::familyTargetsDirectory", before)
        self.assertIn("LocalSavegamesRouting", before)
        self.assertIn("routing->routingIniName()", before)
        self.assertIn("routing->routingPath()", before)
        self.assertIn("managedIniFiles.append(contractIni)", before)
        self.assertIn("profileIniSource(*launchProfile, iniFile)", before)
        self.assertIn("profileIniSource(*launchProfile, routingIniName)", before)
        self.assertNotIn("absoluteIniFilePath(iniFile)", before)
        self.assertIn("routing.recoveryRequired", before)
        self.assertIn("profileIniDeployments.append", before)
        self.assertIn("iniDeployment.needsCleanup()", before)
        self.assertNotIn('WriteRegistryValue("General", "__MO_Saves', before)
        self.assertIn("WineSaveDeployment::pendingDeployment", before)
        self.assertIn("WineSaveDeployment::beginSessionLease", before)
        self.assertIn("sessionLeasePublished = true", before)
        self.assertIn("preparedPrefixPath", before)
        self.assertIn("canonicalFilePath()", before)
        self.assertRegex(before, r"prefixPath\s*=\s*preparedPrefixPath")
        self.assertNotIn('WriteRegistryValue("General", "sLocalSavePath"', before)
        self.assertIn("already selects the unowned", before)
        self.assertIn("managed save route", before)
        self.assertIn("legacySavePathReceipts", before)
        self.assertIn("restoreConfirmedLegacyReceipt", before)
        self.assertIn("clearConfirmedLegacyRoute", before)
        self.assertIn("QMessageBox::question", before)
        self.assertIn("were preserved for explicit recovery", before)
        self.assertLess(before.index("pendingDeployment"),
                        before.index("deployPlugins"))

    def test_linux_gamebryo_profile_preparation_has_no_routing_side_effects(self):
        source = (GAMEBRYO / "gamebryolocalsavegames.cpp").read_text(
            encoding="utf-8"
        )
        prepare = function_body(
            source, "bool GamebryoLocalSavegames::prepareProfile"
        )
        non_windows = prepare[
            prepare.index("#ifndef _WIN32") : prepare.index("#else")
        ]
        windows = prepare[prepare.index("#else") : prepare.index("#endif")]

        self.assertIn("Q_UNUSED(profile)", non_windows)
        self.assertIn("return false;", non_windows)
        for mutation in (
            "WriteRegistryValue",
            "RemoveRegistryValue",
            "savepath.ini",
            "QFile::remove",
            "mkdir",
        ):
            self.assertNotIn(mutation, non_windows)
        self.assertIn("WriteRegistryValue", windows)
        self.assertIn("RemoveRegistryValue", windows)

        header = (GAMEBRYO / "gamebryolocalsavegames.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("public MOBase::LocalSavegamesRouting", header)
        self.assertIn("routingIniName() const override", header)
        self.assertIn("routingPath() const override", header)

        enderal = (
            ROOT
            / "libs"
            / "game_bethesda"
            / "src"
            / "games"
            / "enderal"
            / "enderallocalsavegames.cpp"
        ).read_text(encoding="utf-8")
        enderal_se = (
            ROOT
            / "libs"
            / "game_bethesda"
            / "src"
            / "games"
            / "enderalse"
            / "enderalselocalsavegames.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("..\\\\Enderal\\\\__MO_Saves\\\\", enderal)
        self.assertIn(
            "..\\\\Enderal Special Edition\\\\__MO_Saves\\\\",
            enderal_se,
        )

        registrations = {
            "fallout4/gamefallout4.cpp": "Fallout4Custom.ini",
            "fallout4vr/gamefallout4vr.cpp": "Fallout4Custom.ini",
            "fallout4london/gamefo4london.cpp": "fo4londoncustom.ini",
            "skyrimse/gameskyrimse.cpp": "SkyrimCustom.ini",
            "starfield/gamestarfield.cpp": "StarfieldCustom.ini",
            "enderal/gameenderal.cpp": "Enderal.ini",
            "enderalse/gameenderalse.cpp": "enderal.ini",
        }
        games = ROOT / "libs" / "game_bethesda" / "src" / "games"
        for relative, routing_ini in registrations.items():
            registration = (games / relative).read_text(encoding="utf-8")
            self.assertRegex(
                registration,
                rf"make_shared<[^>]*LocalSavegames>\(this,\s*\"{routing_ini}\"\)",
            )

    def test_teardown_and_abort_use_the_recorded_receipt(self):
        core = (SRC / "organizercore.cpp").read_text(encoding="utf-8")
        runner = (SRC / "processrunner.cpp").read_text(encoding="utf-8")
        continuation = function_body(core, "OrganizerCore::continueAfterRun")
        abort = function_body(core, "OrganizerCore::continueAbortedLaunchTeardown")

        self.assertIn("work->saveDeployment.mode", continuation)
        self.assertIn("work->saveDeployment.profileRoot", continuation)
        self.assertIn("work->saveDeployment.livePath", continuation)
        self.assertIn("restoreSaveRouting(work->saveDeployment)", continuation)
        self.assertIn("WineSaveDeployment::endSessionLease", continuation)
        self.assertIn("finishProfileIniDeployment", continuation)
        self.assertLess(continuation.index("finishProfileIniDeployment"),
                        continuation.index("WineSaveDeployment::endSessionLease"))
        self.assertIn("work->preparedWinePrefix", continuation)
        self.assertNotIn("ProtonLauncher::unprivilegedBindMountSupported()",
                         continuation)
        self.assertNotIn("undeployProfileSaves", continuation)
        self.assertNotIn("localSavesEnabled()", continuation)
        self.assertIn("rollbackProfileSaves", abort)
        self.assertIn("restoreSaveRouting(work->saveDeployment)", abort)
        self.assertIn("WineSaveDeployment::endSessionLease", abort)
        self.assertIn("finishProfileIniDeployment", abort)
        self.assertLess(abort.index("finishProfileIniDeployment"),
                        abort.index("WineSaveDeployment::endSessionLease"))
        self.assertIn("observer->saveDeployment", runner)
        self.assertIn("std::move(m_sp.saveDeployment)", runner)
        self.assertNotIn("usedSaveBindMount", core + runner)

        deployment = (SRC / "winesavedeployment.cpp").read_text(
            encoding="utf-8"
        )
        lease = function_body(deployment, "QString leasePathFor")
        self.assertIn(".fluorine-save-prefix.lock", lease)
        self.assertNotIn("physicalLiveIdentity", lease)

    def test_bind_request_cannot_silently_fall_back(self):
        spawn = (SRC / "spawn.cpp").read_text(encoding="utf-8")
        proton = (SRC / "protonlauncher.cpp").read_text(encoding="utf-8")
        launch = function_body(proton, "ProtonLauncher::launchWithProton")
        launch_dispatch = function_body(proton, "ProtonLauncher::launch()")
        spawn_dispatch = function_body(spawn, "int spawn(")

        self.assertIn("SaveDeploymentMode::BindMount", spawn)
        self.assertIn("!sp.useProton", spawn)
        self.assertIn("refusing to launch", launch)
        self.assertNotIn("game will write to prefix", launch)
        self.assertIn("bindRequested", launch_dispatch)
        self.assertIn("m_protonPath.isEmpty()", launch_dispatch)
        self.assertIn("managedLivePaths", spawn_dispatch)
        self.assertIn("m_bindMountTargets", launch)
        self.assertIn('while [ "$1" != "--" ]', launch)

        self.assertIn("sp.saveDeployment.prefixPath", spawn_dispatch)
        self.assertIn("samePhysicalDirectory", spawn_dispatch)
        self.assertLess(spawn_dispatch.index("samePhysicalDirectory"),
                        spawn_dispatch.index("launcher.setPrefix"))

    def test_routing_snapshot_is_durable_and_not_qsettings_based(self):
        source = (SRC / "winesaverouting.cpp").read_text(encoding="utf-8")
        core = (SRC / "organizercore.cpp").read_text(encoding="utf-8")
        restore = function_body(core, "restoreSaveRouting")
        activate = function_body(source, "Result activate")

        self.assertIn("QSaveFile", source)
        self.assertIn("setDirectWriteFallback(false)", source)
        self.assertIn("sLocalSavePath", source)
        self.assertIn("bUseMyGamesDirectory", source)
        self.assertNotIn("QSettings", source)
        self.assertIn("WineSaveRouting::restore", restore)
        self.assertIn("routeTargetsDirectory", activate)
        self.assertLess(activate.index("routeTargetsDirectory"),
                        activate.index("writeReceipt"))

        application = (SRC / "moapplication.cpp").read_text(encoding="utf-8")
        recovery = application[
            application.index("// Restore any stale INI/save backups") :
            application.index("m_core->createDefaultProfile()")
        ]
        self.assertLess(recovery.index('"fluorine/prefix_path"'),
                        recovery.index('"Settings/proton_prefix_path"'))

    def test_startup_does_not_claim_name_only_save_backups(self):
        source = (SRC / "wineprefix.cpp").read_text(encoding="utf-8")
        restore = function_body(source, "WinePrefix::restoreStaleBackups")
        self.assertNotIn(".mo2linux_backup_", restore)
        self.assertNotIn("recoverBackupAtStartup", restore)
        self.assertIn("hasPersistedSessionLease", restore)
        self.assertIn("WineSaveRouting::pendingOwner", restore)


if __name__ == "__main__":
    unittest.main()
