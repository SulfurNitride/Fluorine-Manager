# Installing, updating, and removing Fluorine Manager

## Install from a release

Download the Linux x86-64 application asset named
`fluorine-manager-<version>.tar.gz` (or the rolling
`fluorine-manager-nightly-linux-x86_64.tar.gz`) from the
[Fluorine Manager releases](https://github.com/SulfurNitride/Fluorine-Manager/releases).
GitHub's automatically generated “Source code” ZIP files are not runnable
application packages.

Extract the archive into a new directory and run only its launcher:

```bash
tar xzf fluorine-manager-*.tar.gz
cd fluorine-manager
./fluorine-manager
```

The launcher verifies the bundle and transactionally publishes the managed
runtime to `~/.local/share/fluorine/bin`. The application runs from that stable
location, so deleting the downloaded archive or extraction directory after a
successful launch does not uninstall Fluorine. `ModOrganizer-core` and files
under `lib/` are internal components and are not supported entry points.

The package intentionally uses several host libraries, including the graphics
stack, X11/Wayland, Fontconfig, OpenSSL, glibc, and libstdc++. Steam and a Proton
installation are needed for Windows games; native games such as OpenMW do not
require Proton. The native FUSE backend also requires access to `/dev/fuse` and
the host `fusermount3` helper, normally provided by the distribution's `fuse3`
package.

## Instances, data, and logs

“Portable instance” describes where an instance's `ModOrganizer.ini`, mods,
profiles, and downloads live. It does not mean the application binary runs in
place without installing its managed runtime.

- Global instances and the default Wine prefix live under
  `~/.local/share/fluorine`.
- Portable instances live in the directory containing their
  `ModOrganizer.ini`.
- Manager session logs live in `<selected instance>/logs`.
- Prefix setup and dependency-installer diagnostics live in
  `~/.local/share/fluorine/logs`.
- Application-wide instance selection and preferences use Qt's user config
  location: `$XDG_CONFIG_HOME/Mod Organizer Team/Mod Organizer.conf` when that
  variable is set, otherwise
  `~/.config/Mod Organizer Team/Mod Organizer.conf`.
- Nexus credentials are sensitive and stored separately at
  `~/.config/ModOrganizer/credentials.ini`.

An existing portable instance can be selected with:

```bash
~/.local/share/fluorine/bin/fluorine-manager \
  --instance '/absolute/path/to/instance'
```

## Updates

The in-app updater downloads a complete release bundle and publishes it through
the same serialized, recoverable transaction used by the launcher. A manual
update is also safe: extract the new release into its own directory and run its
`fluorine-manager` launcher.

Before the first manual update from a release that predates the typed bundle
manifest, close every Fluorine Manager window. Current releases coordinate
publication with the running application automatically.

## Legacy Flatpak data

Current releases are native packages; `flatpak run com.fluorine.manager` is no
longer a supported entry point. On native startup, Fluorine inventories known
historical data below `~/.var/app/com.fluorine.manager`, moves only unambiguous
prefix and global-instance directories, and imports settings only when their
identity is proven. Existing native destinations always win.

Ambiguous runtime/plugin data is preserved rather than merged or deleted. The
startup warning gives the path to a durable
`legacy-flatpak-migration-attention*.txt` report. Keep that report and both data
locations until every retained item has been reviewed.

## Recoverable removal

There is no recursive uninstaller because `~/.local/share/fluorine` contains
user instances and prefixes as well as the application runtime.

1. In Fluorine settings, click **Remove NXM Association**. This stores a
   global opt-out, so a later launch will not reclaim the association.
2. Close Fluorine and every launched game or tool.
3. Rename `~/.local/share/fluorine/bin` to a backup name rather than deleting it
   immediately. Older installations may contain user-added files there.
4. Remove the Fluorine-owned desktop entry and icon if present:
   `~/.local/share/applications/com.fluorine.manager.desktop` and
   `~/.local/share/icons/hicolor/256x256/apps/com.fluorine.manager.png`.
5. The association uses
   `$XDG_DATA_HOME/applications/com.fluorine.manager.nxm-handler.desktop`
   (normally `~/.local/share/applications`) and entries in
   `$XDG_CONFIG_HOME/mimeapps.list` (normally `~/.config`). The Remove button
   deletes only the marker-owned current desktop entry or a strictly
   recognized Fluorine historical desktop/wrapper pair, and surgically removes
   Fluorine's MIME entries. Older releases may leave
   `~/.local/bin/mo2-nxm-handler` or
   `~/.local/share/applications/mo2-nxm-handler.desktop`; inspect ambiguous or
   modified files before removing them manually.
6. If account and application preferences should also be removed, back up and
   then explicitly remove the Qt settings file and Nexus credentials listed
   above. Leave them in place when preserving login or instance-selection
   state.
7. After confirming that instances and tools no longer need anything in the
   backup, remove that backup explicitly.

Do not recursively delete `~/.local/share/fluorine` unless you intentionally
want to delete global instances, Wine prefixes, logs, caches, tools, updater
state, and migration records. Back up portable instances separately wherever
they reside.
