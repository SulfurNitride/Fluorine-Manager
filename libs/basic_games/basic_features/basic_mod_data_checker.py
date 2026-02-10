from __future__ import annotations

import fnmatch
import re
from dataclasses import dataclass, field
from typing import Iterable, Literal

import mobase

from .utils import is_directory


class OptionalRegexPattern:
    _pattern: re.Pattern[str] | None

    def __init__(self, globs: Iterable[str] | None) -> None:
        if globs is None:
            self._pattern = None
        else:
            self._pattern = OptionalRegexPattern.regex_from_glob_list(globs)

    @staticmethod
    def regex_from_glob_list(glob_list: Iterable[str]) -> re.Pattern[str]:
        """
        Returns a regex pattern form a list of glob patterns.

        Every pattern has a capturing group, so that `match.lastindex - 1` will
        give the `glob_list` index.
        """
        return re.compile(
            "|".join(f"({fnmatch.translate(f)})" for f in glob_list), re.I
        )

    def match(self, value: str) -> bool:
        if self._pattern is None:
            return False
        return bool(self._pattern.match(value))


class RegexPatterns:
    """
    Regex patterns for validation in `BasicModDataChecker`.
    """

    def __init__(self, globs: GlobPatterns) -> None:
        self.unfold = OptionalRegexPattern(globs.unfold)
        self.delete = OptionalRegexPattern(globs.delete)
        self.valid = OptionalRegexPattern(globs.valid)

        self.move = {
            key: re.compile(fnmatch.translate(key), re.I) for key in globs.move
        }
        self.ignore = OptionalRegexPattern(globs.ignore)

    def move_match(self, value: str) -> str | None:
        """
        Retrieve the first move patterns that matches the given value, or None if no
        move matches.
        """
        for key, pattern in self.move.items():
            if pattern.match(value):
                return key
        return None


def _merge_list(l1: list[str] | None, l2: list[str] | None) -> list[str] | None:
    if l1 is None and l2 is None:
        return None

    return (l1 or []) + (l2 or [])


@dataclass(frozen=True, unsafe_hash=True)
class GlobPatterns:
    """
    See: `BasicModDataChecker`
    """

    unfold: list[str] | None = None
    valid: list[str] | None = None
    delete: list[str] | None = None
    move: dict[str, str] = field(default_factory=dict[str, str])
    ignore: list[str] | None = None

    def merge(
        self, other: GlobPatterns, mode: Literal["merge", "replace"] = "replace"
    ) -> GlobPatterns:
        """
        Construct a new GlobPatterns by merging the current one with the given one.

        There are two different modes:
          - 'merge': In this mode, unfold/valid/delete are concatenated and move
            will contain the union of key from self and other, with values from other
            overriding common keys.
          - 'replace': The merged object will contains attributes from other, except
            for None attributes taken from self.

        Args:
            other: Other patterns to "merge" with this one.
            mode: Merge mode.

        Returns:
            A new glob pattern representing the merge of this one with other.
        """
        if mode == "merge":
            return GlobPatterns(
                unfold=_merge_list(self.unfold, other.unfold),
                valid=_merge_list(self.valid, other.valid),
                delete=_merge_list(self.delete, other.delete),
                move=self.move | other.move,
                ignore=_merge_list(self.ignore, other.ignore),
            )
        else:
            return GlobPatterns(
                unfold=other.unfold or self.unfold,
                valid=other.valid or self.valid,
                delete=other.delete or self.delete,
                move=other.move or self.move,
                ignore=other.ignore or self.ignore,
            )


class BasicModDataChecker(mobase.ModDataChecker):
    """Game feature that is used to check and fix the content of a data tree
    via simple file definitions.

    The file definitions support glob pattern (without subfolders) and are
    checked and fixed in definition order of the `file_patterns` dict.

    Args:
        file_patterns (optional): A GlobPatterns object, with the following attributes:
            ignore: [ "list of files and folders to ignore." ]
                # Check result: unchanged
            unfold: [ "list of folders to unfold" ],
                # (remove and move contents to parent), after being checked and
                # fixed recursively.
                # Check result: `mobase.ModDataChecker.VALID`.

            valid: [ "list of files and folders in the right path." ],
                # Check result: `mobase.ModDataChecker.VALID`.

            delete: [ "list of files/folders to delete." ],
                # Check result: `mobase.ModDataChecker.FIXABLE`.

            move: {"Files/folders to move": "target path"}
                # If the path ends with `/` or `\\`, the entry will be inserted
                # in the corresponding directory instead of replacing it.
                # Check result: `mobase.ModDataChecker.FIXABLE`.

    Example:

        BasicModDataChecker(
            GlobPatterns(
                valid=["valid_folder", "*.ext1"]
                move={"*.ext2": "path/to/target_folder/"}
            )
        )

    See Also:
        `mobase.IFileTree.move` for the `"move"` target path specs.
    """

    _file_patterns: GlobPatterns
    """Private `file_patterns`, updated together with `._regex` and `._move_targets`."""

    _regex_patterns: RegexPatterns
    """The regex patterns derived from the file (glob) patterns."""

    def __init__(self, file_patterns: GlobPatterns | None = None):
        super().__init__()

        self._file_patterns = file_patterns or GlobPatterns()
        self._regex_patterns = RegexPatterns(self._file_patterns)

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        status = mobase.ModDataChecker.INVALID

        rp = self._regex_patterns
        for entry in filetree:
            name = entry.name().casefold()

            if rp.ignore.match(name):
                continue
            if rp.unfold.match(name):
                if is_directory(entry):
                    status = self.dataLooksValid(entry)
                else:
                    status = mobase.ModDataChecker.INVALID
                    break
            elif rp.valid.match(name):
                if status is mobase.ModDataChecker.INVALID:
                    status = mobase.ModDataChecker.VALID
            elif rp.delete.match(name) or rp.move_match(name) is not None:
                status = mobase.ModDataChecker.FIXABLE
            else:
                status = mobase.ModDataChecker.INVALID
                break
        return status

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        rp = self._regex_patterns
        for entry in list(filetree):
            name = entry.name()

            if rp.ignore.match(name):
                continue
            # unfold first - if this match, entry is a directory (checked in
            # dataLooksValid)
            if rp.unfold.match(name):
                assert is_directory(entry)
                filetree.merge(entry)
                entry.detach()

            elif rp.valid.match(name):
                continue

            elif rp.delete.match(name):
                entry.detach()

            elif (move_key := rp.move_match(name)) is not None:
                target = self._file_patterns.move[move_key]
                filetree.move(entry, target)

        return filetree
