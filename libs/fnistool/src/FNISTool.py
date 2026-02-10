# This Mod Organizer plugin is released to the pubic under the terms of the GNU GPL version 3, which is accessible from the Free Software Foundation here: https://www.gnu.org/licenses/gpl-3.0-standalone.html

# To use this plugin, place it in the plugins directory of your Mod Organizer install. You will then find a 'Run FNIS' option under the tools menu.

# Intended behaviour:
# * Adds button to tools menu.
# * If FNIS' location isn't known (or isn't valid, e.g. FNIS isn't actually there) when the button is pressed, a file chooser is displayed to find FNIS.
# * `GenerateFNISforUsers.exe RedirectFiles="<some mod path>" InstantExecute=1` is then run within the VFS, with the RedirectFiles option being controlled by other settings (which the user is prompted to fill in if they have not yet been specified).
# * When it exits, if necessary, its return code is used to generate a helpful popup saying whether or not it worked.

# Future behaviour:
# * As in Vortex's FNIS integration, keeps track of mod files which affect FNIS.
# * If they don't match what was there when the last FNIS output was created, uses IPluginDiagnose interface to display a warning
# * This may already be handled by MO's built-in (but disabled) FNIS checker plugin
import os
import pathlib
import sys

from PyQt6.QtCore import QCoreApplication, qCritical, QFileInfo
from PyQt6.QtGui import QIcon, QFileSystemModel
from PyQt6.QtWidgets import QFileDialog, QMessageBox

import mobase

class FNISMissingException(Exception):
    """Thrown if GenerateFNISforUsers.exe path can't be found"""
    pass

class FNISInactiveException(Exception):
    """Thrown if GenerateFNISforUsers.exe is installed to an inactive mod"""
    pass

class UnknownOutputPreferenceException(Exception):
    """Thrown if the user hasn't specified whether to output to a separate mod"""
    pass

