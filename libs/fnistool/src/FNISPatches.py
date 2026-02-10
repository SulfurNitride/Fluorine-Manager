# This Mod Organizer plugin is released to the pubic under the terms of the GNU GPL version 3, which is accessible from the Free Software Foundation here: https://www.gnu.org/licenses/gpl-3.0-standalone.html

# To use this plugin, place it in the plugins directory of your Mod Organizer install. You will then find a 'ENTER BUTTON NAME HERE BEFORE RELEASE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!' option under the tools menu.

# Intended behaviour:
# * Adds button to tools menu.
# * If FNIS' location isn't known (or isn't valid, e.g. FNIS isn't actually there) when the button is pressed, nag the user to run the main FNIS Tool at least once, and exit.
# * Reads FNIS' patches file for the current game and the MyPatches.txt file to determine which are active.
# * Displays a popup where the user can enable or disable patches, and saves the results to MyPatches.txt when Save is pressed.

# Future behaviour:
# * Maybe allow the user to show hidden patches
# * Maybe move the saved list to something stored per-profile

import os
import pathlib
import sys

from FNISTool import FNISTool
from PyQt6.QtCore import QCoreApplication, qCritical, QFileInfo, Qt
from PyQt6.QtGui import QIcon, QFileSystemModel
from PyQt6.QtWidgets import QDialogButtonBox, QLabel, QListWidget, QListWidgetItem, QMessageBox, QVBoxLayout, QDialog

import mobase

patchListNames = {
    "Skyrim": "PatchList.txt",
    "Skyrim Special Edition": "PatchListSE.txt",
    "Skyrim VR": "PatchListVR.txt"
}

class Patch:

    def __init__(self, fullString):
        fields = fullString.split('#')
        # field names taken from FNIS PatchListSE.txt
        self.patchid = fields[0]
        self.hidden = fields[1] == "1"
        self.num_bones = fields[2]
        self.required_behaviors_pattern = fields[3]
        self.text_for_patch = fields[4]
        if len(fields) > 5:
            self.optional_file_path_for_mod_install_check = fields[5]
        else:
            self.optional_file_path_for_mod_install_check = None

    def asQListWidgetItem(self):
        listItem = QListWidgetItem(self.text_for_patch, None, 0)
        listItem.setFlags(listItem.flags() | Qt.ItemFlag.ItemIsUserCheckable)
        # We can't set the hidden status until the item is actually in a list
        listItem.setData(Qt.ItemDataRole.UserRole, self.patchid)

        return listItem

class ExpandingQListWidget(QListWidget):

    def __init__(self, parent=None):
        super().__init__(parent)

    def sizeHint(self):
        return self.childrenRect().size()

