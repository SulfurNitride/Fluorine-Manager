from collections.abc import Callable
from enum import IntEnum
from re import match
from typing import Any, List, Set, cast

from mobase import (
    FileTreeEntry,
    IFileTree,
    IOrganizer,
    ModDataChecker,
    ModDataContent,
    ReleaseType,
    VersionInfo,
)

from ..basic_game import BasicGame


class Content(IntEnum):
    PACKAGE = 0
    SCRIPT = 1


class ValidationResult(IntEnum):
    INVALID = 0
    VALID = 1
    FIXABLE = 2


class TS4Game(BasicGame):
    Name = "The Sims 4 Support Plugin"
    Author = "R3z Shark, xieve"
    GameName = "The Sims 4"
    GameShortName = "thesims4"
    GameBinary = "Game/Bin/TS4_x64.exe"
    GameDataPath = "%DOCUMENTS%/Electronic Arts/The Sims 4/Mods"
    GameSteamId = 1222670
    GameOriginManifestIds = ["OFB-EAST:109552677"]
    GameOriginWatcherExecutables = ("TS4_x64.exe",)

    def version(self):
        return VersionInfo(1, 0, 0, ReleaseType.FINAL)

    def documentsDirectory(self):
        return self.dataDirectory()

    def init(self, organizer: IOrganizer):
        if super().init(organizer):
            self._register_feature(TS4ModDataChecker())
            self._register_feature(TS4ModDataContent())
            return True
        return False


class TS4ModDataChecker(ModDataChecker):
    # .package files are allowed at a maximum depth of 5 subfolders, script files can be at most one level deep
    # The first capturing group lazily captures any parent folders exceeding that depth, see below
    _fixableOrValid = r"(?i)^(.*?)((?:[^\\]+\\){0,5}[^\\]*\.package|(?:[^\\]+\\)?[^\\]*\.ts4script|(?:[^\\]+\\)scripts\\.*\.py)$"

    def dataLooksValid(self, filetree: IFileTree) -> ModDataChecker.CheckReturn:
        return cast(
            ModDataChecker.CheckReturn,
            self._fixOrValidateTree(filetree, validateMode=True),
        )

    def fix(self, filetree: IFileTree) -> IFileTree | None:
        return cast(IFileTree, self._fixOrValidateTree(filetree))

    def _fixOrValidateTree(
        self,
        tree: IFileTree,
        validateMode: bool = False,
    ) -> IFileTree | ModDataChecker.CheckReturn:
        validationResult = ValidationResult.INVALID
        checkReturn = ModDataChecker.INVALID
        walkReturn = IFileTree.CONTINUE

        def setValidationResult(
            newResult: ValidationResult = ValidationResult.INVALID,
            actionCallback: Callable[[], Any] = lambda: None,
        ):
            nonlocal validationResult
            nonlocal checkReturn
            nonlocal walkReturn
            if validateMode:
                validationResult = max(validationResult, newResult)
                match validationResult:
                    case ValidationResult.INVALID:
                        pass
                    case ValidationResult.VALID:
                        checkReturn = ModDataChecker.VALID
                    case ValidationResult.FIXABLE:
                        checkReturn = ModDataChecker.FIXABLE
                        walkReturn = IFileTree.STOP
            else:
                actionCallback()

        def fixOrValidateEntry(
            parentPath: str, entry: FileTreeEntry
        ) -> IFileTree.WalkReturn:
            path = f"{parentPath}{entry.name()}"
            fixableOrValid = match(self._fixableOrValid, path)
            if fixableOrValid:
                match fixableOrValid.groups():
                    case ["", _]:
                        setValidationResult(ValidationResult.VALID)
                    case [_, innerPath]:
                        # Move files that are nested too deeply up, preserving the inner folder structure
                        # E.g. a/b/c.ts4script will be moved to b/c.ts4script
                        setValidationResult(
                            ValidationResult.FIXABLE,
                            lambda: tree.move(entry, innerPath),
                        )
                    case _:
                        pass
            return walkReturn

        tree.walk(fixOrValidateEntry)

        if validateMode:
            return checkReturn
        else:
            return tree


class TS4ModDataContent(ModDataContent):
    def getAllContents(self: ModDataContent) -> List[ModDataContent.Content]:
        return [
            ModDataContent.Content(
                Content.PACKAGE, "Package", ":/MO/gui/content/plugin"
            ),
            ModDataContent.Content(Content.SCRIPT, "Script", ":/MO/gui/content/script"),
        ]

    def getContentsFor(self: ModDataContent, filetree: IFileTree) -> List[int]:
        contents: Set[int] = set()

        def getContentForEntry(path: str, entry: FileTreeEntry):
            nonlocal contents
            match entry.suffix():
                case "package":
                    contents.add(Content.PACKAGE)
                case "ts4script" | "py":
                    contents.add(Content.SCRIPT)
                case _:
                    pass
            if len(contents) == 2:
                return IFileTree.STOP
            else:
                return IFileTree.CONTINUE

        filetree.walk(getContentForEntry)
        return list(contents)
