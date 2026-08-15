import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src" / "src"
GAMEBRYO = ROOT / "libs" / "game_bethesda" / "src"
BASIC = ROOT / "libs" / "basic_games" / "basic_game.py"


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


class WineRuntimeSurfaceTest(unittest.TestCase):
    def test_spawn_consumes_only_the_prepared_runtime(self):
        source = (SRC / "spawn.cpp").read_text(encoding="utf-8")
        spawn = function_body(source, "int spawn(")
        self.assertIn("sp.wineRuntime.prefixPath", spawn)
        self.assertIn("sp.wineRuntime.protonPath", spawn)
        self.assertIn("setCompatDataPath(sp.wineRuntime.compatDataPath)", spawn)
        self.assertIn("WineRuntimeConfig::revalidate", spawn)
        self.assertNotIn("FluorineConfig::", spawn)
        self.assertNotIn("fluorine/prefix_path", spawn)
        self.assertNotIn("resolvePrefixPath", source)
        self.assertNotIn("resolveProtonPath", source)

    def test_setup_publishes_before_plugins_and_recovery_reuses_snapshot(self):
        source = (SRC / "moapplication.cpp").read_text(encoding="utf-8")
        setup = function_body(source, "int MOApplication::setup")
        self.assertLess(
            setup.index("WineRuntimeConfig::publish"),
            setup.index("m_plugins->loadPlugins"),
        )
        self.assertIn("WineRuntimeConfig::current", setup)
        recovery = setup[setup.index("Restore any stale INI/save backups") :]
        self.assertNotIn("FluorineConfig::prefixPath", recovery)
        self.assertNotIn('"fluorine/prefix_path"', recovery)

    def test_organizer_launch_does_not_reresolve_configuration(self):
        source = (SRC / "organizercore.cpp").read_text(encoding="utf-8")
        before = function_body(source, "OrganizerCore::beforeRun")
        self.assertIn("wineRuntime.prefixPath", before)
        self.assertIn("WineRuntimeConfig::revalidate", before)
        self.assertNotIn("FluorineConfig::", before)
        self.assertNotIn("resolveWinePrefixPath", source)
        route = function_body(source, "QString resolveWineDataDirName")
        self.assertIn("docsDir.dirName()", route)
        self.assertNotIn("docsDir.exists()", route)
        plugin_routes = function_body(source, "QStringList resolveWinePluginDataDirs")
        self.assertIn("mapping.destination", plugin_routes)
        self.assertIn("mapping.source", plugin_routes)
        self.assertIn("Qt::CaseInsensitive", plugin_routes)
        self.assertIn("canonicalExpected", plugin_routes)
        self.assertIn("return result", plugin_routes)
        documents_route = function_body(
            source, "QString resolvePrefixGameDocumentsDir"
        )
        self.assertIn("managedGame->documentsDirectory().absolutePath()", documents_route)
        self.assertIn("relative.startsWith(\"../\")", documents_route)
        self.assertNotIn("prefix.myGamesPath()", documents_route)
        runner = (SRC / "processrunner.cpp").read_text(encoding="utf-8")
        run = function_body(runner, "ProcessRunner::runBinary")
        self.assertIn("game->isNativeLinux() && !m_sp.useProton", run)
        self.assertIn("m_sp.wineRuntime = {}", run)
        self.assertIn("prefixOwnershipRequired = true", before)
        self.assertIn("coreOwnedIniMappingTargets", before)
        self.assertIn("coreOwnedPluginMappingTargets", before)
        self.assertLess(
            before.index("coreOwnedPluginMappingTargets = pluginProjectionTargets"),
            before.index("const auto launchMappings"),
        )
        launch_mappings = before[
            before.index("const auto launchMappings") :
            before.index("// VFS Root Builder")
        ]
        self.assertIn("coreOwnedPluginMappingTargets", launch_mappings)
        self.assertIn("coreOwnedFileMappingTargets", launch_mappings)
        self.assertIn("mapping.destination", launch_mappings)
        self.assertIn("Qt::CaseInsensitive", launch_mappings)
        self.assertLess(
            before.index("const auto launchMappings"),
            before.index("m_USVFS.updateMapping"),
        )
        self.assertLess(
            before.index("leasePathFor(preparedPrefix"),
            before.index("checkGameRegistryKey(wineRuntime)"),
        )
        ownership = before[
            before.index("leasePathFor(preparedPrefix") :
            before.index("checkGameRegistryKey(wineRuntime)")
        ]
        self.assertIn("setStaleLockTime(0)", ownership)
        self.assertIn("WineRuntimeConfig::revalidatePrefix", ownership)
        self.assertNotIn(
            "localSettingsEnabled() && !vfsOwnedSaves", before
        )
        self.assertIn("has no exact launch-profile VFS mapping", before)
        wine_prefix = (SRC / "wineprefix.cpp").read_text(encoding="utf-8")
        self.assertEqual(wine_prefix.count("leasePathFor("),
                         wine_prefix.count("setStaleLockTime(0)"))

    def test_gamebryo_uses_only_selected_runtime_paths(self):
        source = (GAMEBRYO / "gamebryo" / "gamegamebryo.cpp").read_text(
            encoding="utf-8"
        )
        local_app = function_body(source, "QString GameGamebryo::localAppFolder")
        my_games = function_body(
            source, "QString GameGamebryo::determineMyGamesPath"
        )
        for body in (local_app, my_games):
            self.assertIn("runtimeWineUserProfile", body)
            self.assertNotIn("compatdata", body)
            self.assertNotIn("mkpath", body)
        self.assertNotIn("readFluorinePrefixPath", source)
        self.assertIn("if (appData.isEmpty())", function_body(
            source, "MappingType GameGamebryo::mappings"
        ))
        self.assertIn("m_MyGamesPath.isEmpty()", function_body(
            source, "QDir GameGamebryo::savesDirectory"
        ))
        self.assertIn("m_MyGamesPath.isEmpty()", function_body(
            source, "bool GameGamebryo::prepareIni"
        ))
        for relative in (
            "games/enderalse/gameenderalse.cpp",
            "games/fallout4/gamefallout4.cpp",
            "games/fallout4london/gamefo4london.cpp",
            "games/falloutnv/gamefalloutnv.cpp",
            "games/skyrimse/gameskyrimse.cpp",
            "games/skyrimvr/gameskyrimvr.cpp",
            "games/starfield/gamestarfield.cpp",
            "games/ttw/gamefalloutttw.cpp",
        ):
            mapping_source = (GAMEBRYO / relative).read_text(encoding="utf-8")
            self.assertIn("if (appData.isEmpty())", function_body(
                mapping_source, "::mappings"
            ))

    def test_basic_games_never_scan_or_root_unresolved_tokens(self):
        source = BASIC.read_text(encoding="utf-8")
        self.assertIn('app.property("fluorineWineUserProfilePath")', source)
        self.assertNotIn(".local/share/fluorine/Prefix", source)
        self.assertNotIn("config.json", source)
        self.assertNotIn('replace("%USERPROFILE%", "")', source)
        self.assertIn("raise RuntimeError", source)
        self.assertIn("not game.isNativeLinux()", source)
        default_documents = source[source.index("def _default_documents_directory") :]
        self.assertIn("_find_wine_userprofile()", default_documents)
        self.assertIn('"Documents", "My Games"', default_documents)
        borderlands = (
            ROOT / "libs/basic_games/games/game_borderlands1.py"
        ).read_text(encoding="utf-8")
        self.assertNotIn(
            'GameIniFiles = "%GAME_DOCUMENTS%/WillowGame/Config"', borderlands
        )
        self.assertIn("WillowEngine.ini", borderlands)

    def test_linux_variant_probes_use_host_path_joining(self):
        for relative in (
            "games/enderalse/gameenderalse.cpp",
            "games/falloutnv/gamefalloutnv.cpp",
            "games/ttw/gamefalloutttw.cpp",
        ):
            source = (GAMEBRYO / relative).read_text(encoding="utf-8")
            check = function_body(source, "::checkVariants")
            self.assertIn("QDir(m_GamePath).filePath", check)
            self.assertNotIn('m_GamePath + "\\\\', check)

        enderal_se = (
            GAMEBRYO / "games/enderalse/gameenderalse.cpp"
        ).read_text(encoding="utf-8")
        initialize = function_body(enderal_se, "GameEnderalSE::initializeProfile")
        self.assertIn(
            "QDir(localAppFolder()).filePath(gameDirectoryName())", initialize
        )

        fallout76 = (
            GAMEBRYO / "games/fallout76/gamefallout76.cpp"
        ).read_text(encoding="utf-8")
        local_app_name = function_body(fallout76, "GameFallout76::localAppName")
        self.assertIn('QStringLiteral("Fallout76")', local_app_name)

    def test_runtime_lifecycle_and_cli_inputs_fail_closed(self):
        settings = (SRC / "settingsdialogproton.cpp").read_text(encoding="utf-8")
        recreate = function_body(
            settings, "void ProtonSettingsTab::onRecreatePrefix"
        )
        self.assertIn("runtime.valid()", recreate)
        self.assertIn("runtime.protonPath", recreate)
        self.assertIn("runtime.compatDataPath.isEmpty()", recreate)
        self.assertIn("Direct Prefix Cannot Be Recreated", recreate)
        self.assertLess(
            recreate.index("resetPrefixForRecreation"),
            recreate.index("markRuntimeLifecycleChanged"),
        )
        main = (SRC / "mainwindow.cpp").read_text(encoding="utf-8")
        self.assertIn("runtimeLifecycleChanged", main)
        self.assertIn("ExitModOrganizer(Exit::Restart)", main)
        settings_handler = function_body(
            main, "void MainWindow::on_actionSettings_triggered"
        )
        self.assertIn(
            "tryAcquireQuiescentConfigurationLease", settings_handler
        )
        update_failure = settings_handler[
            settings_handler.index("if (!updatesSucceeded)") :
            settings_handler.index("if (oldManagedGameDirectory")
        ]
        self.assertIn("runtimeLifecycleChanged", update_failure)
        self.assertIn("ExitModOrganizer(Exit::Restart)", update_failure)
        commandline = (SRC / "commandline.cpp").read_text(encoding="utf-8")
        self.assertIn("--prefix must not be empty", commandline)
        self.assertIn("--proton must not be empty", commandline)
        proton_settings = (SRC / "settingsdialogproton.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("acquirePrefixMutationLock", proton_settings)
        self.assertIn("acquireOwnedIncompletePrefixMutationLocks",
                      proton_settings)
        self.assertIn("hasPersistedSessionLease", proton_settings)
        self.assertIn("settingsLockPath(configPath)", proton_settings)
        winetricks = function_body(
            proton_settings, "void ProtonSettingsTab::onWinetricks"
        )
        self.assertNotIn("startDetached", winetricks)
        self.assertIn("mutationLocks", winetricks)
        self.assertIn("beginSessionLease", winetricks)
        self.assertIn("endSessionLease", winetricks)
        self.assertIn("beginRuntimeMutation", winetricks)
        self.assertIn("endRuntimeMutation", winetricks)
        setup = function_body(
            proton_settings, "void ProtonSettingsTab::runPrefixSetupDialog"
        )
        self.assertIn("beginSessionLease", setup)
        self.assertIn("endSessionLease", setup)
        self.assertIn("dialog.quiescent()", setup)
        self.assertLess(setup.index("beginSessionLease"),
                        setup.index("PrefixSetupDialog dialog"))
        prefix_dialog = (SRC / "prefixsetupdialog.cpp").read_text(
            encoding="utf-8"
        )
        reject = function_body(prefix_dialog, "void PrefixSetupDialog::reject")
        close = function_body(prefix_dialog,
                              "void PrefixSetupDialog::closeEvent")
        finished = function_body(prefix_dialog,
                                 "void PrefixSetupDialog::onFinished")
        self.assertIn("m_closeWhenFinished", reject)
        self.assertIn("onCancel()", reject)
        self.assertIn("event->ignore()", close)
        self.assertIn("m_closeWhenFinished", finished)
        settings_dialog = (SRC / "settingsdialog.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("m_runtimeMutationActive", function_body(
            settings_dialog, "void SettingsDialog::accept"
        ))
        self.assertIn("m_runtimeMutationActive", function_body(
            settings_dialog, "void SettingsDialog::reject"
        ))


if __name__ == "__main__":
    unittest.main()
