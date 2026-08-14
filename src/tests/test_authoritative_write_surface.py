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


if __name__ == "__main__":
    unittest.main()
