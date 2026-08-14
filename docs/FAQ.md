# FAQ

## Where Are Logs Stored?
Manager session logs are written to the selected instance's `logs/` directory.
For a portable instance that is `<instance>/logs`; global instances live below
`~/.local/share/fluorine`. Prefix setup and dependency-installer diagnostics are
stored separately in `~/.local/share/fluorine/logs`.

Fluorine does not create or manage Linux crash dumps. If the host enables
systemd-coredump, use `coredumpctl list` and `coredumpctl info` to inspect its
records. Exported core files can contain the process's full memory, including
private account or download data, so review and handle them as sensitive files.

## Does Removing an Instance Delete My Files?

**Remove from list** does not delete instance content. It unregisters a portable
instance or disables a global instance's discovery file so the entry disappears
from the selector. **Delete instance…** is a separate destructive action; it
shows the exact files and directories selected for permanent deletion before
asking for confirmation.

## Do I Need to Install Many Dependencies?
The release bundles its application runtime and Fluorine can install supported
Wine-prefix dependencies. It intentionally uses the host graphics stack,
X11/Wayland, Fontconfig, OpenSSL, glibc, and libstdc++; these are normally
provided by the distribution. See [installation](installation.md) for the
current runtime boundary.

## Do I Need to Select Proton Before Playing?
Windows games and Windows modding tools need a selected Proton version and a
configured Wine prefix under **Settings > Wine/Proton**. Native Linux games
such as OpenMW do not use that Proton launch path.
<img width="2818" height="1688" alt="Screenshot_20260211_021905" src="https://github.com/user-attachments/assets/3437628f-7e75-4a07-b643-62b1cc130bbf" />

## Does It Work with Existing Modlists?
Yes. Fluorine translates Wine paths for the Linux GUI and preserves compatible
paths for MO2 under Proton/Wine.

To open an existing portable instance containing `ModOrganizer.ini`, run:

```bash
~/.local/share/fluorine/bin/fluorine-manager \
  --instance '/absolute/path/to/instance'
```

The historical Flatpak command is no longer a supported entry point. Native
Fluorine imports unambiguous data from the old Flatpak layout automatically and
reports anything that needs manual attention.

Browser “Download with manager” links and Nexus OAuth are supported.

FAQ is going to be updated with more info in the future.
