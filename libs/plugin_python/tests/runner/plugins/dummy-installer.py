# -*- encoding: utf-8 -*-

from typing import Union, cast

import mobase


# we do not implement everything since this will do fine if we do not call anything
# from the C++ side
#
class DummyInstaller(mobase.IPluginInstallerSimple):
    def isManualInstaller(self) -> bool:
        return False

    def priority(self) -> int:
        return 10

    def isArchiveSupported(self, tree: mobase.IFileTree) -> bool:
        return tree.find("needed-file.txt") is not None

    def install(
        self,
        name: mobase.GuessedString,
        tree: mobase.IFileTree,
        version: str,
        nexus_id: int,
    ) -> Union[
        mobase.InstallResult,
        mobase.IFileTree,
        tuple[mobase.InstallResult, mobase.IFileTree, str, int],
    ]:
        if tree.find("needed-file.txt") is None:
            return mobase.InstallResult.FAILED

        if tree.find("extra-file.txt"):
            name.update("new name")
            new_tree = tree.createOrphanTree()
            new_tree.move(tree, "subtree")
            cast(mobase.IFileTree, new_tree.find("subtree")).remove("extra-file.txt")
            return new_tree

        return (mobase.InstallResult.NOT_ATTEMPTED, tree, "2.4.5", 33)


def createPlugin() -> mobase.IPlugin:
    return DummyInstaller()  # type: ignore
