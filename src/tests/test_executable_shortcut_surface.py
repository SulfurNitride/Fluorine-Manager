#!/usr/bin/env python3

from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


SOURCE_ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIR = SOURCE_ROOT / "src/src"


def widget_property(widget: ET.Element, name: str) -> str:
    prop = widget.find(f"property[@name='{name}']")
    if prop is None or len(prop) != 1:
        raise AssertionError(f"missing {name!r} on {widget.attrib.get('name')!r}")
    return prop[0].text or ""


class ExecutableShortcutSurfaceTests(unittest.TestCase):
    def test_link_button_only_controls_internal_pinning(self) -> None:
        ui = ET.parse(SOURCE_DIR / "mainwindow.ui").getroot()
        link_button = ui.find(".//widget[@name='linkButton']")
        self.assertIsNotNone(link_button)
        assert link_button is not None

        self.assertEqual(widget_property(link_button, "text"), "Pin")
        self.assertEqual(
            widget_property(link_button, "toolTip"),
            "Show or hide the selected program on the toolbar and Run menu",
        )
        self.assertEqual(
            widget_property(link_button, "whatsThis"),
            "Pin or unpin the selected program inside Fluorine.",
        )

        source = (SOURCE_DIR / "mainwindow.cpp").read_text(encoding="utf-8")
        header = (SOURCE_DIR / "mainwindow.h").read_text(encoding="utf-8")
        direct_connection = (
            "connect(ui->linkButton, &QPushButton::clicked, this, "
            "&MainWindow::linkToolbar);"
        )
        self.assertEqual(source.count(direct_connection), 1)
        self.assertIn("void MainWindow::updateLinkButtonState()", source)
        self.assertIn('pinned ? tr("Unpin") : tr("Pin")', source)
        self.assertNotIn("on_linkButton_pressed", source + header)

        refresh = source.split("void MainWindow::refreshExecutablesList()", 1)[1]
        refresh = refresh.split("static bool BySortValue", 1)[0]
        self.assertIn("updateLinkButtonState();", refresh)

        selection = source.split(
            "void MainWindow::on_executablesListBox_currentIndexChanged", 1
        )[1]
        selection = selection.split("void MainWindow::helpTriggered()", 1)[0]
        self.assertIn("updateLinkButtonState();", selection)

        toggle = source.split("void MainWindow::linkToolbar()", 1)[1]
        toggle = toggle.split("void MainWindow::updateLinkButtonState()", 1)[0]
        self.assertEqual(toggle.count("setShownOnToolbar("), 1)
        self.assertIn("updateLinkButtonState();", toggle)

        language_change = source.split("void MainWindow::languageChange(", 1)[1]
        language_change = language_change.split("void MainWindow::originModified", 1)[0]
        self.assertIn(
            "ui->retranslateUi(this);\n  updateLinkButtonState();", language_change
        )
        for retired in (
            "env::Shortcut",
            "envshortcut.h",
            "linkDesktop",
            "linkMenu",
            "m_LinkDesktop",
            "m_LinkStartMenu",
            'tr("Application Launcher")',
        ):
            self.assertNotIn(retired, source + header)

    def test_external_publisher_and_dead_icon_option_are_retired(self) -> None:
        self.assertFalse((SOURCE_DIR / "envshortcut.cpp").exists())
        self.assertFalse((SOURCE_DIR / "envshortcut.h").exists())
        self.assertNotIn(
            "envshortcut.h",
            (SOURCE_DIR / "env.cpp").read_text(encoding="utf-8"),
        )

        edit_ui = ET.parse(SOURCE_DIR / "editexecutablesdialog.ui").getroot()
        self.assertIsNone(edit_ui.find(".//widget[@name='useApplicationIcon']"))
        edit_source = (SOURCE_DIR / "editexecutablesdialog.cpp").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("useApplicationIcon", edit_source)

        # Preserve the stored flag and public plugin API for existing configs.
        executable_header = (SOURCE_DIR / "executableslist.h").read_text(
            encoding="utf-8"
        )
        executable_source = (SOURCE_DIR / "executableslist.cpp").read_text(
            encoding="utf-8"
        )
        plugin_api = (
            SOURCE_ROOT / "libs/uibase/include/uibase/iexecutable.h"
        ).read_text(encoding="utf-8")
        self.assertIn("UseApplicationIcon", executable_header)
        self.assertIn('map["ownicon"]', executable_source)
        self.assertIn("usesOwnIcon()", plugin_api)

    def test_historical_shortcut_consumer_remains_supported(self) -> None:
        parser = (SOURCE_DIR / "moshortcut.cpp").read_text(encoding="utf-8")
        commandline = (SOURCE_DIR / "commandline.cpp").read_text(encoding="utf-8")
        application = (SOURCE_DIR / "moapplication.cpp").read_text(encoding="utf-8")
        runner = (SOURCE_DIR / "processrunner.cpp").read_text(encoding="utf-8")

        self.assertIn('link.startsWith("moshortcut://")', parser)
        self.assertIn(".setFromShortcut(m_shortcut)", commandline)
        self.assertIn(".setFromShortcut(moshortcut)", application)
        self.assertIn("ProcessRunner::setFromShortcut", runner)

    def test_user_facing_text_does_not_promise_external_publication(self) -> None:
        tutorial = (
            SOURCE_DIR / "tutorials/tutorial_primer_main.js"
        ).read_text(encoding="utf-8")
        self.assertIn("Pin or unpin the selected program", tutorial)
        self.assertNotIn("Windows Desktop", tutorial)
        self.assertNotIn("Start Menu", tutorial)

        translations = ET.parse(SOURCE_DIR / "organizer_en.ts").getroot()
        sources = {message.findtext("source") for message in translations.findall(".//message")}
        self.assertIn("Pin", sources)
        self.assertIn(
            "Show or hide the selected program on the toolbar and Run menu", sources
        )
        self.assertIn("Unpin", sources)
        self.assertNotIn("Use application's icon for desktop shortcuts", sources)
        self.assertNotIn(
            "Create a shortcut in your start menu or on the desktop to the specified program",
            sources,
        )
        self.assertFalse(any("shortcuts created here" in (source or "") for source in sources))

        installation = (SOURCE_ROOT / "docs/installation.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("external-shortcut publisher was retired", installation)
        self.assertIn("deliberately do not scan or delete", installation)
        self.assertIn("Existing shortcut pairs remain launch-compatible", installation)


if __name__ == "__main__":
    unittest.main()
