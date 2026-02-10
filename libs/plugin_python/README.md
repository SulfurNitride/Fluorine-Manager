# ModOrganizer2 - Python Proxy

This repository contains the Python proxy plugin for ModOrganizer2.
The proxy plugin allow developers to write Python plugins for ModOrganizer2.

## Setup, build, tests

This repository is part of MO2 main repositories and should usually be build using
[`mob`](https://github.com/ModOrganizer2/mob).

## Organization

This repositories contains 5 sub-projects in `src`.
The interface between Python and C++ is done using the
[`pybind11`](https://github.com/pybind/pybind11) library.
See the `README` in the subfolder (when there is one) for more details.

- [`src/proxy`](src/proxy/) contains the actual proxy plugin.
  This project is a simple interface between MO2 and the runner (see below).
  The CMake code:
  - generates the `plugin_python.dll` library,
  - generates the translation file (under `src/`),
  - installs necessary files for the plugin (Python DLL, Python libraries, etc),
    including `mobase`.
- [`src/runner`](src/runner/) contains the Python runner. This is the project that
  instantiates a Python interpreter and load/unload Python plugins.
- [`src/pybind11-qt`](src/pybind11-qt/) contains many utility stuff to interface
  pybind11 with Qt and PyQt.
- [`src/pybind11-utils`](src/pybind11-utils/) contains some utility stuff pybind11.
  This project is header-only.
- [`src/mobase`](src/mobase) contains the Python plugin interface.
  - This projects generates the `mobase` Python library.

Some (woefully incomplete) tests are available under `tests`, split in three
sub-directories:

- [`tests/mocks`](tests/mocks/) simply contains mocks of `uibase` interfaces to be used
  in the two other test projects.
- [`tests/python`](tests/python/) contains Python tests for `pytest`.
  This project generates a bunch of Python test libraries that are then imported in
  Python test files (`test_*.py`) and tested using `pytest`.
  These tests mostly cover the pybind11 Qt and utility stuff, and some standalone
  MO2 classes and functions (`IFileTree`, `GuessedString`, etc).
- [`tests/runner`](tests/runner/) contains C++ tests, using GTest
  Tests in this project instantiate a Python runner and then use it to check that
  plugins implemented in Python can be used properly on the C++ side.

## Building & Running tests

Tests are not built by default with `mob`, so you will need to run `cmake` manually
with the proper arguments.

You need to define `PLUGIN_PYTHON_TESTS` with `-DPLUGIN_PYTHON_TESTS` when running
the configure step of cmake.

You can then build the tests

```bash
# replace vsbuild with your build folder
cmake --build vsbuild --config RelWithDebInfo --target "python-tests" "runner-tests"
```

To run the tests, use `ctest`

```bash
# replace vsbuild with your build folder
ctest.exe --test-dir vsbuild -C RelWithDebInfo
```