class FNISPatches(mobase.IPluginTool):

    def __init__(self):
        super(FNISPatches, self).__init__()
        self.__organizer = None
        self.__parentWidget = None

    def init(self, organizer):
        self.__organizer = organizer
        if sys.version_info < (3, 0):
            qCritical(self.tr("FNISPatches plugin requires a Python 3 interpreter, but is running on a Python 2 interpreter."))
            QMessageBox.critical(self.__parentWidget, self.tr("Incompatible Python version."), self.tr("This version of the FNIS Patches plugin requires a Python 3 interpreter, but Mod Organizer has provided a Python 2 interpreter. You should check for an updated version, including in the Mod Organizer 2 Development Discord Server."))
            return False
        return True

    def name(self):
        return "FNIS Patches Tool"

    def localizedName(self):
        return self.tr("FNIS Patches Tool")

    def author(self):
        return "AnyOldName3"

    def description(self):
        return self.tr("Configures the patches which FNIS applies to the game.")

    def version(self):
        return mobase.VersionInfo(1, 0, 1, mobase.ReleaseType.final)

    def master(self):
        return "FNIS Integration Tool"

    def settings(self):
        return []

    def displayName(self):
        return self.tr("FNIS/Configure FNIS Patches")

    def tooltip(self):
        return self.tr("Configures the patches which FNIS applies to the game.")

    def icon(self):
        fnisPath = self.__organizer.pluginSetting(self.__mainToolName(), "fnis-path")
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
        fnisPath = self.__getFNISPath()
        if fnisPath == None:
            return

        enabledPatches = self.__loadEnabledPatches()

        availablePatches = self.__loadAvailablePatches()

        dialog = QDialog(self.__parentWidget)
        dialog.setWindowTitle(self.tr("Select Patches"))

        label = QLabel(self.tr("Note: Some patches may be automatically enabled or disabled by Fore's New Idles in Skyrim, so don't be surprised if its list differs from this one."))
        label.setWordWrap(True)

        listWidget = ExpandingQListWidget()
        for patch in availablePatches.values():
            listItem = patch.asQListWidgetItem()
            if patch.patchid in enabledPatches:
                listItem.setCheckState(Qt.CheckState.Checked)
            else:
                listItem.setCheckState(Qt.CheckState.Unchecked)
            listWidget.addItem(listItem)
            listItem.setHidden(patch.hidden)

        buttonBox = QDialogButtonBox(QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        buttonBox.accepted.connect(dialog.accept)
        buttonBox.rejected.connect(dialog.reject)

        layout = QVBoxLayout()
        layout.addWidget(label)
        layout.addWidget(listWidget)
        layout.addWidget(buttonBox)
        dialog.setLayout(layout)

        result = dialog.exec()
        if result == QDialog.DialogCode.Rejected:
            # Cancel was pressed
            return

        enabledPatches = set()
        for i in range(0, listWidget.count()):
            listItem = listWidget.item(i)
            if listItem.checkState() == Qt.CheckState.Checked:
                enabledPatches.add(listItem.data(Qt.ItemDataRole.UserRole))
        self.__saveEnabledPatches(enabledPatches)

    def tr(self, str):
        return QCoreApplication.translate("FNISPatches", str)

    @staticmethod
    def __mainToolName():
        return FNISTool().name()

    def __getFNISPath(self):
        savedPath = self.__organizer.pluginSetting(self.__mainToolName(), "fnis-path")
        # FNIS must be installed within the game's data directory, so needs to either be within that or a mod folder
        modDirectory = self.__getModDirectory()
        gameDataDirectory = pathlib.Path(self.__organizer.managedGame().dataDirectory().absolutePath())
        pathlibPath = pathlib.Path(savedPath)
        inGoodLocation = self.__withinDirectory(pathlibPath, modDirectory)
        inGoodLocation |= self.__withinDirectory(pathlibPath, gameDataDirectory)
        if not pathlibPath.is_file() or not inGoodLocation:
            QMessageBox.information(self.__parentWidget, self.tr("Unable to find FNIS"), self.tr("Fore's New Idles in Skyrim can't be found using the location saved in Mod Organizer's settings. Please run the main FNIS integration tool before using this one."))
            return None
        return savedPath

    def __getModDirectory(self):
        modDirectory = None
        modList = self.__organizer.modList().allModsByProfilePriority()
        # Get the first managed mod so we can access the mods directory.
        for mod in modList:
            if (self.__organizer.modList().state(mod) & 0x2) != 0:
                modDirectory = pathlib.Path(self.__organizer.modList().getMod(mod).absolutePath()).parent
                break
        return modDirectory

    @staticmethod
    def __withinDirectory(innerPath, outerDir):
        for path in innerPath.parents:
            if path.samefile(outerDir):
                return True
        return False

    def __loadEnabledPatches(self, ):
        path = pathlib.Path(self.__getFNISPath()).parent
        path /= "MyPatches.txt"
        if not path.is_file():
            return set()

        enabledPatches = set()
        with path.open() as f:
            for line in f:
                if line != "":
                    enabledPatches.add(line.strip())
        return enabledPatches

    def __saveEnabledPatches(self, enabledPatches):
        path = pathlib.Path(self.__getFNISPath()).parent
        path /= "MyPatches.txt"
        with path.open('w') as f:
            for patchName in enabledPatches:
                f.write(patchName)
                f.write("\n")

    def __loadAvailablePatches(self):
        patchListName = patchListNames[self.__organizer.managedGame().gameName()]
        path = pathlib.Path(self.__getFNISPath()).parent
        path /= patchListName
        availablePatches = {}
        # FNIS uses UTF-8 with a BOM, Python assumes everything's using the platform's default encoding
        with path.open(encoding="utf-8-sig") as f:
            # The first line is a header thing
            firstLine = True
            for line in f:
                if firstLine:
                    firstLine = False
                    continue
                if line[0] == "'":
                    continue
                patch = Patch(line.strip())
                availablePatches[patch.patchid] = patch
        return availablePatches

def createPlugin():
    return FNISPatches()
