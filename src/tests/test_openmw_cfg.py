from __future__ import annotations

import importlib.util
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = (
    Path(__file__).parents[2]
    / "libs"
    / "basic_games"
    / "games"
    / "openmw_support"
    / "openmw_cfg.py"
)
SPEC = importlib.util.spec_from_file_location("openmw_cfg", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Unable to load {MODULE_PATH}")
openmw_cfg = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(openmw_cfg)


class OpenMWConfigTests(unittest.TestCase):
    def test_selects_only_the_launch_type_config_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            native = directory / "native" / "openmw.cfg"
            flatpak = directory / "flatpak" / "openmw.cfg"
            flatpak.parent.mkdir()
            flatpak.write_text("flatpak\n", encoding="utf-8")

            self.assertIsNone(openmw_cfg.find_openmw_cfg(native, flatpak, False))
            self.assertEqual(
                openmw_cfg.find_openmw_cfg(native, flatpak, True), flatpak
            )

            native.parent.mkdir()
            native.write_text("native\n", encoding="utf-8")
            self.assertEqual(
                openmw_cfg.find_openmw_cfg(native, flatpak, False), native
            )

    def test_normalizes_openmw_player_stub_loadorder(self) -> None:
        self.assertEqual(
            openmw_cfg.normalize_plugin_loadorder(
                [
                    "Addon.omwaddon.esp",
                    "addon.OMWADDON.ESP",
                    "Game.omwgame.ESP",
                    "Scripts.OMWSCRIPTS.esp",
                    "Ordinary.esp",
                ]
            ),
            [
                "Addon.omwaddon",
                "Game.omwgame",
                "Scripts.OMWSCRIPTS",
                "Ordinary.esp",
            ],
        )
        self.assertTrue(openmw_cfg.is_openmw_player_stub("Addon.OMWADDON.esp"))
        self.assertFalse(openmw_cfg.is_openmw_player_stub("Ordinary.esp"))

    def test_loadorder_keeps_ranked_plugins_before_unranked_plugins(self) -> None:
        self.assertEqual(
            openmw_cfg.order_plugins_by_loadorder(
                ["Unranked.omwaddon", "Ranked.omwaddon"],
                ["Duplicate.esp", "duplicate.ESP", "Ranked.omwaddon.esp"],
            ),
            ["Ranked.omwaddon", "Unranked.omwaddon"],
        )

    def test_loadorder_collapses_duplicate_providers(self) -> None:
        self.assertEqual(
            openmw_cfg.order_plugins_by_loadorder(
                ["Addon.omwaddon", "ADDON.OMWADDON"],
                ["Addon.omwaddon.esp"],
            ),
            ["ADDON.OMWADDON"],
        )

    def test_reports_only_unranked_native_content(self) -> None:
        self.assertEqual(
            openmw_cfg.unranked_native_plugins(
                [
                    "Ranked.omwaddon",
                    "Unranked.omwscripts",
                    "Ordinary.esp",
                    "Master.esm",
                    "UNRANKED.OMWSCRIPTS",
                ],
                ["Ranked.omwaddon.esp"],
            ),
            ["Unranked.omwscripts"],
        )
        self.assertEqual(
            openmw_cfg.unranked_native_plugins(
                ["Unranked.omwscripts"], []
            ),
            [],
        )

    def test_formats_bounded_unranked_name_sample(self) -> None:
        self.assertEqual(
            openmw_cfg.format_name_sample(["One", "Two", "Three"], limit=2),
            "'One', 'Two' (+1 more)",
        )

    def test_reads_curated_selection_and_deduplicates_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cfg_path = Path(temporary) / "openmw.cfg"
            cfg_path.write_text(
                "\n".join(
                    (
                        "# preserved profile selection",
                        " Content = Enabled.esp ",
                        "content=enabled.ESP",
                        "groundcover=Grass.esp",
                        "fallback-archive=Morrowind.bsa",
                        "Fallback-Archive=Morrowind - Invalidation.bsa",
                        "data=/ignored",
                    )
                ),
                encoding="utf-8",
            )

            self.assertEqual(
                openmw_cfg.read_openmw_selection(cfg_path),
                {
                    "content": ["Enabled.esp"],
                    "groundcover": ["Grass.esp"],
                    "fallback-archive": [
                        "Morrowind.bsa",
                        "Morrowind - Invalidation.bsa",
                    ],
                },
            )

    def test_parses_source_replacements_without_clearing_same_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cfg_path = Path(temporary) / "openmw.cfg"
            cfg_path.write_text(
                "\n".join(
                    (
                        "content=Discarded.esp",
                        "groundcover=Discarded Grass.esp",
                        "content=Also Discarded.esp",
                        "replace=CONTENT",
                        "groundcover=Kept Grass.esp",
                        "content=Addon.omwaddon.esp",
                        "replace=groundcover",
                        "content=After.esp",
                        "replace=fallback-archive",
                        "replace=fallback-archive",
                    )
                ),
                encoding="utf-8",
            )

            parsed = openmw_cfg.parse_openmw_selection(cfg_path)

            self.assertEqual(
                parsed["content"],
                [
                    "Discarded.esp",
                    "Also Discarded.esp",
                    "Addon.omwaddon",
                    "After.esp",
                ],
            )
            self.assertEqual(
                parsed["groundcover"],
                ["Discarded Grass.esp", "Kept Grass.esp"],
            )
            self.assertEqual(parsed["fallback_archive"], [])
            self.assertEqual(
                parsed["plugin_order"],
                [
                    "Discarded.esp",
                    "Discarded Grass.esp",
                    "Also Discarded.esp",
                    "Kept Grass.esp",
                    "Addon.omwaddon",
                    "After.esp",
                ],
            )
            self.assertTrue(parsed["content_present"])
            self.assertTrue(parsed["groundcover_present"])
            self.assertTrue(parsed["fallback_archive_present"])
            self.assertEqual(
                parsed["replaced_channels"],
                ["content", "groundcover", "fallback-archive"],
            )

    def test_rejects_malformed_openmw_replace_directive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cfg_path = Path(temporary) / "openmw.cfg"
            cfg_path.write_text("replace=content,groundcover\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "Malformed"):
                openmw_cfg.parse_openmw_selection(cfg_path)

    def test_parses_multi_target_openmw_replace_directive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cfg_path = Path(temporary) / "openmw.cfg"
            cfg_path.write_text(
                "content=Old.esp\n"
                "groundcover=Old Grass.esp\n"
                "replace=content groundcover fallback-archive\n",
                encoding="utf-8",
            )

            parsed = openmw_cfg.parse_openmw_selection(cfg_path)

            self.assertEqual(parsed["content"], ["Old.esp"])
            self.assertEqual(parsed["groundcover"], ["Old Grass.esp"])
            self.assertEqual(
                parsed["replaced_channels"],
                ["content", "groundcover", "fallback-archive"],
            )

    def test_parses_effective_config_chain_with_source_replace_semantics(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / "openmw.cfg"
            child_dir = directory / 'Child & "Quoted"'
            nested_dir = directory / "nested"
            child_dir.mkdir()
            nested_dir.mkdir()
            root.write_text(
                "content=Root First.esp\n"
                "groundcover=Root Grass.esp\n"
                f"config={openmw_cfg.escape_data_path(str(child_dir))}\n"
                "content=Root Last.esp\n"
                "fallback-archive=Root.bsa\n",
                encoding="utf-8",
            )
            (child_dir / "openmw.cfg").write_text(
                "content=Child Before.esp\n"
                "replace=content fallback-archive\n"
                "groundcover=Child Grass.esp\n"
                "config=../nested\n"
                "content=Child After.esp\n",
                encoding="utf-8",
            )
            (nested_dir / "openmw.cfg").write_text(
                "content=Nested.esp\n"
                "replace=groundcover\n"
                "groundcover=Nested Grass.esp\n"
                "fallback-archive=Nested.bsa\n",
                encoding="utf-8",
            )

            parsed = openmw_cfg.parse_openmw_selection_chain(root)

            self.assertEqual(
                parsed["content"],
                ["Child Before.esp", "Child After.esp", "Nested.esp"],
            )
            self.assertEqual(parsed["groundcover"], ["Nested Grass.esp"])
            self.assertEqual(parsed["fallback_archive"], ["Nested.bsa"])
            self.assertEqual(
                parsed["plugin_order"],
                [
                    "Child Before.esp",
                    "Child After.esp",
                    "Nested.esp",
                    "Nested Grass.esp",
                ],
            )
            self.assertEqual(
                openmw_cfg.read_openmw_selection(root),
                {
                    "content": parsed["content"],
                    "groundcover": parsed["groundcover"],
                    "fallback-archive": parsed["fallback_archive"],
                },
            )

    def test_config_chain_uses_breadth_first_source_priority(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / "openmw.cfg"
            a = directory / "a"
            b = directory / "b"
            c = directory / "c"
            for child in (a, b, c):
                child.mkdir()
            root.write_text(
                "content=Root.esp\nfallback-archive=Root.bsa\n"
                "config=a\nconfig=b\n",
                encoding="utf-8",
            )
            (a / "openmw.cfg").write_text(
                "content=A.esp\nconfig=../c\n", encoding="utf-8"
            )
            (b / "openmw.cfg").write_text(
                "groundcover=B.esp\n", encoding="utf-8"
            )
            (c / "openmw.cfg").write_text(
                "replace=fallback-archive\ncontent=C.esp\n"
                "fallback-archive=C.bsa\n",
                encoding="utf-8",
            )

            parsed = openmw_cfg.parse_openmw_selection_chain(root)

            self.assertEqual(
                parsed["plugin_order"],
                ["Root.esp", "A.esp", "B.esp", "C.esp"],
            )
            self.assertEqual(parsed["content"], ["Root.esp", "A.esp", "C.esp"])
            self.assertEqual(parsed["groundcover"], ["B.esp"])
            self.assertEqual(parsed["fallback_archive"], ["C.bsa"])

    def test_config_chain_resolves_all_supplied_path_tokens(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / "openmw.cfg"
            token_roots = {
                token: directory / name
                for token, name in (
                    ("?local?", "local"),
                    ("?userconfig?", "userconfig"),
                    ("?userdata?", "userdata"),
                    ("?global?", "global"),
                )
            }
            root.write_text(
                "content=Root.esp\n"
                "config=?local?/profile\n"
                "config=?userconfig?/profile\n"
                "config=?userdata?/profile\n"
                "config=?global?/profile\n",
                encoding="utf-8",
            )
            for index, token_root in enumerate(token_roots.values(), start=1):
                profile = token_root / "profile"
                profile.mkdir(parents=True)
                (profile / "openmw.cfg").write_text(
                    f"content=Token {index}.esp\n", encoding="utf-8"
                )

            parsed = openmw_cfg.parse_openmw_selection_chain(
                root, path_tokens=token_roots
            )

            self.assertEqual(
                parsed["content"],
                [
                    "Root.esp",
                    "Token 1.esp",
                    "Token 2.esp",
                    "Token 3.esp",
                    "Token 4.esp",
                ],
            )

    def test_linux_default_global_token_includes_application_directory(self) -> None:
        tokens = openmw_cfg._default_openmw_path_tokens(
            Path("/tmp/profile/openmw.cfg")
        )

        self.assertEqual(tokens["?global?"], Path("/usr/share/games/openmw"))

    def test_config_chain_rejects_unresolved_path_token(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "openmw.cfg"
            root.write_text("config=?local?/profile\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "Unresolved OpenMW path token"):
                openmw_cfg.parse_openmw_selection_chain(root)

    def test_config_chain_rejects_wrong_case_path_token(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "openmw.cfg"
            root.write_text("config=?USERCONFIG?/profile\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "Unresolved OpenMW path token"):
                openmw_cfg.parse_openmw_selection_chain(
                    root,
                    {"?userconfig?": Path(temporary)},
                )

    def test_config_chain_uses_linux_environment_token_defaults(self) -> None:
        if not openmw_cfg.sys.platform.startswith("linux"):
            self.skipTest("Linux OpenMW defaults only")
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            config_home = directory / "config"
            data_home = directory / "data"
            root_dir = config_home / "openmw"
            config_child = root_dir / "config-child"
            data_child = data_home / "openmw" / "data-child"
            config_child.mkdir(parents=True)
            data_child.mkdir(parents=True)
            root = root_dir / "openmw.cfg"
            root.write_text(
                "config=?userconfig?/config-child\n"
                "config=?userdata?/data-child\n",
                encoding="utf-8",
            )
            (config_child / "openmw.cfg").write_text(
                "content=Config.esp\n", encoding="utf-8"
            )
            (data_child / "openmw.cfg").write_text(
                "content=Data.esp\n", encoding="utf-8"
            )

            with mock.patch.dict(
                os.environ,
                {
                    "XDG_CONFIG_HOME": str(config_home),
                    "XDG_DATA_HOME": str(data_home),
                },
            ):
                parsed = openmw_cfg.parse_openmw_selection_chain(root)

            self.assertEqual(parsed["content"], ["Config.esp", "Data.esp"])

    def test_config_chain_ignores_text_after_quoted_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / "openmw.cfg"
            child = directory / "child"
            child.mkdir()
            root.write_text(
                'content=Root.esp\nconfig="child" ignored trailing text\n',
                encoding="utf-8",
            )
            (child / "openmw.cfg").write_text(
                "content=Child.esp\n", encoding="utf-8"
            )

            parsed = openmw_cfg.parse_openmw_selection_chain(root)

            self.assertEqual(parsed["content"], ["Root.esp", "Child.esp"])

    def test_config_chain_replace_config_discards_intermediate_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / "openmw.cfg"
            first = directory / "first"
            replacement = directory / "replacement"
            first.mkdir()
            replacement.mkdir()
            root.write_text("content=Root.esp\nconfig=first\n", encoding="utf-8")
            (first / "openmw.cfg").write_text(
                "content=Discarded.esp\nconfig=../replacement\n",
                encoding="utf-8",
            )
            (replacement / "openmw.cfg").write_text(
                "replace=config\ncontent=Replacement.esp\n",
                encoding="utf-8",
            )

            parsed = openmw_cfg.parse_openmw_selection_chain(root)

            self.assertEqual(parsed["content"], ["Root.esp", "Replacement.esp"])

    def test_config_chain_rejects_cycles_repeated_sources_and_malformed_paths(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / "openmw.cfg"
            child = directory / "child"
            child.mkdir()
            root.write_text("config=child\n", encoding="utf-8")
            (child / "openmw.cfg").write_text("config=..\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Cycle"):
                openmw_cfg.parse_openmw_selection_chain(root)

            shared = directory / "shared"
            shared.mkdir()
            (shared / "openmw.cfg").write_text(
                "content=Shared.esp\n", encoding="utf-8"
            )
            root.write_text("config=shared\nconfig=./shared\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Ambiguous repeated"):
                openmw_cfg.parse_openmw_selection_chain(root)

            root.write_text('config="unterminated&"\n', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Malformed OpenMW config path"):
                openmw_cfg.parse_openmw_selection_chain(root)

    def test_reads_morrowind_ini_losslessly_and_in_numeric_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            ini_path = Path(temporary) / "Morrowind.ini"
            ini_path.write_bytes(
                (
                    "\ufeff[gAmE fIlEs]\n"
                    "GameFile10=Ten%#;.omwaddon.esp\n"
                    "gamefile2=Two;#%.esp\n"
                    "GAMEFILE2=Two;#%.esp\n"
                    "[aRcHiVeS]\n"
                    "Archive 12=Twelfth.bsa\n"
                    "archive2=Second.bsa\n"
                ).encode("utf-8")
            )

            parsed = openmw_cfg.read_morrowind_ini(ini_path)

            self.assertEqual(
                parsed["game_files"],
                ["Two;#%.esp", "Ten%#;.omwaddon"],
            )
            self.assertEqual(parsed["archives"], ["Second.bsa", "Twelfth.bsa"])
            self.assertTrue(parsed["game_files_present"])
            self.assertTrue(parsed["archives_present"])

    def test_reads_cp1252_morrowind_ini_and_empty_game_files_presence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            ini_path = Path(temporary) / "Morrowind.ini"
            ini_path.write_bytes(
                "[Game Files]\n[Archives]\nArchive 0=Caf\u00e9.bsa\n".encode(
                    "cp1252"
                )
            )

            parsed = openmw_cfg.read_morrowind_ini(ini_path)

            self.assertEqual(parsed["game_files"], [])
            self.assertTrue(parsed["game_files_present"])
            self.assertEqual(parsed["archives"], ["Caf\u00e9.bsa"])

    def test_rejects_conflicting_morrowind_ini_numeric_indices(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            ini_path = Path(temporary) / "Morrowind.ini"
            ini_path.write_text(
                "[Game Files]\nGameFile01=One.esp\nGameFile1=Other.esp\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "Conflicting game files index 1"):
                openmw_cfg.read_morrowind_ini(ini_path)

    def test_rejects_morrowind_ini_undecodable_by_both_encodings(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            ini_path = Path(temporary) / "Morrowind.ini"
            ini_path.write_bytes(b"[Game Files]\nGameFile0=Bad\x81.esp\n")

            with self.assertRaises(UnicodeDecodeError):
                openmw_cfg.read_morrowind_ini(ini_path)

    def test_legacy_plugin_files_are_destubbed_and_empty_is_non_authoritative(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            plugin_path = Path(temporary) / "plugins.txt"
            plugin_path.write_text(
                "# generated\nAddon.omwaddon.esp\naddon.OMWADDON.ESP\n",
                encoding="utf-8",
            )
            empty_path = Path(temporary) / "empty.txt"
            empty_path.write_text("# generated\n", encoding="utf-8")

            self.assertEqual(
                openmw_cfg.read_legacy_plugin_file(plugin_path),
                ["Addon.omwaddon"],
            )
            self.assertEqual(openmw_cfg.read_legacy_plugin_file(empty_path), [])

    def test_filters_curated_content_activation(self) -> None:
        available = [
            "Enabled.esp",
            "Siege at Firemoth.esp",
            "Helm of Tohan Naturalized.esp",
            "Grass.esp",
        ]
        configured = ["Enabled.esp", "Grass.esp"]

        self.assertEqual(
            openmw_cfg.filter_selected_files(available, configured),
            ["Enabled.esp", "Grass.esp"],
        )

    def test_preserves_curated_archive_order(self) -> None:
        available = [
            "Bloodmoon.bsa",
            "Morrowind - Invalidation.bsa",
            "dynamicsounds.bsa",
            "Morrowind.bsa",
            "Protection From Sun Damage.bsa",
            "Tribunal.bsa",
            "Hiding Vampirism Under Helmets.bsa",
            "Disabled.bsa",
        ]
        configured = [
            "Morrowind.bsa",
            "Tribunal.bsa",
            "Bloodmoon.bsa",
            "Morrowind - Invalidation.bsa",
            "Hiding Vampirism Under Helmets.bsa",
            "Protection From Sun Damage.bsa",
            "dynamicsounds.bsa",
        ]

        self.assertEqual(
            openmw_cfg.order_selected_files(available, configured),
            configured,
        )

    def test_migrates_curated_profile_to_durable_selection(self) -> None:
        configured = {
            "content": ["Morrowind.esm", "Enabled.esp"],
            "groundcover": ["Grass.esp"],
            "fallback-archive": [
                "Morrowind.bsa",
                "Tribunal.bsa",
                "Bloodmoon.bsa",
                "Morrowind - Invalidation.bsa",
                "Enabled Mod.bsa",
            ],
        }
        available_plugins = [
            "Enabled.esp",
            "Siege at Firemoth.esp",
            "Helm of Tohan Naturalized.esp",
            "Grass.esp",
        ]
        state = openmw_cfg.create_selection_state(
            configured,
            loadorder=available_plugins,
            available_plugins=available_plugins,
            available_archives=[
                "Morrowind.bsa",
                "Tribunal.bsa",
                "Bloodmoon.bsa",
                "Morrowind - Invalidation.bsa",
                "Enabled Mod.bsa",
                "Disabled Mod.bsa",
            ],
            supplemental_archives=["Morrowind - Invalidation.bsa"],
        )

        self.assertEqual(
            openmw_cfg.filter_selected_files(
                available_plugins, state["enabled_plugins"]
            ),
            ["Enabled.esp", "Grass.esp"],
        )
        self.assertEqual(state["groundcover"], ["Grass.esp"])
        self.assertNotIn("Disabled Mod.bsa", state["archives"])
        self.assertIn("Siege at Firemoth.esp", state["plugin_order"])
        self.assertNotIn("Siege at Firemoth.esp", state["enabled_plugins"])

    def test_migration_uses_independent_authoritative_sources(self) -> None:
        openmw_selection = {
            "content": ["Overwritten.esp"],
            "groundcover": ["Cfg Grass.esp"],
            "fallback_archive": ["Cfg.bsa"],
            "content_present": True,
            "groundcover_present": True,
            "fallback_archive_present": True,
            "plugin_order": ["Cfg Grass.esp", "Overwritten.esp"],
            "replaced_channels": [],
        }
        morrowind = {
            "game_files": [],
            "archives": ["Ini.bsa"],
            "game_files_present": True,
            "archives_present": True,
        }

        state = openmw_cfg.migrate_selection_state(
            None,
            available_plugins=[
                "Overwritten.esp",
                "Cfg Grass.esp",
                "Txt Grass.omwaddon",
            ],
            available_archives=["Cfg.bsa", "Ini.bsa"],
            morrowind_ini=morrowind,
            openmw_selection=openmw_selection,
            legacy_plugins=["Plugins Txt.esp"],
            legacy_loadorder=[
                "Overwritten.esp",
                "Txt Grass.omwaddon.esp",
            ],
            legacy_groundcover=["Txt Grass.omwaddon.esp"],
            legacy_archives=["Archives Txt.bsa"],
        )

        self.assertEqual(state["enabled_plugins"], ["Txt Grass.omwaddon"])
        self.assertEqual(state["groundcover"], ["Txt Grass.omwaddon"])
        self.assertEqual(state["archives"], ["Cfg.bsa"])
        self.assertEqual(state["content_migration_source"], "Morrowind.ini")
        self.assertEqual(
            state["groundcover_migration_source"], "groundcover.txt"
        )
        self.assertEqual(state["archive_migration_source"], "openmw.cfg")
        self.assertEqual(state["order_migration_source"], "loadorder.txt")
        self.assertNotIn("Txt Grass.omwaddon.esp", state["plugin_order"])

    def test_migration_merges_archive_lists_before_morrowind_ini(self) -> None:
        state = openmw_cfg.migrate_selection_state(
            None,
            available_plugins=[],
            available_archives=[],
            morrowind_ini={
                "game_files": [],
                "archives": ["shared.BSA", "Ini.bsa"],
                "game_files_present": False,
                "archives_present": True,
            },
            legacy_archives=["First.bsa", "Shared.bsa"],
        )

        self.assertEqual(
            state["archives"], ["First.bsa", "Shared.bsa", "Ini.bsa"]
        )
        self.assertEqual(
            state["archive_migration_source"], "archives.txt+Morrowind.ini"
        )

    def test_empty_openmw_replacements_are_authoritative(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cfg_path = directory / "openmw.cfg"
            selected_dir = directory / "selected"
            selected_dir.mkdir()
            cfg_path.write_text(
                "groundcover=Grass.esp\n"
                "content=Discarded.esp\n"
                "fallback-archive=Discarded.bsa\n"
                "config=selected\n",
                encoding="utf-8",
            )
            (selected_dir / "openmw.cfg").write_text(
                "replace=content fallback-archive\n",
                encoding="utf-8",
            )
            parsed = openmw_cfg.parse_openmw_selection_chain(cfg_path)

            state = openmw_cfg.migrate_selection_state(
                None,
                available_plugins=["Fallback.esp", "Grass.esp"],
                available_archives=["Default.bsa"],
                openmw_selection=parsed,
                legacy_plugins=["Fallback.esp"],
                legacy_archives=["Legacy.bsa"],
            )

        self.assertEqual(state["enabled_plugins"], ["Grass.esp"])
        self.assertEqual(state["groundcover"], ["Grass.esp"])
        self.assertEqual(state["archives"], [])
        self.assertEqual(state["content_migration_source"], "openmw.cfg")
        self.assertEqual(state["groundcover_migration_source"], "openmw.cfg")
        self.assertEqual(state["archive_migration_source"], "openmw.cfg")
        self.assertEqual(state["order_migration_source"], "openmw.cfg")

    def test_ranked_order_appends_unranked_identities_by_inventory_name(self) -> None:
        state = openmw_cfg.migrate_selection_state(
            None,
            available_plugins=["Zulu.esp", "Ranked.esp", "Alpha.esp"],
            available_archives=[],
            primary_plugins=["Primary.esm"],
            morrowind_ini={
                "game_files": ["Zulu.esp", "Alpha.esp"],
                "archives": [],
                "game_files_present": True,
                "archives_present": False,
            },
            legacy_loadorder=["Ranked.esp"],
        )

        self.assertEqual(
            state["plugin_order"],
            ["Primary.esm", "Ranked.esp", "Alpha.esp", "Zulu.esp"],
        )
        self.assertEqual(
            state["enabled_plugins"],
            ["Primary.esm", "Alpha.esp", "Zulu.esp"],
        )

    def test_version_two_order_completion_ignores_activation_source_order(self) -> None:
        state = openmw_cfg.migrate_selection_state(
            {
                "version": 2,
                "known_plugins": ["Ranked.esp"],
                "enabled_plugins": ["Zulu.esp", "Alpha.esp"],
                "groundcover": [],
                "known_archives": [],
                "archives": [],
                "profile_config_entries": [],
                "profile_config_entries_known": True,
                "profile_config_terminal": False,
            },
            available_plugins=["Zulu.esp", "Ranked.esp", "Alpha.esp"],
            available_archives=[],
        )

        self.assertEqual(
            state["plugin_order"], ["Ranked.esp", "Alpha.esp", "Zulu.esp"]
        )

    def test_openmw_order_completion_uses_inventory_name_order(self) -> None:
        state = openmw_cfg.migrate_selection_state(
            None,
            available_plugins=["Zulu.esp", "Ranked.esp", "Alpha.esp"],
            available_archives=[],
            openmw_selection={
                "content": ["Ranked.esp"],
                "groundcover": [],
                "fallback_archive": [],
                "content_present": True,
                "groundcover_present": False,
                "fallback_archive_present": False,
                "plugin_order": ["Ranked.esp"],
                "replaced_channels": [],
            },
        )

        self.assertEqual(
            state["plugin_order"], ["Ranked.esp", "Alpha.esp", "Zulu.esp"]
        )
        self.assertEqual(state["enabled_plugins"], ["Ranked.esp"])

    def test_inventory_order_is_independent_of_activation_source_order(self) -> None:
        state = openmw_cfg.migrate_selection_state(
            None,
            available_plugins=["Zulu.esp", "Alpha.esp"],
            available_archives=[],
            morrowind_ini={
                "game_files": ["Zulu.esp", "Alpha.esp"],
                "archives": [],
                "game_files_present": True,
                "archives_present": False,
            },
        )

        self.assertEqual(state["plugin_order"], ["Alpha.esp", "Zulu.esp"])
        self.assertEqual(
            state["enabled_plugins"], ["Alpha.esp", "Zulu.esp"]
        )
        self.assertEqual(state["order_migration_source"], "inventory")

    def test_version_two_state_is_authoritative_and_destubbed(self) -> None:
        state_v2 = {
            "version": 2,
            "known_plugins": ["Disabled.esp", "Addon.omwaddon.esp"],
            "enabled_plugins": ["Addon.omwaddon.esp"],
            "groundcover": [],
            "known_archives": ["Known.bsa", "Disabled.bsa"],
            "archives": [],
            "profile_config_entries": ["config=../nested"],
            "profile_config_entries_known": True,
            "profile_config_terminal": True,
        }

        state = openmw_cfg.migrate_selection_state(
            state_v2,
            available_plugins=["Lower Source.esp"],
            available_archives=["Lower Source.bsa"],
            morrowind_ini={
                "game_files": ["Lower Source.esp"],
                "archives": ["Lower Source.bsa"],
                "game_files_present": True,
                "archives_present": True,
            },
        )

        self.assertEqual(
            state["plugin_order"],
            ["Disabled.esp", "Addon.omwaddon", "Lower Source.esp"],
        )
        self.assertEqual(state["enabled_plugins"], ["Addon.omwaddon"])
        self.assertEqual(state["archives"], [])
        self.assertEqual(state["content_migration_source"], "state-v2")
        self.assertEqual(state["groundcover_migration_source"], "state-v2")
        self.assertEqual(state["archive_migration_source"], "state-v2")
        self.assertEqual(state["order_migration_source"], "state-v2")
        self.assertEqual(state["profile_config_entries"], ["config=../nested"])
        self.assertTrue(state["profile_config_terminal"])

    def test_valid_migrated_v3_never_reimports_legacy_sources(self) -> None:
        state = openmw_cfg.create_selection_state(
            {"content": [], "groundcover": [], "fallback-archive": []},
            loadorder=[],
            available_plugins=["Original.esp"],
            available_archives=[],
        )

        migrated = openmw_cfg.migrate_selection_state(
            state,
            available_plugins=["Different.esp"],
            available_archives=["Different.bsa"],
            legacy_plugins=["Different.esp"],
            legacy_loadorder=["Different.esp"],
            legacy_groundcover=["Different.esp"],
            legacy_archives=["Different.bsa"],
        )

        self.assertEqual(migrated, state)
        self.assertIsNot(migrated, state)

    def test_invalid_state_fails_closed_instead_of_importing_legacy(self) -> None:
        invalid = {
            "version": 3,
            "plugin_order": ["Duplicate.esp", "duplicate.ESP"],
        }

        with self.assertRaises(ValueError):
            openmw_cfg.migrate_selection_state(
                invalid,
                available_plugins=["Fallback.esp"],
                available_archives=[],
                legacy_plugins=["Fallback.esp"],
            )

    def test_selection_survives_missing_files_and_enables_new_files(self) -> None:
        state = openmw_cfg.create_selection_state(
            {
                "content": ["Enabled.esp"],
                "groundcover": ["Grass.esp"],
                "fallback-archive": ["Enabled.bsa"],
            },
            loadorder=["Enabled.esp", "Disabled.esp", "Grass.esp"],
            available_plugins=["Enabled.esp", "Disabled.esp", "Grass.esp"],
            available_archives=["Enabled.bsa", "Disabled.bsa"],
        )

        self.assertFalse(
            openmw_cfg.update_selection_state(
                state,
                available_plugins=["Disabled.esp"],
                available_archives=["Disabled.bsa"],
                groundcover=["Grass.esp"],
            )
        )
        self.assertIn("Enabled.esp", state["enabled_plugins"])
        self.assertIn("Enabled.bsa", state["archives"])

        self.assertTrue(
            openmw_cfg.update_selection_state(
                state,
                available_plugins=["Enabled.esp", "New.omwaddon"],
                available_archives=["Enabled.bsa", "New.bsa"],
                groundcover=["Grass.esp"],
            )
        )
        self.assertIn("New.omwaddon", state["enabled_plugins"])
        self.assertIn("New.bsa", state["archives"])
        self.assertNotIn("Disabled.esp", state["enabled_plugins"])
        self.assertNotIn("Disabled.bsa", state["archives"])

    def test_selection_state_round_trips_atomically(self) -> None:
        state = openmw_cfg.create_selection_state(
            {
                "content": ["Enabled.esp"],
                "groundcover": [],
                "fallback-archive": ["Enabled.bsa"],
            },
            loadorder=["Enabled.esp"],
            available_plugins=["Enabled.esp"],
            available_archives=["Enabled.bsa"],
        )
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "fluorine-openmw-selection.json"
            openmw_cfg.write_selection_state(state_path, state)

            self.assertEqual(openmw_cfg.read_selection_state(state_path), state)
            self.assertTrue(state_path.read_text(encoding="utf-8").endswith("\n"))

    def test_strict_v3_validation_rejects_bad_plugin_identity(self) -> None:
        state = openmw_cfg.create_selection_state(
            {
                "content": ["Enabled.esp"],
                "groundcover": [],
                "fallback-archive": [],
            },
            loadorder=["Enabled.esp"],
            available_plugins=["Enabled.esp"],
            available_archives=[],
        )

        corruptions = (
            ("plugin_order", ["Enabled.esp", "enabled.ESP"]),
            ("enabled_plugins", ["Enabled.esp", "enabled.ESP"]),
            ("groundcover", ["Enabled.esp", "enabled.ESP"]),
            ("plugin_order", ["Addon.omwaddon.esp"]),
            ("enabled_plugins", ["Missing.esp"]),
            ("groundcover", ["Missing.esp"]),
        )
        for field, value in corruptions:
            with self.subTest(field=field, value=value):
                invalid = json.loads(json.dumps(state))
                invalid[field] = value
                with self.assertRaises(ValueError):
                    openmw_cfg.validate_selection_state(invalid)

        invalid = json.loads(json.dumps(state))
        invalid["known_plugins"] = ["Enabled.esp"]
        with self.assertRaises(ValueError):
            openmw_cfg.validate_selection_state(invalid)

    def test_inventory_reconciliation_is_pure_and_retains_unavailable(self) -> None:
        state = openmw_cfg.create_selection_state(
            {
                "content": ["Old Enabled.esp", "Missing.esp"],
                "groundcover": ["Missing.esp"],
                "fallback-archive": [],
            },
            loadorder=["Old Disabled.esp", "Old Enabled.esp", "Missing.esp"],
            available_plugins=[
                "Old Disabled.esp",
                "Old Enabled.esp",
                "Missing.esp",
            ],
            available_archives=[],
        )
        original = json.loads(json.dumps(state))

        result = openmw_cfg.reconcile_plugin_inventory(
            state,
            available_plugins=[
                "old enabled.ESP",
                "OLD ENABLED.esp",
                "Primary.esm",
                "z.esp",
                "A.esp",
                "Native.omwaddon.esp",
                "Native.omwaddon",
                "Only.omwscripts.esp",
            ],
            primary_plugins=["Primary.esm"],
        )

        self.assertEqual(state, original)
        self.assertEqual(
            result["state"]["plugin_order"],
            [
                "Primary.esm",
                "Old Disabled.esp",
                "OLD ENABLED.esp",
                "Missing.esp",
                "A.esp",
                "Native.omwaddon",
                "Only.omwscripts",
                "z.esp",
            ],
        )
        self.assertEqual(result["state"]["groundcover"], ["Missing.esp"])
        self.assertIn("Missing.esp", result["unavailable_plugins"])
        self.assertIn("Old Disabled.esp", result["unavailable_plugins"])
        self.assertIn("Only.omwscripts", result["unavailable_plugins"])
        self.assertNotIn("Old Disabled.esp", result["state"]["enabled_plugins"])
        self.assertIn("Primary.esm", result["state"]["enabled_plugins"])
        self.assertEqual(
            result["newly_discovered"],
            [
                "A.esp",
                "Native.omwaddon",
                "Only.omwscripts",
                "Primary.esm",
                "z.esp",
            ],
        )
        self.assertEqual(len(result["alias_diagnostics"]), 2)
        self.assertEqual(len(result["ignored_alias_diagnostics"]), 1)
        self.assertEqual(
            result["ignored_alias_identities"], ["native.omwaddon"]
        )
        self.assertEqual(result["ignored_alias_counts"], {"omwaddon": 1})
        self.assertEqual(len(result["wrapper_only_alias_diagnostics"]), 1)
        self.assertIn(
            "Only.omwscripts.esp",
            result["wrapper_only_alias_diagnostics"][0],
        )
        self.assertEqual(
            result["wrapper_only_alias_identities"], ["only.omwscripts"]
        )
        self.assertEqual(
            result["wrapper_only_alias_counts"], {"omwscripts": 1}
        )
        self.assertTrue(result["duplicate_diagnostics"])
        self.assertEqual(
            result["active_plugins"],
            ["Primary.esm", "OLD ENABLED.esp", "A.esp", "Native.omwaddon", "z.esp"],
        )

        plugins, loadorder = openmw_cfg.project_plugin_lists(
            result["state"],
            result["available_plugins"],
            primary_plugins=["Primary.esm"],
        )
        self.assertEqual(plugins, result["active_plugins"])
        self.assertEqual(loadorder, result["state"]["plugin_order"])

    def test_migrates_version_one_selection_state(self) -> None:
        state_v1 = {
            "version": 1,
            "known_plugins": ["Known.esp"],
            "enabled_plugins": ["Known.esp"],
            "groundcover": [],
            "known_archives": ["Known.bsa", "Disabled.bsa"],
            "archives": ["Known.bsa"],
        }
        with tempfile.TemporaryDirectory() as temporary:
            state_path = Path(temporary) / "fluorine-openmw-selection.json"
            state_path.write_text(json.dumps(state_v1), encoding="utf-8")
            state = openmw_cfg.read_selection_state(state_path)
            self.assertIsNotNone(state)

            self.assertTrue(openmw_cfg.upgrade_selection_state(state))
            self.assertEqual(state["version"], 3)
            self.assertEqual(state["plugin_order"], ["Known.esp"])
            self.assertEqual(
                state["known_archives"], ["Known.bsa", "Disabled.bsa"]
            )
            self.assertEqual(state["archives"], ["Known.bsa"])
            self.assertNotIn("known_plugins", state)
            self.assertEqual(state["profile_config_entries"], [])
            self.assertFalse(state["profile_config_entries_known"])
            self.assertFalse(state["profile_config_terminal"])
            self.assertTrue(state["plugin_state_migrated"])
            self.assertFalse(openmw_cfg.upgrade_selection_state(state))

    def test_suspends_and_restores_exact_profile_config_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cfg_path = Path(temporary) / "openmw.cfg"
            entries = [
                '  Config = "../Nested &"Config"  ',
                "config=?local?/nested",
                "config=?local?/nested",
            ]
            cfg_path.write_text(
                "setting=value\n" + "\n".join(entries) + "\ncontent=Old.esp\n",
                encoding="utf-8",
            )
            state = openmw_cfg.create_selection_state(
                {
                    "content": ["Old.esp"],
                    "groundcover": [],
                    "fallback-archive": [],
                },
                loadorder=["Old.esp"],
                available_plugins=["Old.esp"],
                available_archives=[],
            )

            self.assertTrue(
                openmw_cfg.suspend_profile_config_entries(state, cfg_path)
            )
            self.assertEqual(state["profile_config_entries"], entries)
            self.assertTrue(state["profile_config_entries_known"])
            self.assertTrue(state["profile_config_terminal"])

            openmw_cfg.write_openmw_cfg(
                cfg_path,
                data_dirs=["/data"],
                content_plugins=["Old.esp"],
                strip_config=True,
                vanilla_masters=(),
                vanilla_bsas=(),
            )
            self.assertEqual(
                openmw_cfg.capture_profile_config_entries(cfg_path), []
            )
            self.assertFalse(
                openmw_cfg.suspend_profile_config_entries(state, cfg_path)
            )

            self.assertTrue(
                openmw_cfg.restore_profile_config_entries(
                    cfg_path, state["profile_config_entries"]
                )
            )
            self.assertEqual(
                openmw_cfg.capture_profile_config_entries(cfg_path), entries
            )
            self.assertFalse(
                openmw_cfg.restore_profile_config_entries(
                    cfg_path, state["profile_config_entries"]
                )
            )

    def test_restores_entries_captured_with_unknown_legacy_backup(self) -> None:
        state = openmw_cfg.create_selection_state(
            {"content": [], "groundcover": [], "fallback-archive": []},
            loadorder=[],
            available_plugins=[],
            available_archives=[],
        )
        state["profile_config_entries_known"] = False
        state["profile_config_terminal"] = True
        with tempfile.TemporaryDirectory() as temporary:
            cfg_path = Path(temporary) / "openmw.cfg"
            cfg_path.write_text("config=../re-added\n", encoding="utf-8")

            self.assertTrue(
                openmw_cfg.suspend_profile_config_entries(state, cfg_path)
            )
            self.assertFalse(state["profile_config_entries_known"])
            self.assertEqual(
                state["profile_config_entries"], ["config=../re-added"]
            )
            openmw_cfg.write_openmw_cfg(
                cfg_path,
                data_dirs=["/data"],
                content_plugins=[],
                strip_config=True,
                vanilla_masters=(),
                vanilla_bsas=(),
            )

            self.assertTrue(
                openmw_cfg.restore_profile_config_entries(
                    cfg_path, state["profile_config_entries"]
                )
            )
            self.assertEqual(
                openmw_cfg.capture_profile_config_entries(cfg_path),
                ["config=../re-added"],
            )

    def test_profile_selector_path_round_trips_escaped_characters(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cfg_path = directory / "openmw.cfg"
            profile_path = directory / 'Profile & "Quoted"'

            openmw_cfg.write_profile_selector(cfg_path, profile_path)

            self.assertEqual(
                openmw_cfg.read_profile_selector(cfg_path),
                profile_path.resolve(strict=False),
            )

    def test_config_restoration_rolls_back_with_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cfg_path = directory / "openmw.cfg"
            state_path = directory / "fluorine-openmw-selection.json"
            cfg_path.write_text("terminal=true\n", encoding="utf-8")
            state = openmw_cfg.create_selection_state(
                {"content": [], "groundcover": [], "fallback-archive": []},
                loadorder=[],
                available_plugins=[],
                available_archives=[],
            )
            state["profile_config_entries"] = ["config=../nested"]
            state["profile_config_terminal"] = True
            openmw_cfg.write_selection_state(state_path, state)
            original_state = state_path.read_text(encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "injected failure"):
                with openmw_cfg.rollback_file_changes([cfg_path, state_path]):
                    openmw_cfg.restore_profile_config_entries(
                        cfg_path, state["profile_config_entries"]
                    )
                    state["profile_config_terminal"] = False
                    openmw_cfg.write_selection_state(state_path, state)
                    raise RuntimeError("injected failure")

            self.assertEqual(
                cfg_path.read_text(encoding="utf-8"), "terminal=true\n"
            )
            self.assertEqual(state_path.read_text(encoding="utf-8"), original_state)

    def test_transaction_rolls_back_existing_and_new_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cfg_path = directory / "openmw.cfg"
            launcher_path = directory / "launcher.cfg"
            state_path = directory / "fluorine-openmw-selection.json"
            cfg_path.write_text("original config\n", encoding="utf-8")
            launcher_path.write_text("original launcher\n", encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "injected failure"):
                with openmw_cfg.rollback_file_changes(
                    [cfg_path, launcher_path, state_path, cfg_path]
                ):
                    openmw_cfg._write_lines(cfg_path, ["new config"])
                    openmw_cfg._write_lines(launcher_path, ["new launcher"])
                    openmw_cfg._write_lines(state_path, ["new state"])
                    raise RuntimeError("injected failure")

            self.assertEqual(
                cfg_path.read_text(encoding="utf-8"), "original config\n"
            )
            self.assertEqual(
                launcher_path.read_text(encoding="utf-8"), "original launcher\n"
            )
            self.assertFalse(state_path.exists())
            self.assertEqual(list(directory.glob(".*.rollback")), [])

    def test_transaction_reports_incomplete_rollback_with_fatal_type(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "openmw.cfg"
            target.write_text("original\n", encoding="utf-8")

            with mock.patch.object(
                openmw_cfg.os,
                "replace",
                side_effect=OSError("injected restore failure"),
            ), self.assertRaisesRegex(
                openmw_cfg.TransactionRollbackError, "rollback was incomplete"
            ):
                with openmw_cfg.rollback_file_changes([target]):
                    target.write_text("changed\n", encoding="utf-8")
                    raise ValueError("injected export failure")

    def test_exact_openmw_and_launcher_projection_readers(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cfg_path = directory / "openmw.cfg"
            launcher_path = directory / "launcher.cfg"
            data_dirs = [directory / 'Data & "Files"', directory / "Mod"]
            openmw_cfg.write_openmw_cfg(
                cfg_path,
                data_dirs,
                ["Example.esp"],
                ["Grass.esp"],
                ["Custom.bsa"],
                vanilla_masters=[],
                vanilla_bsas=[],
            )
            openmw_cfg.write_openmw_launcher_cfg(
                launcher_path,
                data_dirs,
                ["Example.esp"],
                ["Custom.bsa"],
                vanilla_masters=[],
                vanilla_bsas=[],
            )

            self.assertEqual(
                openmw_cfg.read_openmw_data_dirs(cfg_path),
                [str(path) for path in data_dirs],
            )
            self.assertEqual(
                openmw_cfg.read_openmw_launcher_profile(launcher_path),
                {
                    "current_profile": "Fluorine",
                    "data": [str(path) for path in data_dirs],
                    "content": ["Example.esp"],
                    "fallback_archive": ["Custom.bsa"],
                },
            )

    def test_launcher_profile_builder_uses_writer_canonicalization(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            launcher_path = directory / "launcher.cfg"
            data = [directory / "Data", directory / "Data", directory / "data"]
            content = ["Addon.esp", "addon.ESP", "Other.omwaddon"]
            archives = ["Custom.bsa", "custom.BSA", "Other.bsa"]
            expected = openmw_cfg.build_openmw_launcher_profile(
                data,
                content,
                archives,
                vanilla_masters=["Game.esm", "game.ESM"],
                vanilla_bsas=["Game.bsa", "game.BSA"],
            )

            self.assertEqual(
                expected,
                {
                    "current_profile": "Fluorine",
                    "data": [str(directory / "Data"), str(directory / "data")],
                    "content": ["Game.esm", "Addon.esp", "Other.omwaddon"],
                    "fallback_archive": [
                        "Game.bsa",
                        "Custom.bsa",
                        "Other.bsa",
                    ],
                },
            )

            openmw_cfg.write_openmw_launcher_cfg(
                launcher_path,
                data,
                content,
                archives,
                vanilla_masters=["Game.esm", "game.ESM"],
                vanilla_bsas=["Game.bsa", "game.BSA"],
            )
            self.assertEqual(
                openmw_cfg.read_openmw_launcher_profile(launcher_path), expected
            )

    def test_launcher_profile_reader_streams_and_does_not_modify_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            launcher_path = Path(temporary) / "launcher.cfg"
            launcher_path.write_bytes(
                b"[General]\r\nfirstrun=true\r\n"
                b"[Unrelated]\r\nvalue=\xff\r\n"
                b"[Profiles]\r\ncurrentprofile=Fluorine\r\n"
                b"Other/data=/unrelated\r\n"
                b"Fluorine/data=/game/Data Files\r\n"
                b"Fluorine/content=Example.esp"
            )
            before = launcher_path.read_bytes()
            before_stat = launcher_path.stat()

            with mock.patch.object(
                openmw_cfg,
                "_read_lines",
                side_effect=AssertionError("launcher reader must stream"),
            ):
                profile = openmw_cfg.read_openmw_launcher_profile(launcher_path)

            after_stat = launcher_path.stat()
            self.assertEqual(
                profile,
                {
                    "current_profile": "Fluorine",
                    "data": ["/game/Data Files"],
                    "content": ["Example.esp"],
                    "fallback_archive": [],
                },
            )
            self.assertEqual(launcher_path.read_bytes(), before)
            self.assertEqual(after_stat.st_ino, before_stat.st_ino)
            self.assertEqual(after_stat.st_mtime_ns, before_stat.st_mtime_ns)
            self.assertEqual(after_stat.st_ctime_ns, before_stat.st_ctime_ns)
            self.assertEqual(list(launcher_path.parent.glob(".*.tmp")), [])
            self.assertEqual(list(launcher_path.parent.glob(".*.rollback")), [])

    def test_current_launcher_is_excluded_only_from_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            launcher_path = directory / "launcher.cfg"
            root_path = directory / "openmw.cfg"
            root_path.write_text("root\n", encoding="utf-8")
            launcher_path.write_bytes(
                b"[General]\r\nfirstrun=true\r\n"
                b"[Historical Profile]\r\ncomment=preserve me\r\n"
                b"[Profiles]\r\ncurrentprofile=Fluorine\r\n"
                b"Other/data=/unrelated\r\n"
                b"Fluorine/data=/game/Data Files\r\n"
                b"Fluorine/content=Example.esp"
            )
            expected = openmw_cfg.build_openmw_launcher_profile(
                ["/game/Data Files"],
                ["Example.esp"],
                vanilla_masters=[],
                vanilla_bsas=[],
            )
            before = launcher_path.read_bytes()
            before_stat = launcher_path.stat()
            file_roles = {
                "launcher config": launcher_path,
                "root config": root_path,
            }

            launcher_needs_update = not openmw_cfg.openmw_launcher_cfg_is_current(
                launcher_path,
                expected,
            )
            openmw_cfg.validate_file_roles(file_roles)
            transaction_paths = [
                path
                for role, path in file_roles.items()
                if role != "launcher config" or launcher_needs_update
            ]

            self.assertFalse(launcher_needs_update)
            self.assertIn(launcher_path, file_roles.values())
            self.assertNotIn(launcher_path, transaction_paths)
            with mock.patch.object(
                openmw_cfg,
                "_atomic_text_writer",
                side_effect=AssertionError("current launcher must not be written"),
            ):
                with openmw_cfg.rollback_file_changes(transaction_paths):
                    if launcher_needs_update:
                        openmw_cfg.write_openmw_launcher_cfg(
                            launcher_path,
                            ["/game/Data Files"],
                            ["Example.esp"],
                            vanilla_masters=[],
                            vanilla_bsas=[],
                        )

            after_stat = launcher_path.stat()
            self.assertEqual(launcher_path.read_bytes(), before)
            self.assertEqual(after_stat.st_ino, before_stat.st_ino)
            self.assertEqual(after_stat.st_mtime_ns, before_stat.st_mtime_ns)
            self.assertEqual(after_stat.st_ctime_ns, before_stat.st_ctime_ns)
            self.assertEqual(list(directory.glob(".*.tmp")), [])
            self.assertEqual(list(directory.glob(".*.rollback")), [])

    def test_launcher_currentness_includes_general_initialization(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            launcher_path = Path(temporary) / "launcher.cfg"
            expected = openmw_cfg.build_openmw_launcher_profile(
                ["/game/Data"],
                ["Example.esp"],
                vanilla_masters=[],
                vanilla_bsas=[],
            )
            launcher_path.write_text(
                "[Profiles]\n"
                "currentprofile=Fluorine\n"
                "Fluorine/data=/game/Data\n"
                "Fluorine/content=Example.esp\n",
                encoding="utf-8",
            )

            self.assertFalse(
                openmw_cfg.openmw_launcher_cfg_is_current(
                    launcher_path, expected
                )
            )
            openmw_cfg.write_openmw_launcher_cfg(
                launcher_path,
                ["/game/Data"],
                ["Example.esp"],
                vanilla_masters=[],
                vanilla_bsas=[],
            )
            self.assertTrue(
                openmw_cfg.openmw_launcher_cfg_is_current(
                    launcher_path, expected
                )
            )

            launcher_path.write_text(
                launcher_path.read_text(encoding="utf-8").replace(
                    "Example.esp", "Changed.esp"
                ),
                encoding="utf-8",
            )
            self.assertFalse(
                openmw_cfg.openmw_launcher_cfg_is_current(
                    launcher_path, expected
                )
            )

    def test_writer_removes_managed_targets_from_multi_replace(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cfg_path = Path(temporary) / "openmw.cfg"
            cfg_path.write_text(
                "replace=data content custom-option\n",
                encoding="utf-8",
            )

            openmw_cfg.write_openmw_cfg(
                cfg_path,
                [Path(temporary) / "Data"],
                ["Example.esp"],
                vanilla_masters=[],
                vanilla_bsas=[],
                replace_managed=True,
            )

            lines = cfg_path.read_text(encoding="utf-8").splitlines()
            self.assertIn("replace=custom-option", lines)
            self.assertNotIn("replace=data content custom-option", lines)

    def test_transaction_commits_and_removes_snapshots(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cfg_path = directory / "openmw.cfg"
            cfg_path.write_text("original\n", encoding="utf-8")

            with openmw_cfg.rollback_file_changes([cfg_path]):
                backups = list(directory.glob(".*.rollback"))
                self.assertEqual(len(backups), 1)
                self.assertEqual(os.stat(backups[0]).st_ino, os.stat(cfg_path).st_ino)
                openmw_cfg._write_lines(cfg_path, ["committed"])

            self.assertEqual(cfg_path.read_text(encoding="utf-8"), "committed\n")
            self.assertEqual(list(directory.glob(".*.rollback")), [])

    def test_transaction_restores_symlink_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            target_path = directory / "target.cfg"
            link_path = directory / "openmw.cfg"
            target_path.write_text("original\n", encoding="utf-8")
            try:
                link_path.symlink_to(target_path)
            except OSError as error:
                self.skipTest(f"Symlinks unavailable: {error}")

            with self.assertRaisesRegex(RuntimeError, "injected failure"):
                with openmw_cfg.rollback_file_changes([link_path]):
                    openmw_cfg._write_lines(link_path, ["changed"])
                    raise RuntimeError("injected failure")

            self.assertTrue(link_path.is_symlink())
            self.assertEqual(target_path.read_text(encoding="utf-8"), "original\n")
            self.assertEqual(list(directory.glob(".*.rollback")), [])

    def test_late_marker_failure_restores_earlier_write(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cfg_path = directory / "profile.cfg"
            root_path = directory / "root.cfg"
            cfg_path.write_text("original profile\n", encoding="utf-8")
            root_path.write_text(
                "# BEGIN FLUORINE OPENMW LOCAL SAVES\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "without a matching"):
                with openmw_cfg.rollback_file_changes([cfg_path, root_path]):
                    openmw_cfg._write_lines(cfg_path, ["changed profile"])
                    openmw_cfg.write_local_saves(root_path, None)

            self.assertEqual(
                cfg_path.read_text(encoding="utf-8"), "original profile\n"
            )
            self.assertEqual(
                root_path.read_text(encoding="utf-8"),
                "# BEGIN FLUORINE OPENMW LOCAL SAVES\n",
            )

    def test_rejects_export_roles_aliased_through_parent_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            real_profile = directory / "real-profile"
            alias_profile = directory / "alias-profile"
            real_profile.mkdir()
            try:
                alias_profile.symlink_to(real_profile, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"Symlinks unavailable: {error}")

            with self.assertRaisesRegex(ValueError, "resolve to the same file"):
                openmw_cfg.validate_file_roles(
                    {
                        "root config": real_profile / "openmw.cfg",
                        "profile config": alias_profile / "openmw.cfg",
                    }
                )

    def test_rejects_absent_transaction_parent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            missing_path = Path(temporary) / "missing" / "openmw.cfg"

            with self.assertRaisesRegex(ValueError, "parent does not exist"):
                with openmw_cfg.rollback_file_changes([missing_path]):
                    self.fail("Transaction should not start")

            self.assertFalse(missing_path.parent.exists())

    def test_removes_partial_copy_when_snapshot_creation_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cfg_path = directory / "openmw.cfg"
            cfg_path.write_text("original\n", encoding="utf-8")

            def fail_after_partial_copy(_source, destination):
                Path(destination).write_text("partial", encoding="utf-8")
                raise OSError("injected copy failure")

            with mock.patch.object(
                openmw_cfg.os, "link", side_effect=OSError("no hard links")
            ), mock.patch.object(
                openmw_cfg.shutil, "copy2", side_effect=fail_after_partial_copy
            ):
                with self.assertRaisesRegex(OSError, "injected copy failure"):
                    with openmw_cfg.rollback_file_changes([cfg_path]):
                        self.fail("Transaction should not start")

            self.assertEqual(cfg_path.read_text(encoding="utf-8"), "original\n")
            self.assertEqual(list(directory.glob(".*.rollback")), [])

    def test_profile_block_preserves_inherited_data(self) -> None:
        block = openmw_cfg.build_managed_block(
            ["/game/Data Files", "/mods/example"],
            ["Example.esp"],
            replace_managed=True,
        )

        self.assertEqual(
            [line for line in block if line.startswith("replace=")],
            [
                "replace=content",
                "replace=fallback-archive",
                "replace=groundcover",
            ],
        )
        self.assertNotIn("replace=data", block)
        self.assertIn('data="/game/Data Files"', block)
        self.assertIn('data="/mods/example"', block)

    def test_profile_rewrite_removes_stale_data_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            cfg_path = Path(temporary) / "openmw.cfg"
            cfg_path.write_text(
                "\n".join(
                    (
                        'resources="/keep/resources"',
                        "replace=config",
                        "replace = DATA",
                        "replace=content",
                        "replace=groundcover",
                        "replace=fallback-archive",
                        'data="/stale/data"',
                        "content=Stale.esp",
                        "groundcover=StaleGrass.esp",
                        "fallback-archive=Stale.bsa",
                        "",
                    )
                ),
                encoding="utf-8",
            )

            def rewrite() -> str:
                openmw_cfg.write_openmw_cfg(
                    cfg_path,
                    data_dirs=["/game/Data Files", "/mods/current"],
                    content_plugins=["Current.esp"],
                    replace_managed=True,
                    vanilla_masters=(),
                    vanilla_bsas=(),
                )
                return cfg_path.read_text(encoding="utf-8")

            first = rewrite()
            second = rewrite()

            self.assertEqual(first, second)
            self.assertIn('resources="/keep/resources"', first)
            self.assertIn("replace=config", first)
            self.assertNotIn("replace=data", first.lower().replace(" ", ""))
            self.assertEqual(first.count("replace=content\n"), 1)
            self.assertEqual(first.count("replace=fallback-archive\n"), 1)
            self.assertEqual(first.count("replace=groundcover\n"), 1)
            self.assertNotIn("/stale/data", first)
            self.assertNotIn("Stale.esp", first)
            self.assertNotIn("StaleGrass.esp", first)
            self.assertNotIn("Stale.bsa", first)
            self.assertEqual(first.count("data="), 2)
            self.assertIn('data="/game/Data Files"', first)
            self.assertIn('data="/mods/current"', first)
            self.assertIn("content=Current.esp", first)


if __name__ == "__main__":
    unittest.main()
