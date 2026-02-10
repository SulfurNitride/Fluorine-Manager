import sys
from pathlib import Path

import pytest
from PyQt6.QtCore import QDir, QFileInfo

import mobase


def test_filepath_wrappers():
    # TBC that this works everywhere
    version = ".".join(map(str, sys.version_info[:3]))

    # from string, ok
    assert mobase.getProductVersion(sys.executable) == version

    # from path, ok
    assert mobase.getProductVersion(Path(sys.executable)) == version

    # from QDir, ko
    with pytest.raises(TypeError):
        mobase.getProductVersion(QDir(sys.executable))  # pyright: ignore[reportArgumentType]


def test_executableinfo():
    info = mobase.ExecutableInfo("exe", QFileInfo(sys.executable))
    assert info.binary() == QFileInfo(sys.executable)

    info = mobase.ExecutableInfo("exe", sys.executable)
    assert info.binary() == QFileInfo(sys.executable)

    info = mobase.ExecutableInfo("exe", Path(sys.executable))
    assert info.binary() == QFileInfo(sys.executable)

    info.withWorkingDirectory(Path(__file__).parent)
    assert info.workingDirectory() == QFileInfo(__file__).dir()

    info.withWorkingDirectory(".")
    assert info.workingDirectory() == QDir(".")

    info.withWorkingDirectory(Path("."))
    assert info.workingDirectory() == QDir(".")

    info.withWorkingDirectory(".")
    assert info.workingDirectory() == QDir(".")
