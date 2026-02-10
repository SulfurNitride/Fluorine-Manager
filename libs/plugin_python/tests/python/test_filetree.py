from typing import TypeAlias, cast

import mobase

import mobase_tests.filetree as m


def test_filetype():
    FT = mobase.FileTreeEntry.FileTypes

    assert mobase.FileTreeEntry.FILE == FT.FILE
    assert m.is_file(FT.FILE)
    assert not m.is_directory(FT.FILE)
    assert m.value(FT.FILE) == FT.FILE.value

    assert mobase.FileTreeEntry.DIRECTORY == FT.DIRECTORY
    assert not m.is_file(FT.DIRECTORY)
    assert m.is_directory(FT.DIRECTORY)
    assert m.value(FT.DIRECTORY) == FT.DIRECTORY.value

    assert mobase.FileTreeEntry.FILE_OR_DIRECTORY == FT.FILE_OR_DIRECTORY
    assert m.is_file(FT.FILE_OR_DIRECTORY)
    assert m.is_directory(FT.FILE_OR_DIRECTORY)
    assert m.value(FT.FILE_OR_DIRECTORY) == FT.FILE_OR_DIRECTORY.value

    assert FT.FILE_OR_DIRECTORY == FT.FILE | FT.DIRECTORY
    assert m.is_file(FT.FILE | FT.DIRECTORY)
    assert m.is_directory(FT.FILE | FT.DIRECTORY)
    assert m.value(FT.FILE | FT.DIRECTORY) == (FT.FILE.value | FT.DIRECTORY.value)

    assert m.is_file(FT.FILE_OR_DIRECTORY & FT.FILE)
    assert not m.is_directory(FT.FILE_OR_DIRECTORY & FT.FILE)

    assert not m.is_file(FT.FILE_OR_DIRECTORY & FT.DIRECTORY)
    assert m.is_directory(FT.FILE_OR_DIRECTORY & FT.DIRECTORY)

    assert m.is_file(FT.FILE_OR_DIRECTORY & ~FT.DIRECTORY)
    assert not m.is_directory(FT.FILE_OR_DIRECTORY & ~FT.DIRECTORY)


_tree_values: TypeAlias = list["str | tuple[str, _tree_values]"]


def make_tree(
    values: _tree_values, root: mobase.IFileTree | None = None
) -> mobase.IFileTree:
    if root is None:
        root = cast(mobase.IFileTree, mobase.private.makeTree())  # type: ignore

    for value in values:
        if isinstance(value, str):
            root.addFile(value)
        else:
            sub_tree = root.addDirectory(value[0])
            make_tree(value[1], sub_tree)

    return root


def test_walk():
    tree = make_tree(
        [("a", []), ("b", ["u", "v"]), "c.x", "d.y", ("e", [("q", ["c.t", ("p", [])])])]
    )

    assert {"a", "b", "b/u", "b/v", "c.x", "d.y", "e", "e/q", "e/q/c.t", "e/q/p"} == {
        e.path("/") for e in tree.walk()
    }

    entries: list[str] = []
    for e in tree.walk():
        if e.name() == "e":
            break
        entries.append(e.path("/"))
    assert {"a", "b", "b/u", "b/v"} == set(entries)


def test_glob():
    tree = make_tree(
        [("a", []), ("b", ["u", "v"]), "c.x", "d.y", ("e", [("q", ["c.t", ("p", [])])])]
    )

    assert {"a", "b", "b/u", "b/v", "c.x", "d.y", "e", "e/q", "e/q/c.t", "e/q/p"} == {
        e.path("/") for e in tree.glob("**/*")
    }

    assert {"d.y"} == {e.path("/") for e in tree.glob("**/*.y")}
