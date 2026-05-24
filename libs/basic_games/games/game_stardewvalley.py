import os
import struct
import sys

from PyQt6.QtCore import QDir, QFileInfo, qWarning

import mobase

from ..basic_features import BasicLocalSavegames
from ..basic_game import BasicGame


class StardewValleyModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        for e in filetree:
            if isinstance(e, mobase.IFileTree) and e.exists(
                "manifest.json", mobase.IFileTree.FILE
            ):
                return mobase.ModDataChecker.VALID

        return mobase.ModDataChecker.INVALID


def _has_native_linux_install(game_dir: QDir) -> bool:
    # Native Linux Stardew ships a `StardewValley` shell-script launcher and
    # has no `Stardew Valley.exe`.  Use that to distinguish from a Windows
    # install dropped under a Wine prefix.
    if sys.platform != "linux":
        return False
    if QFileInfo(game_dir, "Stardew Valley.exe").exists():
        return False
    return QFileInfo(game_dir, "StardewValley").exists()


def _native_linux_config_dir() -> QDir:
    xdg_config_home = os.environ.get("XDG_CONFIG_HOME")
    candidates = []
    if xdg_config_home:
        candidates.append(os.path.join(xdg_config_home, "StardewValley"))
    candidates.extend(
        [
            os.path.expanduser("~/.config/StardewValley"),
            os.path.expanduser(
                "~/.var/app/com.valvesoftware.Steam/.config/StardewValley"
            ),
            os.path.expanduser(
                "~/.var/app/com.heroicgameslauncher.hgl/config/StardewValley"
            ),
        ]
    )

    seen = set()
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        directory = QDir(candidate)
        if directory.exists():
            return directory

    return QDir(candidates[0])


def _clear_executable_stack(path: str) -> None:
    # Equivalent to `execstack -c`: clear PF_X on the PT_GNU_STACK program
    # header. Stardew's Galaxy libraries can ship with this bit set, which
    # makes some Linux systems refuse to load them.
    PT_GNU_STACK = 0x6474E551
    PF_X = 0x1

    if not os.path.exists(path):
        return

    try:
        with open(path, "r+b") as f:
            ident = f.read(16)
            if len(ident) != 16 or ident[:4] != b"\x7fELF":
                return

            elf_class = ident[4]
            endian = "<" if ident[5] == 1 else ">" if ident[5] == 2 else None
            if endian is None:
                return

            if elf_class == 2:
                f.seek(32)
                phoff = struct.unpack(endian + "Q", f.read(8))[0]
                f.seek(54)
                phentsize, phnum = struct.unpack(endian + "HH", f.read(4))
                flags_offset = 4
            elif elf_class == 1:
                f.seek(28)
                phoff = struct.unpack(endian + "I", f.read(4))[0]
                f.seek(42)
                phentsize, phnum = struct.unpack(endian + "HH", f.read(4))
                flags_offset = 24
            else:
                return

            for index in range(phnum):
                header_offset = phoff + index * phentsize
                f.seek(header_offset)
                p_type = struct.unpack(endian + "I", f.read(4))[0]
                if p_type != PT_GNU_STACK:
                    continue

                flags_pos = header_offset + flags_offset
                f.seek(flags_pos)
                flags = struct.unpack(endian + "I", f.read(4))[0]
                if flags & PF_X:
                    f.seek(flags_pos)
                    f.write(struct.pack(endian + "I", flags & ~PF_X))
                return
    except OSError as err:
        qWarning(f"Failed to clear executable stack on '{path}': {err}")


def _fix_native_linux_galaxy_libraries(game_dir: QDir) -> None:
    for library in ("libGalaxy64.so", "libGalaxyCSharpGlue.so"):
        _clear_executable_stack(game_dir.absoluteFilePath(library))


class StardewValleyGame(BasicGame):
    Name = "Stardew Valley Support Plugin"
    Author = "Syer10"
    Version = "0.1.0a"

    GameName = "Stardew Valley"
    GameShortName = "stardewvalley"
    GameNexusName = "stardewvalley"
    GameNexusId = 1303
    GameSteamId = 413150
    GameGogId = 1453375253
    GameBinary = "Stardew Valley.exe"
    GameDataPath = "Mods"
    GameDocumentsDirectory = "%DOCUMENTS%/StardewValley"
    GameSavesDirectory = "%GAME_DOCUMENTS%/Saves"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Stardew-Valley"
    )

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(StardewValleyModDataChecker())
        if self.isNativeLinux():
            _fix_native_linux_galaxy_libraries(self.gameDirectory())
            self._register_feature(BasicLocalSavegames(self))
        return True

    def isNativeLinux(self) -> bool:
        return _has_native_linux_install(self.gameDirectory())

    def documentsDirectory(self) -> QDir:
        if self.isNativeLinux():
            return _native_linux_config_dir()
        return super().documentsDirectory()

    def savesDirectory(self) -> QDir:
        if self.isNativeLinux():
            return QDir(self.documentsDirectory().absoluteFilePath("Saves"))
        return super().savesDirectory()

    def binaryName(self) -> str:
        if self.isNativeLinux():
            return "StardewValley"
        return super().binaryName()

    def executables(self):
        if self.isNativeLinux():
            return [
                mobase.ExecutableInfo(
                    "SMAPI",
                    QFileInfo(self.gameDirectory(), "StardewModdingAPI"),
                ),
                mobase.ExecutableInfo(
                    "Stardew Valley",
                    QFileInfo(self.gameDirectory(), "StardewValley"),
                ),
            ]
        return [
            mobase.ExecutableInfo(
                "SMAPI", QFileInfo(self.gameDirectory(), "StardewModdingAPI.exe")
            ),
            mobase.ExecutableInfo(
                "Stardew Valley", QFileInfo(self.gameDirectory(), "Stardew Valley.exe")
            ),
        ]
