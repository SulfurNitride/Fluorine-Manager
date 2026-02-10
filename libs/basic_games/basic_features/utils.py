from typing import TypeGuard

import mobase


def is_directory(entry: mobase.FileTreeEntry) -> TypeGuard[mobase.IFileTree]:
    return entry.isDir()
