import os
import site
import sys
from pathlib import Path
from typing import cast

import pybind11_stubgen as py11stubs

typings_dir = Path(__file__).parent
mobase_tests_dir = Path(__file__).parent.parent.joinpath(
    "vsbuild", "tests", "python", "pylibs", "mobase_tests"
)

site.addsitedir(str(mobase_tests_dir.parent))

os.add_dll_directory(str(Path(cast(str, os.getenv("QT_ROOT"))).joinpath("bin")))
os.add_dll_directory(str(os.getenv("UIBASE_PATH")))

from PyQt6.QtWidgets import QApplication  # noqa: E402

app = QApplication(sys.argv)

args = py11stubs.arg_parser().parse_args(["dummy"], namespace=py11stubs.CLIArgs())

parser = py11stubs.stub_parser_from_args(args)
printer = py11stubs.Printer(invalid_expr_as_ellipses=True)  # type: ignore

for path in mobase_tests_dir.glob("*.pyd"):
    name = path.name.split(".")[0]
    py11stubs.run(
        parser,
        printer,
        f"mobase_tests.{name}",
        typings_dir.joinpath("mobase_tests"),
        sub_dir=None,
        dry_run=False,
        writer=py11stubs.Writer(stub_ext="pyi"),  # type: ignore
    )
