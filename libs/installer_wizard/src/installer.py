from __future__ import annotations

import os
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Union, cast

from PyQt6 import QtWidgets
from PyQt6.QtWidgets import QApplication

from wizard.runner import WizardRunnerState

import mobase

from .dialog import WizardInstallerDialog
from .runner import make_interpreter
from .utils import make_ini_tweaks, merge_ini_tweaks


class WizardInstaller(mobase.IPluginInstallerSimple):
    """
    This is the actual plugin. MO2 has two types of installer plugin, this one is
    "simple", i.e., it will work directly on the file-tree contained in the archive.
    The purpose of the installer is to take the file-tree from the archive, check if
    it is valid (for this installer) and then modify it if required before extraction.
    """

    # regex used to parse settings
    RE_DESCRIPTION = re.compile(r"select([0-9]+)-description")
    RE_OPTION = re.compile(r"select([0-9]+)-option([0-9]+)")

    _organizer: mobase.IOrganizer

    # list of selected options
    _installerOptions: Dict[str, List[str]]
    _installerUsed: bool

    def __init__(self):
        super().__init__()

    def init(self, organizer: mobase.IOrganizer):
        self._organizer = organizer
        return True

    def name(self):
        return "BAIN Wizard Installer"

    def localizedName(self) -> str:
        return self.tr("BAIN Wizard Installer")

    def author(self):
        return "Holt59"

    def description(self):
        return self.tr("Installer for BAIN archive containing wizard scripts.")

    def version(self):
        return mobase.VersionInfo(1, 0, 2)

    def isActive(self):
        return self._organizer.pluginSetting(self.name(), "enabled")

    def settings(self):
        return [
            mobase.PluginSetting("enabled", "check to enable this plugin", True),
            mobase.PluginSetting(
                "prefer_fomod",
                "prefer FOMOD installer over this one when possible",
                True,
            ),
            mobase.PluginSetting(
                "prefer_omod",
                "prefer OMOD installer over this one when possible",
                False,
            ),
            # above FOMOD?
            mobase.PluginSetting("priority", "priority of this installer", 120),
        ]

    # method for IPluginInstallerSimple

    def priority(self) -> int:
        return cast(int, self._organizer.pluginSetting(self.name(), "priority"))

    def isManualInstaller(self) -> bool:
        return False

    def onInstallationStart(
        self,
        archive: str,
        reinstallation: bool,
        current_mod: Optional[mobase.IModInterface],
    ):
        self._installerUsed = False
        self._installerOptions = {}

        if current_mod:
            settings = current_mod.pluginSettings(self.name())

            # first extract the description
            descriptions: Dict[int, str] = {}
            options: Dict[int, Dict[int, str]] = defaultdict(dict)
            for setting, value in settings.items():
                mdesc = WizardInstaller.RE_DESCRIPTION.match(setting)
                if mdesc:
                    select = int(mdesc.group(1))
                    descriptions[select] = str(value)

                mopt = WizardInstaller.RE_OPTION.match(setting)
                if mopt:
                    select = int(mopt.group(1))
                    index = int(mopt.group(2))
                    options[select][index] = str(value)

            for kdesc, desc in descriptions.items():
                self._installerOptions[desc] = []
                if kdesc in options:
                    for index in sorted(options[kdesc].keys()):
                        self._installerOptions[desc].append(options[kdesc][index])

    def onInstallationEnd(
        self, result: mobase.InstallResult, new_mod: Optional[mobase.IModInterface]
    ):
        if (
            result != mobase.InstallResult.SUCCESS
            or not self._installerUsed
            or not new_mod
        ):
            return

        new_mod.clearPluginSettings(self.name())
        for i, desc in enumerate(self._installerOptions):
            new_mod.setPluginSetting(self.name(), f"select{i}-description", desc)
            for iopt, opt in enumerate(self._installerOptions[desc]):
                new_mod.setPluginSetting(self.name(), f"select{i}-option{iopt}", opt)

    def _hasFomodInstaller(self) -> bool:
        # do not consider the NCC installer
        return self._organizer.isPluginEnabled("Fomod Installer")

    def _hasOmodInstaller(self) -> bool:
        return self._organizer.isPluginEnabled("Omod Installer")

    def _getWizardArchiveBase(
        self, tree: mobase.IFileTree, data_name: str, checker: mobase.ModDataChecker
    ) -> Optional[mobase.IFileTree]:
        """
        Try to find the folder containing wizard.txt.

        Args:
            tree: Tree to look the data folder in.
            data_name: Name of the data folder (e.g., "data" for Bethesda games).
            checker: Checker to use to check if a tree is a data folder.

        Returns:
            The tree corresponding to the folder containing wizard.txt, or None.
        """

        entry = tree.find("wizard.txt", mobase.FileTreeEntry.FILE)

        if entry:
            return tree

        if len(tree) == 1 and isinstance((root := tree[0]), mobase.IFileTree):
            return self._getWizardArchiveBase(root, data_name, checker)

        return None

    def _getEntriesToExtract(
        self,
        tree: mobase.IFileTree,
        extensions: Sequence[str] = ["png", "jpg", "jpeg", "gif", "bmp", "ini"],
    ) -> list[mobase.FileTreeEntry]:
        """
        Retrieve all the entries to extract from the given tree.

        Args:
            tree: The tree.
            extensions: The extensions of files.

        Returns:
            A list of entries corresponding to files with the given extensions.
        """
        entries: list[mobase.FileTreeEntry] = []

        def fn(path: str, entry: mobase.FileTreeEntry):
            if entry.isFile() and entry.hasSuffix(extensions):
                entries.append(entry)
            return mobase.IFileTree.CONTINUE

        tree.walk(fn)

        return entries

    def isArchiveSupported(self, tree: mobase.IFileTree) -> bool:
        """
        Check if the given file-tree (from the archive) can be installed by this
        installer.

        Args:
            tree: The tree to check.

        Returns:
            True if the file-tree can be installed, false otherwise.
        """

        # retrieve the name of the "data" folder
        data_name = self._organizer.managedGame().dataDirectory().dirName()

        # retrieve the mod-data-checker
        checker = self._organizer.gameFeatures().gameFeature(mobase.ModDataChecker)

        # retrieve the base
        base = self._getWizardArchiveBase(tree, data_name, checker)

        if not base:
            return False

        # check FOMOD for priority
        fomod = base.exists("fomod/ModuleConfig.xml")
        if (
            fomod
            and self._hasFomodInstaller()
            and self._organizer.pluginSetting(self.name(), "prefer_fomod")
        ):
            return False

        # TODO: Check OMOD?

        return True

    def install(
        self,
        name: mobase.GuessedString,
        tree: mobase.IFileTree,
        version: str,
        nexus_id: int,
    ) -> Union[mobase.InstallResult, mobase.IFileTree]:
        """
        Perform the actual installation.

        Args:
            name: The "name" of the mod. This can be updated to change the name of the
                mod.
            tree: The original archive tree.
            version: The original version of the mod.
            nexus_id: The original ID of the mod.

        Returns: We either return the modified file-tree (if the installation was
            successful), or a InstallResult otherwise.

        Note: It is also possible to return a tuple (InstallResult, IFileTree, str, int)
            containing where the two last members correspond to the new version and ID
            of the mod, in case those were updated by the installer.
        """

        # retrieve the name of the "data" folder
        data_name = self._organizer.managedGame().dataDirectory().dirName()

        # retrieve the mod-data-checker
        checker = self._organizer.gameFeatures().gameFeature(mobase.ModDataChecker)

        # retrieve the "base" folder
        base = self._getWizardArchiveBase(tree, data_name, checker)
        if not base or not checker:
            return mobase.InstallResult.NOT_ATTEMPTED

        wizard = base.find("wizard.txt")
        if wizard is None:
            return mobase.InstallResult.NOT_ATTEMPTED

        to_extract = self._getEntriesToExtract(tree)

        # extract the script
        paths = self._manager().extractFiles([wizard] + to_extract, silent=False)
        if len(paths) != len(to_extract) + 1:
            return mobase.InstallResult.FAILED

        interpreter = make_interpreter(base, self._organizer)

        script = paths[0]

        dialog = WizardInstallerDialog(
            self._organizer,
            interpreter,
            interpreter.make_top_level_context(Path(script), WizardRunnerState()),
            name,
            {
                Path(entry.path()): Path(path)
                for entry, path in zip(to_extract, paths[1:], strict=True)
                if not path.endswith(".ini")
            },
            self._installerOptions,
            self._parentWidget(),
        )

        dialog.scriptButtonClicked.connect(  # pyright: ignore[reportUnknownMemberType, reportAttributeAccessIssue]
            lambda: os.startfile(script)
        )

        # unlike the official installer, we do not have a "silent" setting, but it is
        # really simple to add it
        if dialog.exec() == QtWidgets.QDialog.DialogCode.Accepted:
            # we update the name with the user specified one
            name.update(dialog.name(), mobase.GuessQuality.USER)

            # create the tree with all the sub-packages
            new_tree = tree.createOrphanTree()

            for subpackage in dialog.subpackages():
                entry = base.find(subpackage)

                # should never happens since we fetch the subpackage for the archive
                if not entry or not isinstance(entry, mobase.IFileTree):
                    print(
                        f"SubPackage {subpackage} not found in the archive.",
                        file=sys.stderr,
                    )
                    continue

                new_tree.merge(entry)

            # handle renames
            for original, new in dialog.renames().items():
                # entry should be at the root
                entry = new_tree.find(original)

                if not entry:
                    print(f"Plugin {original} not found, cannot rename.")
                    continue

                new_tree.move(entry, new)

            # move not selected plugins to optional
            for plugin, enabled in dialog.plugins().items():
                if not enabled:
                    entry = new_tree.find(plugin)
                    if not entry:
                        continue  # silently fail since the plugin should be disabled
                    new_tree.addDirectory("optional").insert(entry)

            # TODO: INI Tweaks:
            alltweaks = dialog.tweaks()

            for filename, tweaks in alltweaks.items():
                # find the original file (if any)
                o_entry = new_tree.find(filename)
                o_filename: Optional[str] = None
                if o_entry:
                    # find the filepath from the list of extracted files
                    index = to_extract.index(o_entry)

                    # +1 because the first one is the script
                    o_filename = paths[index + 1]

                # if the file existed before, we keep the new one at the same place
                if o_entry or Path(filename).parts[0].lower() == "ini tweaks":
                    entry = new_tree.addFile(filename, replace_if_exists=True)

                # otherwise we create it in INI Tweaks
                else:
                    entry = new_tree.addFile(
                        os.path.join("INI Tweaks", filename), replace_if_exists=True
                    )

                filepath = self._manager().createFile(entry)

                if not o_filename:
                    data = make_ini_tweaks(tweaks)
                else:
                    data = merge_ini_tweaks(tweaks, Path(o_filename))

                with open(filepath, "w") as fp:
                    fp.write(data)

            # mark stuff for saving
            self._installerUsed = True
            self._installerOptions = dict(dialog.selectedOptions())

            return new_tree

        # if user requested a manual installation, we update the name (to keep it
        # in the manual installation dialog) and just notify the installation manager
        elif dialog.isManualRequested():
            name.update(dialog.name(), mobase.GuessQuality.USER)
            return mobase.InstallResult.MANUAL_REQUESTED

        # if user canceled, we simply notify the installation manager
        else:
            return mobase.InstallResult.CANCELED

    def tr(self, value: str) -> str:
        # we need this to translate string in Python. Check the common documentation
        # for more details
        return QApplication.translate("WizardInstaller", value)
