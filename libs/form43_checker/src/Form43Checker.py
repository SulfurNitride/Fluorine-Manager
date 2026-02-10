from pathlib import Path

import mobase
from PyQt6.QtCore import QCoreApplication


class Form43Checker(mobase.IPluginDiagnose):
    __organizer: mobase.IOrganizer
    __invalidPlugins: list[str] = []

    def __init__(self):
        super().__init__()

    def init(self, organizer: mobase.IOrganizer):
        self.__organizer = organizer
        return True

    def name(self):
        return "Form 43 Plugin Checker"

    def localizedName(self):
        return "Form 43 Plugin Checker"

    def author(self):
        return "AnyOldName3"

    def description(self):
        return self.tr(
            "Checks plugins (.ESM/.ESP files) to see if any are lower than Form 44 (Skyrim SE)."
        )

    def version(self):
        return mobase.VersionInfo(1, 2, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self):
        return [
            mobase.PluginRequirementFactory.gameDependency("Skyrim Special Edition")
        ]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def activeProblems(self) -> list[int]:
        self.__updateInvalidPlugins()
        if self.__invalidPlugins:
            return [0]
        else:
            return []

    def shortDescription(self, key: int) -> str:
        return self.tr("Form 43 (or lower) plugin detected")

    def fullDescription(self, key: int) -> str:
        pluginList = self.__listPlugins()
        pluginListString = "<br><br>•  " + ("<br>•  ".join(pluginList))
        outputString = self.tr(
            "You have one or more plugins that are not form 44. They are:{0}"
        ).format(pluginListString)
        outputString += "<br><br>"
        outputString += self.tr(
            "Form 43 (or lower) plugins are modules that were made for Skyrim LE (Oldrim) and have not been "
            "properly ported to Skyrim Special Edition, which uses form 44 plugins. This usually results in "
            "parts of the mod not working correctly."
            "<br><br>"
            "To be converted, these plugins simply need to be opened and saved with the SSE Creation Kit "
            "but their presence can be an indication that a mod was not properly ported to SSE and so "
            "can potentially have additional issues."
            "<br><br>"
            "Online guides can have more information on how to correctly convert mods for Skyrim SE.<br>"
        )
        return outputString

    def hasGuidedFix(self, key: int) -> bool:
        return False

    def startGuidedFix(self, key: int) -> None:
        # Maybe we could use xEdit or something to resave the file?
        pass

    def tr(self, value: str):
        return QCoreApplication.translate("Form43Checker", value)

    def __testFile(self, path: str) -> bool:
        version = self.__getForm(path)
        return version != -1 and version < 44

    def __getForm(self, file: str) -> int:
        pluginName = Path(file).name
        return self.__organizer.pluginList().formVersion(pluginName)

    def __updateInvalidPlugins(self) -> None:
        self.__invalidPlugins.clear()
        for file in self.__organizer.findFiles("", "*.es[pm]"):
            if self.__testFile(file):
                self.__invalidPlugins.append(file)

    def __listPlugins(self) -> list[str]:
        return [
            f"{Path(file).name} (form {self.__getForm(file)})"
            for file in self.__invalidPlugins
        ]


def createPlugin():
    return Form43Checker()
