# [MO2](https://github.com/ModOrganizer2/modorganizer) plugin for FOMOD installers using C# scripts

This is simple installer plugin for Mod Organizer 2 to handle FOMOD installers containing C# script.

Without this plugin, Mod Organizer 2 will use NCC to install such plugins, but NCC has a few limitations when
used within MO2:

- Mod detection will not work, so the installer will not be able to check for active mods or even existing files.
- Modifying settings (`FalloutNV.ini`, etc.) does not work properly with the NCC installer (it may work if you
    do not use per-profile settings, but that is not recommended).

This installer is better integrated with Mod Organizer 2 and should handle any kind of FOMOD installer containing
a C# script. Feel free to [open an issue](https://github.com/ModOrganizer2/modorganizer/issues/new?assignees=&labels=issue+report&template=issue-report.md)
if you find a bug.

The installer will never modify settings silently. If during installation, the script tries to modify settings,
they will be stored in memory and presented to you at the end of the installation. You will then be able to either
apply the settings, save them inside the mod folder or discard them.

**Warning:** This plugin currently only works with Mod Organizer 2 development build 2.3.0 alpha 10!
