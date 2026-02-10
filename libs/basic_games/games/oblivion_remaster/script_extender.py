from pathlib import Path

import mobase


class OblivionRemasteredScriptExtender(mobase.ScriptExtender):
    def __init__(self, game: mobase.IPluginGame):
        super().__init__()
        self._game = game

    def binaryName(self):
        return "obse64_loader.exe"

    def loaderName(self) -> str:
        return self.binaryName()

    def loaderPath(self) -> str:
        return (
            self._game.gameDirectory().absolutePath()
            + "\\OblivionRemastered\\Binaries\\Win64\\"
            + self.loaderName()
        )

    def pluginPath(self) -> str:
        return "OBSE/Plugins"

    def savegameExtension(self) -> str:
        return ""

    def isInstalled(self) -> bool:
        return Path(self.loaderPath()).exists()

    def getExtenderVersion(self) -> str:
        return mobase.getFileVersion(self.loaderPath())

    def getArch(self) -> int:
        return 0x8664 if self.isInstalled() else 0x0
