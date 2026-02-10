from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Iterable, List, Optional

from wizard.interpreter import WizardInterpreter
from wizard.manager import ManagerModInterface
from wizard.severity import SeverityContext
from wizard.utils import make_runner_context_factory
from wizard.value import SubPackage, SubPackages

import mobase


class MO2SubPackage(SubPackage):
    _tree: mobase.IFileTree
    _files: List[str]

    def __init__(self, tree: mobase.IFileTree):
        super().__init__(tree.name())
        self._tree = tree

        # we cannot perform lazy iteration on the tree in a Python way so we
        # have to list the files
        self._files = []

        def fn(folder: str, entry: mobase.FileTreeEntry) -> mobase.IFileTree.WalkReturn:
            self._files.append(entry.path())
            return mobase.IFileTree.CONTINUE

        self._tree.walk(fn)

    @property
    def files(self) -> Iterable[str]:
        return self._files


class MO2SeverityContext(SeverityContext):
    _organizer: mobase.IOrganizer

    def __init__(self, organizer: mobase.IOrganizer):
        super().__init__()
        self._organizer = organizer

    def warning(self, text: str):
        print(text, file=sys.stderr)


class MO2ManagerModInterface(ManagerModInterface):
    _organizer: mobase.IOrganizer
    _game: mobase.IPluginGame
    _subpackages: SubPackages

    def __init__(self, tree: mobase.IFileTree, organizer: mobase.IOrganizer):
        self._organizer = organizer
        self._game = organizer.managedGame()

        checker = self._organizer.gameFeatures().gameFeature(mobase.ModDataChecker)

        # read the sub-packages
        self._subpackages = SubPackages()
        for entry in tree:
            if isinstance(entry, mobase.IFileTree):
                if checker:
                    if checker.dataLooksValid(entry) == mobase.ModDataChecker.VALID:
                        self._subpackages.append(MO2SubPackage(entry))
                        continue

                # add entry with INI tweaks
                if entry.exists("INI Tweaks") or entry.exists("INI"):
                    self._subpackages.append(MO2SubPackage(entry))
                    continue

                # we add folder with format "XXX Docs" where "XXX" is a number
                parts = entry.name().split()
                if (
                    len(parts) >= 2
                    and parts[0].isdigit()
                    and parts[1].lower().startswith("doc")
                ):
                    self._subpackages.append(MO2SubPackage(entry))

    @property
    def subpackages(self) -> SubPackages:
        return self._subpackages

    def compareGameVersion(self, version: str) -> int:
        v1 = mobase.VersionInfo(version)
        v2 = mobase.VersionInfo(self._game.gameVersion())
        if v1 < v2:
            return 1
        elif v1 > v2:
            return -1
        else:
            return 0

    def compareSEVersion(self, version: str) -> int:
        se = self._organizer.gameFeatures().gameFeature(mobase.ScriptExtender)
        if not se:
            return 1
        v1 = mobase.VersionInfo(version)
        v2 = mobase.VersionInfo(se.getExtenderVersion())
        if v1 < v2:
            return 1
        elif v1 > v2:
            return -1
        else:
            return 0

    def compareGEVersion(self, version: str) -> int:
        # cannot do th is in MO2
        return 1

    def compareWBVersion(self, version: str) -> int:
        # cannot do this in MO2
        return 1

    def _resolve(self, filepath: str) -> Optional[Path]:
        """
        Resolve the given filepath.

        Args:
            filepath: The path to resolve.

        Returns:
            The path to the given file on the disk, or one of the file mapping
            to it in the VFS, or None if the file does not exists.
        """
        # TODO: This does not handle weird path that go back (..) and then in data
        # again, e.g. ../data/xxx.esp.
        path: Optional[Path]
        if filepath.startswith(".."):
            path = Path(self._game.dataDirectory().absoluteFilePath(filepath))
            if not path.exists():
                path = None
        else:
            path = Path(filepath)
            parent = path.parent.as_posix()
            if parent == ".":
                parent = ""

            files = self._organizer.findFiles(parent, "*" + path.name)
            if files:
                path = Path(files[0])
            else:
                path = None

        return path

    def dataFileExists(self, *filepaths: str) -> bool:
        return all(self._resolve(path) for path in filepaths)

    def getPluginLoadOrder(self, filename: str, fallback: int = -1) -> int:
        return self._organizer.pluginList().loadOrder(filename)

    def getPluginStatus(self, filename: str) -> int:
        state = self._organizer.pluginList().state(filename)

        if state == mobase.PluginState.ACTIVE:
            return 2
        if state == mobase.PluginState.INACTIVE:
            return 0  # Or 1?
        return -1

    def getFilename(self, path: str) -> str:
        path_ = self._resolve(path)
        if path_:
            if path_.is_file():
                return path_.name
        return ""

    def getFolder(self, path: str) -> str:
        path_ = self._resolve(path)
        if path_:
            if path_.is_dir():
                return path_.name
        return ""


def make_interpreter(
    base: mobase.IFileTree, organizer: mobase.IOrganizer
) -> WizardInterpreter[Any]:
    manager = MO2ManagerModInterface(base, organizer)
    severity = MO2SeverityContext(organizer)

    factory = make_runner_context_factory(manager.subpackages, manager, severity)

    return WizardInterpreter(factory)
