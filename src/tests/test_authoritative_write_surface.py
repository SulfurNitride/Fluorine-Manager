from pathlib import Path
import hashlib
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AuthoritativeWriteSurfaceTests(unittest.TestCase):
    def test_bundled_publishers_no_longer_construct_legacy_writer(self):
        sources = [
            "src/src/profile.cpp",
            "src/src/pluginlist.cpp",
            "src/src/mainwindow.cpp",
            "src/src/wineprefix.cpp",
            "src/src/winepluginlistsync.cpp",
            "libs/game_bethesda/src/gamebryo/gamebryogameplugins.cpp",
            "libs/game_bethesda/src/creation/creationgameplugins.cpp",
            "libs/game_bethesda/src/games/enderalse/enderalsegameplugins.cpp",
            "libs/game_bethesda/src/games/starfield/starfieldgameplugins.cpp",
            "libs/game_bethesda/src/games/morrowind/morrowindpluginlistwriter.cpp",
            "libs/installer_bsplugins/src/TESData/PluginList.cpp",
        ]
        legacy_construction = re.compile(r"\bSafeWriteFile\s+[A-Za-z_]")
        for relative in sources:
            text = (ROOT / relative).read_text(encoding="utf-8")
            self.assertIsNone(legacy_construction.search(text), relative)
            self.assertIn("TransactionalWriteFile", text, relative)

    def test_empty_state_decisions_precede_publication(self):
        gamebryo = (
            ROOT
            / "libs/game_bethesda/src/gamebryo/gamebryogameplugins.cpp"
        ).read_text(encoding="utf-8")
        write_list = gamebryo[gamebryo.index("bool GamebryoGamePlugins::writeList") :]
        empty = write_list[write_list.index("if (writtenCount == 0)"):
                           write_list.index("TransactionalWriteFile file")]
        self.assertIn("recordWriteFailure", empty)

        profile = (ROOT / "src/src/profile.cpp").read_text(encoding="utf-8")
        write_modlist = profile[profile.index("void Profile::doWriteModlist()") :]
        self.assertLess(write_modlist.index("if (m_ModStatus.empty())"),
                        write_modlist.index("TransactionalWriteFile file"))

        rename = (ROOT / "src/src/profilemodlistrename.cpp").read_text(
            encoding="utf-8"
        )
        apply = rename[rename.index("Result apply(") :]
        self.assertLess(apply.index("TransactionalWriteFile output(path)"),
                        apply.index("::open("))

    def test_legacy_abi_surface_remains_and_new_writer_is_additive(self):
        header = (
            ROOT / "libs/uibase/include/uibase/safewritefile.h"
        ).read_text(encoding="utf-8")
        source = (ROOT / "libs/uibase/src/safewritefile.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("class QDLLEXPORT SafeWriteFile", header)
        self.assertIn("class DirectWriteFile : public QFile", header)
        self.assertIn("SafeWriteFile::SafeWriteFile", source)

        transactional = (
            ROOT / "libs/uibase/include/uibase/transactionalwritefile.h"
        ).read_text(encoding="utf-8")
        self.assertIn("class QDLLEXPORT TransactionalWriteFile final", transactional)
        self.assertIn("replaceWith(QByteArrayView contents)", transactional)

        self.assertEqual(
            hashlib.sha256((ROOT / "libs/uibase/include/uibase/safewritefile.h")
                           .read_bytes()).hexdigest(),
            "f40e0caf38873fd21130b9410e35c5d8dd7a33d1f285c7914abad1d84d9dc476",
        )
        self.assertEqual(
            hashlib.sha256((ROOT / "libs/uibase/src/safewritefile.cpp")
                           .read_bytes()).hexdigest(),
            "44d5a158bdb99424929c09f7ebd63806a87a1b7e548f189b119e02048e26af73",
        )

    def test_morrowind_and_wine_do_not_publish_in_place(self):
        morrowind = (
            ROOT
            / "libs/game_bethesda/src/games/morrowind/morrowindgameplugins.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("WriteRegistryValue", morrowind)
        self.assertNotIn('settings.remove("Game Files")', morrowind)
        self.assertIn("MorrowindPluginListWriter::publish", morrowind)

        wine = (ROOT / "src/src/wineprefix.cpp").read_text(encoding="utf-8")
        sync = wine[wine.index("bool WinePrefix::syncPluginsBack") :]
        self.assertNotIn("QFile::remove(sibling)", sync)
        self.assertNotIn("QFile::copy(newest, sibling)", sync)
        self.assertIn("WinePluginListSync::publish", sync)

    def test_plugin_warnings_follow_terminal_publication(self):
        source = (
            ROOT
            / "libs/game_bethesda/src/gamebryo/gamebryogameplugins.cpp"
        ).read_text(encoding="utf-8")
        function = source[source.index(
            "void GamebryoGamePlugins::writePluginLists"
        ):source.index("void GamebryoGamePlugins::readPluginLists")]
        self.assertLess(function.index("writePluginList"),
                        function.index("reportWriteFailure"))
        self.assertLess(function.index("writeLoadOrderList"),
                        function.index("reportInvalidFileNames"))

        for relative in [
            "libs/game_bethesda/src/creation/creationgameplugins.cpp",
            "libs/game_bethesda/src/games/enderalse/enderalsegameplugins.cpp",
            "libs/game_bethesda/src/games/morrowind/morrowindgameplugins.cpp",
            "libs/game_bethesda/src/games/starfield/starfieldgameplugins.cpp",
        ]:
            self.assertNotIn("reportError(",
                             (ROOT / relative).read_text(encoding="utf-8"),
                             relative)

    def test_ini_and_wine_registry_publishers_share_transactional_boundaries(self):
        registry = (ROOT / "libs/uibase/src/registry.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("TransactionalWriteFile transaction(fileName)", registry)
        self.assertIn("transaction.readOriginal", registry)
        self.assertIn("transaction.replaceWith", registry)
        self.assertNotIn("QIODevice::Truncate", registry)

        wine_registry = (ROOT / "src/src/wineregistryfile.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("TransactionalWriteFile transaction(path)", wine_registry)
        self.assertIn("removeDriveMappings", wine_registry)
        self.assertNotIn("QIODevice::Truncate", wine_registry)

        prefix = (ROOT / "src/src/wineprefix.cpp").read_text(encoding="utf-8")
        self.assertIn("WineSaveDeployment::leasePathFor", prefix)
        self.assertIn("WineSaveDeployment::hasPersistedSessionLease", prefix)
        self.assertIn("WineRegistryFile::removeDriveMappings", prefix)

        organizer = (ROOT / "src/src/organizercore.cpp").read_text(
            encoding="utf-8"
        )
        check = organizer[organizer.index("bool OrganizerCore::checkGameRegistryKey"):
                          organizer.index("bool OrganizerCore::beforeRun")]
        self.assertIn("readHklmValues", check)
        self.assertIn("writeHklmValues", check)
        self.assertEqual(check.count("writeHklmValues"), 1)
        before = organizer[organizer.index("bool OrganizerCore::beforeRun"):]
        self.assertIn("prefix.pruneExtraDrives", before)
        self.assertLess(before.index("prefix.pruneExtraDrives"),
                        before.index("WineSaveDeployment::beginSessionLease"))
        self.assertIn("compareExisting", (
            ROOT / "src/src/wineregistryfile.h"
        ).read_text(encoding="utf-8"))

        spawn = (ROOT / "src/src/spawn.cpp").read_text(encoding="utf-8")
        self.assertNotIn("pruneDriveRegistry", spawn)
        self.assertNotIn("pruneExtraDrives(prefixPath)", spawn)

        proton = (ROOT / "src/src/protonlauncher.cpp").read_text(encoding="utf-8")
        self.assertIn("ProtonDxvkConfig::publish", proton)
        self.assertNotIn('filePath("dxvk.conf")', proton)
        self.assertNotIn("QIODevice::Truncate", proton)

        dxvk = (ROOT / "src/src/protondxvkconfig.cpp").read_text(encoding="utf-8")
        self.assertIn("TransactionalWriteFile transaction(path)", dxvk)
        self.assertIn('FileName = ".fluorine-dxvk.conf"', dxvk)
        self.assertIn("transaction.replaceWith", dxvk)

    def test_gamebryo_ini_seeding_preserves_existing_generations(self):
        game = (ROOT / "libs/game_bethesda/src/gamebryo/gamegamebryo.cpp").read_text(
            encoding="utf-8"
        )
        ensure = game[game.index("bool GameGamebryo::ensureIniFilesExist"):
                      game.index("QString GameGamebryo::readIniValue")]
        self.assertIn("GamebryoIniSeeder::ensure", ensure)
        self.assertNotIn("size() > 200", ensure)
        self.assertNotIn("QFile::remove", ensure)

        prepare = game[game.index("bool GameGamebryo::prepareIni"):
                       game.index("bool GameGamebryo::ensureIniFilesExist")]
        self.assertIn("if (!ensureIniFilesExist(basePath))", prepare)
        self.assertIn("if (!MOBase::WriteRegistryValue", prepare)

        invalidation = (
            ROOT / "libs/game_bethesda/src/gamebryo/gamebryobsainvalidation.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("if (!const_cast<GameGamebryo*>(gamebryo)->ensureIniFilesExist",
                      invalidation)

    def test_editable_text_surfaces_publish_complete_encoded_generations(self):
        editor = (ROOT / "src/src/texteditor.cpp").read_text(encoding="utf-8")
        save = editor[editor.index("bool TextEditor::save()"):
                      editor.index("const QString& TextEditor::filename")]
        self.assertIn("encodeTextData", save)
        self.assertIn("TransactionalWriteFile", save)
        self.assertIn("replaceWith", save)
        self.assertNotIn("resize(0)", save)
        self.assertNotIn("QIODevice::WriteOnly", save)
        self.assertLess(save.index("replaceWith"),
                        save.index("document()->setModified(false)"))

        generic = (ROOT / "src/src/modinfodialogtextfiles.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("if (!m_editor->save())", generic)
        self.assertIn("if (!m_editor->load(path))", generic)

        viewer = (ROOT / "libs/uibase/src/textviewer.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("encodeTextData", viewer)
        self.assertIn("TransactionalWriteFile transaction(path)", viewer)
        self.assertIn("if (!trySaveEditor(editor))", viewer)
        self.assertNotIn("QIODevice::Truncate", viewer)
        self.assertIn("mo2TextEncoding", viewer)
        self.assertIn("modificationChanged", viewer)
        self.assertEqual(
            hashlib.sha256(
                (ROOT / "libs/uibase/include/uibase/textviewer.h").read_bytes()
            ).hexdigest(),
            "17737d2222ecff862f5dbfd78af8ff0261557e70d18843217ce4ae7e60deca4a",
        )

        utility = (ROOT / "libs/uibase/src/utility.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("codec = QStringConverter::Encoding::Latin1", utility)
        self.assertIn("bool encodeTextData", utility)


if __name__ == "__main__":
    unittest.main()
