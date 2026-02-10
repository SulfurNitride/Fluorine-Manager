from collections.abc import Sequence

from PyQt6.QtWidgets import QWidget

import mobase


class DummyModDataChecker(mobase.ModDataChecker):
    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        return mobase.ModDataChecker.VALID

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        return filetree


class DummySaveGameInfo(mobase.SaveGameInfo):
    def getMissingAssets(self, save: mobase.ISaveGame) -> dict[str, Sequence[str]]:
        return {}

    def getSaveGameWidget(self, parent: QWidget) -> mobase.ISaveGameInfoWidget | None:
        return None


# we do not implement everything since this will do fine if we do not call anything
# from the C++ side
#
class DummyGame(mobase.IPluginGame):
    _features: dict[type, object]

    def __init__(self):
        super().__init__()

        self._features = {
            mobase.ModDataChecker: DummyModDataChecker(),
            mobase.SaveGameInfo: DummySaveGameInfo(),
        }

    def _featureList(self) -> dict[type, object]:
        return self._features


def createPlugin() -> mobase.IPlugin:
    return DummyGame()  # type: ignore
