import json
import os
from pathlib import Path

from PyQt6.QtCore import qInfo, qWarning
from PyQt6.QtWidgets import QApplication

from .bg3_tool_plugin import BG3ToolPlugin
from .icons import exchange


class BG3ToolConvertJsonsToYaml(BG3ToolPlugin):
    icon_bytes = exchange
    sub_name = "Convert JSONS to YAML"
    desc = "Convert all jsons in active mods to yaml immediately."

    def display(self):
        from ...game_baldursgate3 import BG3Game

        game_plugin = self._organizer.managedGame()
        if not isinstance(game_plugin, BG3Game):
            return
        utils = game_plugin.utils
        qInfo("converting all json files to yaml")
        active_mods = utils.active_mods()
        progress = utils.create_progress_window(
            "Converting all json files to yaml", len(active_mods) + 1
        )
        for mod in active_mods:
            _convert_jsons_in_dir_to_yaml(Path(mod.absolutePath()))
            progress.setValue(progress.value() + 1)
            QApplication.processEvents()
            if progress.wasCanceled():
                qWarning("conversion canceled by user")
                return
        _convert_jsons_in_dir_to_yaml(utils.overwrite_path)
        progress.setValue(len(active_mods) + 1)
        QApplication.processEvents()
        progress.close()


def _convert_jsons_in_dir_to_yaml(path: Path):
    for file in list(path.rglob("*.json")):
        converted_path = file.parent / file.name.replace(".json", ".yaml")
        try:
            if not converted_path.exists() or os.path.getmtime(file) > os.path.getmtime(
                converted_path
            ):
                import yaml

                with open(file, "r") as json_file:
                    with open(converted_path, "w") as yaml_file:
                        yaml.dump(
                            json.load(json_file), yaml_file, indent=2, sort_keys=False
                        )
                qInfo(f"Converted {file} to YAML")
        except OSError as e:
            qWarning(f"Error accessing file {converted_path}: {e}")
