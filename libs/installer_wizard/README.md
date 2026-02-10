# MO2 BAIN Wizard Installer Plugin

This plugin can be used to install BAIN archives containing a wizard script (`wizard.txt`).

## How to install?

Go to the [releases page](https://github.com/Holt59/modorganizer-installer_wizard/releases) and download
the latest release for your MO2 version.

### A few words on INI Tweaks

Mod Organizer 2 does not currently manage INI Tweaks, so the Wizard installer is partially functional
regarding them.

- The installer will create proper INI Tweaks when requested, but these will not be applied
  to the game INI files automatically. If INI Tweaks are present, a pop-up should appear at
  the end of the installation.
- INI Tweaks for OBSE script are directly applied to the OBSE scripts.

## How to contribute?

### Setting-up the environment

Below are the steps to setup a development environment.


1. Clone this repository into the Mod Organizer 2 plugins folder.

```bash
# (Optional) you can change the name of the folder:
git clone https://github.com/Holt59/modorganizer-installer_wizard installer_wizard
```

2. **Requirements:** You need a Python 3.8 installation. The list of requirements is in
    [`requirements.txt`](requirements.txt):

```bash
# Those are only the development requirements.
pip install -r requirements
```

3. "Build" the installer:

```bash
# This will install the 3rd party libraries in src/lib (required for the installer) and convert the .ui files into .py files.
make.ps1
```

4. Create a root `__init__.py` - MO2 will not find and load the plugin unless there is a
    `__init__.py` file in the root of the folder, so you need to create one:

```python
from .src import createPlugin
```

### Opening a Pull-Request

Once you are satisfied with your changes, you can
[open a pull-request](https://github.com/Holt59/modorganizer-installer_wizard/pulls).
Before doing so, you should check that your code is properly
formatted and clean:

```bash
# The -vv option is mandatory, otherwise tox will crash...
tox -vv -e py38-lint
```

### The interpreter

The interpreter used by the installer is from the
[`bain-wizard-interpreter`](https://github.com/Holt59/bain-wizard-interpreter) package.
For issues related to interpreter (i.e. the script is wrongly parsed), open issues on the interpreter repository.

## License

MIT License

Copyright (c) 2020 Mikaël Capelle

See [LICENSE](LICENSE) for more information.

**Note:** The release archives contains external libraries that are under their
own LICENSE.
