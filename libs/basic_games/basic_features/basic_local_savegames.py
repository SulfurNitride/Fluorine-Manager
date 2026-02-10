from PyQt6.QtCore import QDir

import mobase


class BasicLocalSavegames(mobase.LocalSavegames):
    _game: mobase.IPluginGame

    def __init__(self, game: mobase.IPluginGame):
        super().__init__()
        self._game = game

    def game_save_dir(self) -> str:
        return self._game.savesDirectory().absolutePath()

    def mappings(self, profile_save_dir: QDir):
        return [
            mobase.Mapping(
                source=profile_save_dir.absolutePath(),
                destination=self.game_save_dir(),
                is_directory=True,
                create_target=True,
            )
        ]

    def prepareProfile(self, profile: mobase.IProfile) -> bool:
        return profile.localSavesEnabled()