class FNISTool(mobase.IPluginTool):

    def __init__(self):
        super(FNISTool, self).__init__()
        self.__organizer = None
        self.__parentWidget = None

    def init(self, organizer):
        self.__organizer = organizer
        if sys.version_info < (3, 0):
            qCritical(self.tr("FNISTool plugin requires a Python 3 interpreter, but is running on a Python 2 interpreter."))
            QMessageBox.critical(self.__parentWidget, self.tr("Incompatible Python version."), self.tr("This version of the FNIS Integration plugin requires a Python 3 interpreter, but Mod Organizer has provided a Python 2 interpreter. You should check for an updated version, including in the Mod Organizer 2 Development Discord Server."))
            return False
        return True

    def name(self):
        return "FNIS Integration Tool"

    def localizedName(self):
        return self.tr("FNIS Integration Tool")

    def author(self):
        return "AnyOldName3"

    def description(self):
        return self.tr("Runs GenerateFNISforUsers.exe so the game can load custom animations.")

    def version(self):
        return mobase.VersionInfo(1, 2, 0, 0)

    def requirements(self):
        return [
            mobase.PluginRequirementFactory.gameDependency({
                "Skyrim",
                "Skyrim Special Edition",
                "Skyrim VR"
            })
        ]

    def settings(self):
        return [
            mobase.PluginSetting("fnis-path", self.tr("Path to GenerateFNISforUsers.exe"), ""),
            mobase.PluginSetting("output-to-mod", self.tr("Whether or not to direct the FNIS output to a mod folder."), True),
            mobase.PluginSetting("output-path", self.tr("When output-to-mod is enabled, the path to the mod to use."), ""),
            mobase.PluginSetting("initialised", self.tr("Settings have been initialised.  Set to False to reinitialise them."), False),
            mobase.PluginSetting("output-logs-to-mod", self.tr("Whether or not to direct any new FNIS logs to a mod folder."), True),
            mobase.PluginSetting("output-logs-path", self.tr("When output-logs-to-mod is enabled, the path to the mod to use."), ""),
            ]

    def displayName(self):
        return self.tr("FNIS/Run FNIS")

    def tooltip(self):
        return self.tr("Runs GenerateFNISforUsers.exe so the game can load custom animations.")

    def icon(self):
        fnisPath = self.__organizer.pluginSetting(self.name(), "fnis-path")
        if os.path.exists(fnisPath):
            # We can't directly grab the icon from an executable, but this seems like the simplest alternative.
            fin = QFileInfo(fnisPath)
            model = QFileSystemModel()
            model.setRootPath(fin.path())
            return model.fileIcon(model.index(fin.filePath()))
        else:
            # Fall back to where the user might have put an icon manually.
            return QIcon("plugins/FNIS.ico")

    def setParentWidget(self, widget):
        self.__parentWidget = widget

    def display(self):
        args = []
        redirectOutput = True
        outputModName = None
        logOutputModName = ""

        if not bool(self.__organizer.pluginSetting(self.name(), "initialised")):
            self.__organizer.setPluginSetting(self.name(), "fnis-path", "")
            self.__organizer.setPluginSetting(self.name(), "output-path", "")
            self.__organizer.setPluginSetting(self.name(), "output-to-mod", True)
            self.__organizer.setPluginSetting(self.name(), "output-logs-path", "")
            self.__organizer.setPluginSetting(self.name(), "output-logs-to-mod", True)

        try:
            redirectOutput = self.__getRedirectOutput()
        except UnknownOutputPreferenceException:
            QMessageBox.critical(self.__parentWidget, self.tr("Output preference not set"), self.tr("Whether or not to output to a mod was not specified. The tool will now exit."))
            return
        if redirectOutput:
            try:
                outputPath = self.__getOutputPath()
                args.append('RedirectFiles="' + outputPath + '"')
                outputModName = pathlib.Path(outputPath).name
            except UnknownOutputPreferenceException:
                QMessageBox.critical(self.__parentWidget, self.tr("Output mod not set"), self.tr("The mod to output to was not specified. The tool will now exit."))
                return
        args.append('InstantExecute=1')

        if redirectOutput:
            try:
                redirectLogs = self.__getRedirectLogs()
            except UnknownOutputPreferenceException:
                QMessageBox.critical(self.__parentWidget, self.tr("Output preference not set"), self.tr("Whether or not to output to a mod was not specified. The tool will now exit."))
                return
            if redirectLogs:
                try:
                    outputPath = self.__getLogOutputPath()
                    logOutputModName = pathlib.Path(outputPath).name
                except UnknownOutputPreferenceException:
                    QMessageBox.critical(self.__parentWidget, self.tr("Output mod not set"), self.tr("The mod to output to was not specified. The tool will now exit."))
                    return

        try:
            executable = self.__getFNISPath()
        except FNISMissingException:
            QMessageBox.critical(self.__parentWidget, self.tr("FNIS path not specified"), self.tr("The path to GenerateFNISforUsers.exe wasn't specified. The tool will now exit."))
            return
        except FNISInactiveException:
            # Error has already been displayed, just quit
            return

        self.__organizer.setPluginSetting(self.name(), "initialised", True)

        if redirectOutput:
            # Disable the output mod as USVFS isn't designed to cope with its input directories being modified
            self.__organizer.modList().setActive(outputModName, False)

            if redirectLogs:
                # Enable the log output mod
                self.__organizer.modList().setActive(logOutputModName, True)


        handle = self.__organizer.startApplication(executable, args, forcedCustomOverwrite=logOutputModName, ignoreCustomOverwrite=not bool(logOutputModName))
        result, exitCode = self.__organizer.waitForApplication(handle)

        if redirectOutput:
            # Enable the output mod
            self.__organizer.modList().setActive(outputModName, True)
            # Ensure the 'No valid game data' message goes away
            self.__organizer.modDataChanged(self.__organizer.getMod(outputModName))

            if redirectLogs:
                self.__organizer.modDataChanged(self.__organizer.getMod(logOutputModName))

    def tr(self, str):
        return QCoreApplication.translate("FNISTool", str)

    def __getRedirectOutput(self):
        redirectOutput = bool(self.__organizer.pluginSetting(self.name(), "output-to-mod"))
        initialised = bool(self.__organizer.pluginSetting(self.name(), "initialised"))
        if not initialised:
            result = QMessageBox.question(self.__parentWidget, self.tr("Output to a mod?"), self.tr("Fore's New Idles in Skyrim can output either to Mod Organizer's VFS (potentially overwriting files from multiple mods) or to a separate mod. Would you like FNIS to output to a separate mod? This setting can be updated in the Plugins tab of the Mod Organizer Settings menu."), QMessageBox.StandardButton(QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No | QMessageBox.StandardButton.Cancel))
            if result == QMessageBox.StandardButton.Yes:
                redirectOutput = True
            elif result == QMessageBox.StandardButton.No:
                redirectOutput = False
            else:
                # the user pressed cancel
                raise UnknownOutputPreferenceException

            self.__organizer.setPluginSetting(self.name(), "output-to-mod", redirectOutput)
        return redirectOutput

    def __getRedirectLogs(self):
        redirectLogs = bool(self.__organizer.pluginSetting(self.name(), "output-logs-to-mod"))
        initialised = bool(self.__organizer.pluginSetting(self.name(), "initialised"))
        if not initialised:
            result = QMessageBox.question(self.__parentWidget, self.tr("Output logs to a mod?"), self.tr("Any new logs generated when running FNIS will end up in Mod Organizer's overwrite folder. Would you like these logs to be output to a separate mod? This setting can be updated in the Plugins tab of the Mod Organizer Settings menu."), QMessageBox.StandardButton(QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No | QMessageBox.StandardButton.Cancel))
            if result == QMessageBox.StandardButton.Yes:
                redirectLogs = True
            elif result == QMessageBox.StandardButton.No:
                redirectLogs = False
            else:
                # the user pressed cancel
                raise UnknownOutputPreferenceException

            self.__organizer.setPluginSetting(self.name(), "output-logs-to-mod", redirectLogs)
        return redirectLogs

    def __getOutputPath(self):
        path = self.__organizer.pluginSetting(self.name(), "output-path")
        pathlibPath = pathlib.Path(path)
        modDirectory = self.__getModDirectory()
        isAMod = pathlibPath.parent.samefile(modDirectory)
        if not pathlibPath.is_dir() or not isAMod:
            QMessageBox.information(self.__parentWidget, self.tr("Choose an output mod"), self.tr("Please choose an output mod for Fore's New Idles in Skyrim. This must be a directory in Mod Organizer's mods directory, and you can create one if you do not have one already. This mod will not be available to the VFS when FNIS is run, so do not choose a mod you use for anything else. This setting can be updated in the Plugins tab of the Mod Organizer Settings menu."))
            while not pathlibPath.is_dir() or not isAMod:
                path = QFileDialog.getExistingDirectory(self.__parentWidget, self.tr("Choose an output mod"), str(modDirectory), QFileDialog.Option.ShowDirsOnly)
                if not path:
                    # cancel was pressed
                    raise UnknownOutputPreferenceException
                pathlibPath = pathlib.Path(path)
                isAMod = pathlibPath.parent.samefile(modDirectory)
                if not isAMod:
                    QMessageBox.information(self.__parentWidget, self.tr("Not a mod..."), self.tr("The selected directory is not a Mod Organizer managed mod. Please choose a directory within the mods directory."))
                    continue
                empty = True
                for item in pathlibPath.iterdir():
                    if item.name != "meta.ini":
                        empty = False
                        break
                if not empty:
                    if QMessageBox.question(self.__parentWidget, self.tr("Mod not empty"), self.tr("The selected mod already contains files. Are you sure want to use it as the output mod?"), QMessageBox.StandardButton(QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)) == QMessageBox.StandardButton.Yes:
                        # Proceed normally - the user is happy
                        pass
                    else:
                        # Restart outer loop - the user wants to pick again
                        isAMod = False
            # The user may have created a new mod in the MO mods directory, so we must trigger a refresh
            self.__organizer.refreshModList()
            self.__organizer.setPluginSetting(self.name(), "output-path", path)
        return path

    def __getLogOutputPath(self):
        path = self.__organizer.pluginSetting(self.name(), "output-logs-path")
        pathlibPath = pathlib.Path(path)
        modDirectory = self.__getModDirectory()
        fnisOutputPath = pathlib.Path(self.__getOutputPath())
        isAMod = pathlibPath.parent.samefile(modDirectory)
        isSameAsFnisOutput = pathlibPath.samefile(fnisOutputPath)
        if not pathlibPath.is_dir() or not isAMod or isSameAsFnisOutput:
            QMessageBox.information(self.__parentWidget, self.tr("Choose an output mod"), self.tr("Please choose an output mod for logs for Fore's New Idles in Skyrim. This must be a directory in Mod Organizer's mods directory, must not be the same as the FNIS output mod, and you can create one if you do not have one already. This setting can be updated in the Plugins tab of the Mod Organizer Settings menu."))
            while not pathlibPath.is_dir() or not isAMod or isSameAsFnisOutput:
                path = QFileDialog.getExistingDirectory(self.__parentWidget, self.tr("Choose a log output mod"), str(modDirectory), QFileDialog.Option.ShowDirsOnly)
                if not path:
                    # cancel was pressed
                    raise UnknownOutputPreferenceException
                pathlibPath = pathlib.Path(path)
                isAMod = pathlibPath.parent.samefile(modDirectory)
                if not isAMod:
                    QMessageBox.information(self.__parentWidget, self.tr("Not a mod..."), self.tr("The selected directory is not a Mod Organizer managed mod. Please choose a directory within the mods directory."))
                    continue
                isSameAsFnisOutput = pathlibPath.samefile(fnisOutputPath)
                if isSameAsFnisOutput:
                    QMessageBox.information(self.__parentWidget, self.tr("Same as FNIS output"), self.tr("The selected mod is the same as the FNIS output mod.  Please choose a different mod."))
                    continue
                empty = True
                for item in pathlibPath.iterdir():
                    if item.name != "meta.ini":
                        empty = False
                        break
                if not empty:
                    if QMessageBox.question(self.__parentWidget, self.tr("Mod not empty"), self.tr("The selected mod already contains files. Are you sure want to use it as the output mod?"), QMessageBox.StandardButton(QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)) == QMessageBox.StandardButton.Yes:
                        # Proceed normally - the user is happy
                        pass
                    else:
                        # Restart outer loop - the user wants to pick again
                        isAMod = False

            # The user may have created a new mod in the MO mods directory, so we must trigger a refresh
            self.__organizer.refreshModList()
            self.__organizer.setPluginSetting(self.name(), "output-logs-path", path)
        return path

    def __getFNISPath(self):
        savedPath = self.__organizer.pluginSetting(self.name(), "fnis-path")
        # FNIS must be installed within the game's data directory, so needs to either be within that or a mod folder
        modDirectory = self.__getModDirectory()
        gameDataDirectory = pathlib.Path(self.__organizer.managedGame().dataDirectory().absolutePath())
        pathlibPath = pathlib.Path(savedPath)
        inGoodLocation = self.__withinDirectory(pathlibPath, modDirectory)
        inGoodLocation |= self.__withinDirectory(pathlibPath, gameDataDirectory)
        if not pathlibPath.is_file() or not inGoodLocation:
            QMessageBox.information(self.__parentWidget, self.tr("Find FNIS"), self.tr("Fore's New Idles in Skyrim can't be found using the location saved in Mod Organizer's settings. Please find GenerateFNISforUsers.exe in the file picker. FNIS must be visible within the VFS, so choose an installation either within the game's data directory or within a mod folder. This setting can be updated in the Plugins tab of the Mod Organizer Settings menu."))
            while True:
                path = QFileDialog.getOpenFileName(self.__parentWidget, self.tr("Locate GenerateFNISforUsers.exe"), str(modDirectory), "FNIS (GenerateFNISforUsers.exe)")[0]
                if path == "":
                    # Cancel was pressed
                    raise FNISMissingException
                pathlibPath = pathlib.Path(path)
                inGoodLocation = self.__withinDirectory(pathlibPath, modDirectory)
                inGoodLocation |= self.__withinDirectory(pathlibPath, gameDataDirectory)
                if pathlibPath.is_file() and inGoodLocation:
                    self.__organizer.setPluginSetting(self.name(), "fnis-path", path)
                    savedPath = path
                    break
                else:
                    QMessageBox.information(self.__parentWidget, self.tr("Not a compatible location..."), self.tr("Fore's New Idles in Skyrim only works when within the VFS, so must be installed to the game's data directory or within a mod folder. Please select a different FNIS installation."))
        # Check the mod is actually enabled
        if self.__withinDirectory(pathlibPath, modDirectory):
            fnisModName = None
            for path in pathlibPath.parents:
                if path.parent.samefile(modDirectory):
                    fnisModName = path.name
                    break
            if (self.__organizer.modList().state(fnisModName) & mobase.ModState.active) == 0:
                # FNIS is installed to an inactive mod
                result = QMessageBox.question(self.__parentWidget, self.tr("FNIS mod deactivated"), self.tr("Fore's New Idles in Skyrim is installed to an inactive mod. Press OK to activate it or Cancel to quit the tool"), QMessageBox.StandardButton(QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel))
                if result == QMessageBox.StandardButton.Ok:
                    self.__organizer.modList().setActive(fnisModName, True)
                else:
                    raise FNISInactiveException
        return savedPath

    def __getModDirectory(self):
        return self.__organizer.modsPath()

    @staticmethod
    def __withinDirectory(innerPath, outerDir):
        for path in innerPath.parents:
            if path.samefile(outerDir):
                return True
        return False

def createPlugin():
    return FNISTool()
