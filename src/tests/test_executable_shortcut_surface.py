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
    def test_options_menu_keeps_pin_and_desktop_shortcut_actions(self) -> None:
        ui = ET.parse(SOURCE_DIR / "mainwindow.ui").getroot()
        options_button = ui.find(".//widget[@name='linkButton']")
        self.assertIsNotNone(options_button)
        assert options_button is not None
        self.assertEqual(widget_property(options_button, "text"), "Options")
        self.assertIn("manage shortcuts", widget_property(options_button, "toolTip"))

        source = (SOURCE_DIR / "mainwindow.cpp").read_text(encoding="utf-8")
        header = (SOURCE_DIR / "mainwindow.h").read_text(encoding="utf-8")
        self.assertIn('tr("Pin to Quick Access")', source)
        self.assertIn('tr("Desktop shortcut")', source)
        self.assertIn('tr("Application menu shortcut")', source)
        self.assertIn('SLOT(linkRunMenu())', source)
        self.assertIn('SLOT(linkDesktop())', source)
        self.assertIn('SLOT(linkMenu())', source)
        self.assertIn("void linkDesktop();", header)
        self.assertIn("void linkMenu();", header)

        run_menu = source.split("void MainWindow::linkRunMenu()", 1)[1]
        run_menu = run_menu.split("void MainWindow::linkDesktop()", 1)[0]
        self.assertIn("setShownOnToolbar(", run_menu)
        desktop = source.split("void MainWindow::linkDesktop()", 1)[1]
        desktop = desktop.split("void MainWindow::linkMenu()", 1)[0]
        self.assertIn("Shortcut::Desktop", desktop)
        application_menu = source.split("void MainWindow::linkMenu()", 1)[1]
        application_menu = application_menu.split("void MainWindow::updateLaunchMenu()", 1)[0]
        self.assertIn("Shortcut::ApplicationMenu", application_menu)

    def test_options_menu_has_steam_launch_option_actions(self) -> None:
        source = (SOURCE_DIR / "mainwindow.cpp").read_text(encoding="utf-8")
        header = (SOURCE_DIR / "mainwindow.h").read_text(encoding="utf-8")
        self.assertIn('tr("Steam launch option")', source)
        self.assertIn('tr("Copy launch option")', source)
        self.assertIn('tr("Set in Steam\u2026")', source)
        # QMenu hides action tooltips (the disabled "no Steam app id" reason) unless enabled.
        self.assertIn("steamMenu->setToolTipsVisible(true);", source)
        self.assertIn("void copySteamLaunchOption();", header)
        self.assertIn("void setSteamLaunchOption();", header)
        self.assertIn("QAction* m_SteamCopy{nullptr};", header)
        self.assertIn("QAction* m_SteamSet{nullptr};", header)

    def test_application_icon_setting_still_controls_shortcut_icon(self) -> None:
        edit_ui = ET.parse(SOURCE_DIR / "editexecutablesdialog.ui").getroot()
        self.assertIsNotNone(edit_ui.find(".//widget[@name='useApplicationIcon']"))
        edit_source = (SOURCE_DIR / "editexecutablesdialog.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("ui->useApplicationIcon, &QCheckBox::toggled", edit_source)
        self.assertIn("ui->useApplicationIcon->setChecked(e.usesOwnIcon())", edit_source)
        self.assertIn("Executable::UseApplicationIcon", edit_source)

        executable_header = (SOURCE_DIR / "executableslist.h").read_text(
            encoding="utf-8"
        )
        executable_source = (SOURCE_DIR / "executableslist.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("UseApplicationIcon", executable_header)
        self.assertIn('map["ownicon"]', executable_source)

    def test_existing_moshortcut_consumer_remains_supported(self) -> None:
        parser = (SOURCE_DIR / "moshortcut.cpp").read_text(encoding="utf-8")
        commandline = (SOURCE_DIR / "commandline.cpp").read_text(encoding="utf-8")
        application = (SOURCE_DIR / "moapplication.cpp").read_text(encoding="utf-8")
        runner = (SOURCE_DIR / "processrunner.cpp").read_text(encoding="utf-8")

        self.assertIn('link.startsWith("moshortcut://")', parser)
        self.assertIn(".setFromShortcut(m_shortcut)", commandline)
        self.assertIn(".setFromShortcut(moshortcut)", application)
        self.assertIn("ProcessRunner::setFromShortcut", runner)

if __name__ == "__main__":
    unittest.main()
