# MO2 CMake Common

This repository contains useful CMake macro and functions that are used to ease creating
CMake configuration for most of MO2 repositories (e.g.,
[uibase](https://github.com/ModOrganizer2/modorganizer-uibase),
[plugin_python](https://github.com/ModOrganizer2/modorganizer-plugin_python) or
[ModOrganizer2](https://github.com/ModOrganizer2/modorganizer) itself).

## Getting Started

### 1. With VCPKG

Add [MO2 VCPKG registry](https://github.com/ModOrganizer2/vcpkg-registry) to your
`vcpkg-configuration.json`:

```json
{
  "default-registry": {
    "kind": "git",
    "repository": "https://github.com/Microsoft/vcpkg",
    "baseline": "f61a294e765b257926ae9e9d85f96468a0af74e7"
  },
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/ModOrganizer2/vcpkg-registry",
      "baseline": "27d8adbfe9e4ce88a875be3a45fadab69869eb60",
      "packages": ["mo2-cmake"]
    }
  ]
}
```

Add `mo2-cmake` to your VCPKG dependencies (`vcpkg.json`) and then import the utilities
with `find_package` in your CMake configuration:

```cmake
find_package(mo2-cmake CONFIG REQUIRED)
```

### 2. Manually

Clone this repository somewhere and then `include(mo2.cmake)` in your CMake
configuration files.

## Usage

Be aware that using these utilities will automatically set some (not too intrusive)
global variable on your project.

In order to properly use this package, you should set `CMAKE_INSTALL_PREFIX` to a valid
location.
There are two possible way of using this package controlled by the `MO2_INSTALL_IS_BIN`
option:

- if `MO2_INSTALL_IS_BIN` is `OFF` (default), this assumes a layout with a `bin`,
  `lib`, `include` and `pdb` folder under `CMAKE_INSTALL_PREFIX`,
- if `MO2_INSTALL_IS_BIN` is `ON` (default when building standalone), this assumes
  that `CMAKE_INSTALL_PREFIX` point directly to the equivalent `bin` folder.

Importing the utilities will make the following variables available:

- `MO2_QT_VERSION` - The Qt version used by MO2, as `major.minor.patch`.
- `MO2_QT_MAJOR_VERSION`, `MO2_QT_MINOR_VERSION` and `MO2_QT_PATCH_VERSION` -
  Respectively the major, minor and patch version of Qt used by MO2.
- `MO2_PYTHON_VERSION` - The Python version used by MO2 as `major.minor` (patch version
  should not be relevant).

All functions are prefixed by `mo2_` and should not conflict with other existing
functions.

### Generic Utilities

- `mo2_set_if_not_defined` - Set a variable to a given value if the variable is not
  defined.
- `mo2_add_subdirectories` -
- `mo2_find_python_executable` - Find Python executable.
- `mo2_find_git_hash` - Find the hash of the current git HEAD.
- `mo2_find_qt_executable` - Find a given Qt executable.
- `mo2_set_project_to_run_from_install` - Configure the debug executable for a VS project.
- `mo2_add_filter` - Add a source group filter.
- `mo2_deploy_qt_for_tests` - Deploy Qt DLLs, etc., for tests.
- `mo2_deploy_qt` - Deploy Qt DLLs for ModOrganizer2.
- `mo2_add_lupdate` - Create a target to run Qt `lupdate`.
- `mo2_add_lrelease` - Create a target to Qt `lrelease`.
- `mo2_add_translations` - Add targets to generate translations for the given target.

### C++ Utilities

TODO:

- `mo2_configure_warnings` - Utility function configure warnings for a target.
- `mo2_configure_sources` - Glob and configure sources, including Qt-related files
  (.ui, etc.) for a target.
- `mo2_configure_msvc` - Set some MSVC-specific flag for a target.
- (Deprecated) `mo2_configure_target` - Combine the above function + Extra stuff.
- (Deprecated) `mo2_configure_plugin`
- `mo2_configure_tests` - TO BE CHANGED
- `mo2_install_plugin` - Install a plugin.

### Python Utilities

- `mo2_python_uifiles` - Add a target to generate `.py` files from `.ui` files.
- `mo2_python_pip_install` - Install Python packages.
- `mo2_python_requirements` - Install plugin requirements from a `plugin-requirements.txt`
  file, ready to ship.
- `mo2_configure_python_module` - Configure a Python module plugin.
- (Deprecated) `mo2_configure_python_simple` - Configure a Python single file plugin.
- `mo2_configure_python` - Wrapper for the two above functions.
