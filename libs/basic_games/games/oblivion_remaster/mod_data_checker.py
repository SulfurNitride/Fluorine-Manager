from typing import cast

import mobase

from .constants import PLUGIN_NAME


def _parent(entry: mobase.FileTreeEntry):
    """
    Same as entry.parent() but always returns a mobase.IFileTree and never None.
    """
    return cast(mobase.IFileTree, entry.parent())


class OblivionRemasteredModDataChecker(mobase.ModDataChecker):
    # These directories are generally considered valid, but may require additional checks.
    # These represent top level directories in the mod.
    _dirs = ["Data", "Paks", "OBSE", "Movies", "UE4SS", "GameSettings", "Root"]
    # Directories considered valid for 'Data' though many of these may be obsolete.
    _data_dirs = [
        "meshes",
        "textures",
        "music",
        "fonts",
        "interface",
        "shaders",
        "strings",
        "materials",
    ]
    # Data file extensions considered valid. Unclear if BSAs are actually used.
    _data_extensions = [".esm", ".esp", ".bsa"]

    def __init__(self, organizer: mobase.IOrganizer):
        super().__init__()
        self._organizer = organizer

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        status = mobase.ModDataChecker.INVALID
        # These represent common mod structures that include UE4SS base files.
        # These should generally be pruned or moved into a Root Builder path.
        if filetree.find("ue4ss/UE4SS.dll") is not None:
            return mobase.ModDataChecker.FIXABLE
        elif (
            filetree.find("OblivionRemastered/Binaries/Win64/ue4ss/UE4SS.dll")
            is not None
        ):
            return mobase.ModDataChecker.FIXABLE

        # Crawl the directory tree to check mod structure.
        for entry in filetree:
            name = entry.name().casefold()
            parent = entry.parent()
            assert parent is not None
            # These are top-level file entries.
            if parent.parent() is None:
                if isinstance(entry, mobase.IFileTree):
                    # Look for valid top level directories.
                    if name in [dirname.lower() for dirname in self._dirs]:
                        if name == "ue4ss":
                            """
                            The UE4SS mod directory should contain either mod directories with
                            a 'scripts/main.lua' file, or 'shared' library files. Certain common
                            'preset settings' files are also acceptable.
                            """
                            mods = entry.find("Mods")
                            if isinstance(mods, mobase.IFileTree):
                                """
                                UE4SS intrinsically maps to the 'Mods' directory, so if this directory
                                is present, it should be relocated.
                                """
                                for sub_entry in mods:
                                    if isinstance(sub_entry, mobase.IFileTree):
                                        if sub_entry.find("scripts/main.lua"):
                                            status = mobase.ModDataChecker.FIXABLE
                                            break
                                        if sub_entry.name().casefold() in [
                                            "shared",
                                            "npcappearancemanager",
                                            "naturalbodymorph",
                                        ]:
                                            status = mobase.ModDataChecker.FIXABLE
                                            break
                            else:
                                for sub_entry in entry:
                                    if isinstance(sub_entry, mobase.IFileTree):
                                        # Files are present in the correct directory. Mark valid.
                                        if sub_entry.find("scripts/main.lua"):
                                            status = mobase.ModDataChecker.VALID
                                            break
                                        if sub_entry.name().casefold() in [
                                            "shared",
                                            "npcappearancemanager",
                                            "naturalbodymorph",
                                        ]:
                                            status = mobase.ModDataChecker.VALID
                                            break
                        else:
                            # All other base directories are considered valid
                            status = mobase.ModDataChecker.VALID
                        # No need to continue checks if the directory looks valid
                        if status == mobase.ModDataChecker.VALID:
                            break
                    elif name in [dirname.lower() for dirname in self._data_dirs]:
                        # Found a 'Data' subdirectory. Should be moved into 'Data'.
                        status = mobase.ModDataChecker.FIXABLE
                    else:
                        # Parse other directories for potential mod files.
                        for sub_entry in entry:
                            if sub_entry.isFile():
                                sub_name = sub_entry.name().casefold()
                                if sub_name.endswith(".exe"):
                                    # Trying to handle EXE files is problematic, let the user figure it out
                                    return mobase.ModDataChecker.INVALID
                                if sub_name.endswith((".pak", ".bk2")):
                                    # Found Pak files or movie files, should be fixable
                                    status = mobase.ModDataChecker.FIXABLE
                                elif sub_name.endswith(tuple(self._data_extensions)):
                                    # Found plugins or BSA files, should be fixable
                                    status = mobase.ModDataChecker.FIXABLE
                            else:
                                if name == "Paks":
                                    # Found Paks directory as subdirectory, should be fixable
                                    status = mobase.ModDataChecker.FIXABLE
                        # Iterate into subdirectories so we can check the entire archive
                        new_status = self.dataLooksValid(entry)
                        if new_status != mobase.ModDataChecker.INVALID:
                            status = new_status
                        if status == mobase.ModDataChecker.VALID:
                            break
                else:
                    if name.endswith(".exe"):
                        return mobase.ModDataChecker.INVALID
                    if name.endswith(tuple(self._data_extensions + [".pak", ".bk2"])):
                        status = mobase.ModDataChecker.FIXABLE
            else:
                # Section is for parsing subdirectories
                if isinstance(entry, mobase.IFileTree):
                    if name in [dir_name.lower() for dir_name in self._dirs]:
                        status = mobase.ModDataChecker.FIXABLE
                    if name in [dir_name.lower() for dir_name in self._data_dirs]:
                        status = mobase.ModDataChecker.FIXABLE
                    else:
                        new_status = self.dataLooksValid(entry)
                        if new_status != mobase.ModDataChecker.INVALID:
                            status = new_status
                else:
                    if name.endswith(".exe"):
                        return mobase.ModDataChecker.INVALID
                    if name.endswith(
                        tuple(self._data_extensions + [".pak", ".lua", ".bk2"])
                    ):
                        status = mobase.ModDataChecker.FIXABLE
                if status == mobase.ModDataChecker.VALID:
                    break
        return status

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        """
        Main fixer function. Iterates files, using 'parse_directory' for subdirectory search and fixing.
        """

        # UE4SS is packaged with many mods (or standalone) and should be processed into Root if found.
        # This can avoid a lot of unnecessary iterations.
        ue4ss_dll = filetree.find("ue4ss/UE4SS.dll")
        if ue4ss_dll is None:
            ue4ss_dll = filetree.find(
                "OblivionRemastered/Binaries/Win64/ue4ss/UE4SS.dll"
            )
        if ue4ss_dll is not None and (ue4ss_folder := ue4ss_dll.parent()) is not None:
            entries: list[mobase.FileTreeEntry] = []
            for entry in _parent(ue4ss_folder):
                entries.append(entry)
            for entry in entries:
                filetree.move(
                    entry,
                    "Root/OblivionRemastered/Binaries/Win64/",
                    mobase.IFileTree.MERGE,
                )

        # Similar to the above, many mods pack files relative to the root game directory. Some common paths can be
        # automatically moved into the appropriate directory structures to avoid needless iteration.
        exe_dir = filetree.find(r"OblivionRemastered\Binaries\Win64")
        if isinstance(exe_dir, mobase.IFileTree):
            gamesettings_dir = exe_dir.find("GameSettings")
            if isinstance(gamesettings_dir, mobase.IFileTree):
                gamesettings_main = self.get_dir(filetree, "GameSettings")
                gamesettings_main.merge(gamesettings_dir, True)
                self.detach_parents(gamesettings_dir)
            obse_dir = exe_dir.find("OBSE")
            if isinstance(obse_dir, mobase.IFileTree):
                obse_main = self.get_dir(filetree, "OBSE")
                obse_main.merge(obse_dir, True)
                self.detach_parents(obse_dir)
            ue4ss_mod_dir = exe_dir.find("ue4ss/Mods")
            if isinstance(ue4ss_mod_dir, mobase.IFileTree):
                if self._organizer.pluginSetting(PLUGIN_NAME, "ue4ss_use_root_builder"):
                    ue4ss_main = self.get_dir(
                        filetree, "Root/OblivionRemastered/Binaries/Win64/ue4ss/Mods"
                    )
                else:
                    ue4ss_main = self.get_dir(filetree, "UE4SS")
                ue4ss_main.merge(ue4ss_mod_dir, True)
                self.detach_parents(ue4ss_mod_dir)
            if len(exe_dir):
                root_exe_dir = self.get_dir(
                    filetree, "Root/OblivionRemastered/Binaries"
                )
                parent = exe_dir.parent()
                exe_dir.moveTo(root_exe_dir)
                if parent:
                    self.detach_parents(parent)
            else:
                self.detach_parents(exe_dir)

        # Start the main directory iteration code
        directories: list[mobase.IFileTree] = []
        for entry in filetree:
            if isinstance(entry, mobase.IFileTree):
                directories.append(entry)
        for directory in directories:
            if directory.name().casefold() in [
                dirname.lower() for dirname in self._data_dirs
            ]:
                # Move detected 'Data' directories into 'Data'
                data_dir = self.get_dir(filetree, "Data")
                directory.moveTo(data_dir)
            elif directory.name().casefold() == "ue4ss":
                # Validate and correct UE4SS mod files into 'UE4SS'
                # Alternately, can use Root Builder path per user request
                mods = directory.find("Mods")
                if isinstance(mods, mobase.IFileTree):
                    for sub_entry in mods:
                        if isinstance(sub_entry, mobase.IFileTree):
                            if (
                                sub_entry.find("scripts/main.lua")
                                or sub_entry.name().casefold() == "shared"
                            ):
                                if self._organizer.pluginSetting(
                                    PLUGIN_NAME, "ue4ss_use_root_builder"
                                ):
                                    ue4ss_main = self.get_dir(
                                        filetree,
                                        "Root/OblivionRemastered/Binaries/Win64/ue4ss/Mods",
                                    )
                                    sub_entry.moveTo(ue4ss_main)
                                    self.detach_parents(directory)
                                else:
                                    parent = _parent(sub_entry)
                                    sub_entry.moveTo(directory)
                                    self.detach_parents(parent)
            elif directory.name().casefold() not in [
                dirname.lower() for dirname in self._dirs
            ]:
                # For non-valid directories, iterate into the directory
                filetree = self.parse_directory(filetree, directory)
        # Parsing top-level files
        entries: list[mobase.FileTreeEntry] = []
        # As we are changing the filetree, cache the current directory list and iterate on that
        for entry in filetree:
            entries.append(entry)
        for entry in entries:
            if entry.parent() == filetree and entry.isFile():
                name = entry.name().casefold()
                if name.endswith(".pak"):
                    # Move all pak|ucas|utoc files into "Paks\~mods"
                    paks_dir = self.get_dir(filetree, "Paks/~mods")
                    pak_files: list[mobase.FileTreeEntry] = []
                    for file in _parent(entry):
                        if file.isFile():
                            if (
                                file.name()
                                .casefold()
                                .endswith((".pak", ".ucas", ".utoc"))
                            ):
                                pak_files.append(file)
                    for pak_file in pak_files:
                        pak_file.moveTo(paks_dir)
                elif name.endswith(".bk2"):
                    # Top-level bk2 files should be moved to "Movies\Modern"
                    movies_dir = self.get_dir(filetree, "Movies/Modern")
                    movie_files: list[mobase.FileTreeEntry] = []
                    for file in _parent(entry):
                        if file.isFile():
                            if file.name().casefold().endswith(".bk2"):
                                movie_files.append(file)
                    for movie_file in movie_files:
                        movie_file.moveTo(movies_dir)
                elif name.endswith(tuple(self._data_extensions)):
                    # Files matching Data file extensions should be moved to "Data"
                    data_dir = self.get_dir(filetree, "Data")
                    data_files: list[mobase.FileTreeEntry] = []
                    for file in _parent(entry):
                        data_files.append(file)
                    for data_file in data_files:
                        data_file.moveTo(data_dir)
        return filetree

    def parse_directory(
        self, main_filetree: mobase.IFileTree, next_dir: mobase.IFileTree
    ) -> mobase.IFileTree:
        """
        Subdirectory iterator for fix(). Allows parsing all subdirectory files.

        :param main_filetree: The main mod filetree that should be returned.
        :param next_dir: The currently processed subdirectory of main_filetree.
        :returns: The updated main_filetree.
        """
        directories: list[mobase.IFileTree] = []
        for entry in next_dir:
            if isinstance(entry, mobase.IFileTree):
                directories.append(entry)
        for directory in directories:
            name = directory.name().casefold()
            stop = False
            for dir_name in self._dirs:
                if name == dir_name.lower():
                    main_dir = self.get_dir(main_filetree, dir_name)
                    if name == "ue4ss":
                        # UE4SS directories should presumably map to 'UE4SS' but check for a 'Mods' directory and move that instead.
                        if self._organizer.pluginSetting(
                            PLUGIN_NAME, "ue4ss_use_root_builder"
                        ):
                            ue4ss_dir = self.get_dir(
                                main_filetree,
                                "Root/OblivionRemastered/Binaries/Win64/ue4ss",
                            )
                            ue4ss_dir.merge(directory)
                        else:
                            mod_dir = directory.find("Mods")
                            if isinstance(mod_dir, mobase.IFileTree):
                                main_dir.merge(mod_dir)
                            else:
                                main_dir.merge(directory)
                    else:
                        main_dir.merge(directory)
                    self.detach_parents(directory)
                    stop = True
                    break
            if stop:
                continue
            if name in ["~mods", "logicmods"]:
                # These directories should represent Paks mods and should be moved into that directory.
                paks_dir = self.get_dir(main_filetree, "Paks")
                directory.moveTo(paks_dir)
                continue
            elif name in [dirname.lower() for dirname in self._data_dirs]:
                # These directories are typically associated with Data and should be moved into that directory.
                data_dir = self.get_dir(main_filetree, "Data")
                data_dir.merge(directory)
                self.detach_parents(directory)
                continue
            main_filetree = self.parse_directory(main_filetree, directory)
        for entry in next_dir:
            if entry.isFile():
                name = entry.name().casefold()
                if name.endswith(tuple(self._data_extensions)):
                    # Files matching Data extensions should be moved into 'Data'
                    data_dir = self.get_dir(main_filetree, "Data")
                    data_dir.merge(next_dir)
                    self.detach_parents(next_dir)
                elif name.endswith(".pak"):
                    # Loose pak files most likely should be installed to 'Paks\~mods' but check the parent directory
                    paks_dir = self.get_dir(main_filetree, "Paks")
                    if next_dir.name().casefold() == "paks":
                        paks_dir.merge(next_dir)
                        self.detach_parents(next_dir)
                        return main_filetree
                    elif next_dir.name().casefold() in ["~mods", "logicmods"]:
                        next_dir.moveTo(paks_dir)
                        return main_filetree
                    else:
                        parent = _parent(next_dir)
                        main_filetree.move(
                            next_dir, "Paks/~mods/", mobase.IFileTree.MERGE
                        )

                        self.detach_parents(parent)
                        return main_filetree
                elif name.endswith(".lua"):
                    # LUA files are generally associated with UE4SS mods. Most will probably be found before this point.
                    if next_dir.parent() and next_dir.parent() != main_filetree:
                        parent = _parent(next_dir).parent()
                        if self._organizer.pluginSetting(
                            PLUGIN_NAME, "ue4ss_use_root_builder"
                        ):
                            ue4ss_main = self.get_dir(
                                main_filetree,
                                "Root/OblivionRemastered/Binaries/Win64/ue4ss/Mods",
                            )
                            _parent(next_dir).moveTo(ue4ss_main)
                        else:
                            if main_filetree.find("UE4SS") is None:
                                main_filetree.addDirectory("UE4SS")
                            main_filetree.move(
                                _parent(next_dir),
                                "UE4SS/",
                                mobase.IFileTree.MERGE,
                            )
                        if parent is not None:
                            self.detach_parents(parent)
                        return main_filetree
                elif name.endswith(".bk2"):
                    movies_dir = self.get_dir(main_filetree, "Movies/Modern")
                    movies_dir.merge(next_dir)
                    self.detach_parents(next_dir)

        return main_filetree

    def detach_parents(self, directory: mobase.IFileTree) -> None:
        """
        Attempts to clean up empty archive directories after files have been moved.
        Find the top-most directory of an empty file tree where only the child directory is contained.
        Remove that filetree.

        :param directory: The directory tree to be pruned (starting with the lowest element).
        """

        if (
            directory.parent() is not None
            and (parent := directory.parent()) is not None
            and len(parent) == 1
        ):
            parent = parent if parent.parent() is not None else directory
            while (
                parent
                and (p_parent := parent.parent()) is not None
                and (pp_parent := p_parent.parent()) is not None
                and len(pp_parent) == 1
            ):
                parent = parent.parent()

            assert parent is not None
            parent.detach()
        else:
            directory.detach()

    def get_dir(self, filetree: mobase.IFileTree, directory: str) -> mobase.IFileTree:
        """
        Simple helper function that finds or creates a directory within a given filetree.

        :param filetree: The filetree in which to file or create the given directory
        :param directory: The directory name to return
        :return: The filetree of the created or found directory.
        """

        tree_dir = filetree.find(directory)
        if not isinstance(tree_dir, mobase.IFileTree):
            tree_dir = filetree.addDirectory(directory)
        return tree_dir
