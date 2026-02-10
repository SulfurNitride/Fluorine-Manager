import os
import sys

from PyQt6.QtWidgets import QApplication


def pytest_configure():
    global app

    os.add_dll_directory(str(os.getenv("UIBASE_PATH")))

    app = QApplication(sys.argv)
