# CLF3 engine updates

Fluorine checks the latest stable release of `SulfurNitride/CLF3` before starting
or resuming a Wabbajack installation. CLF3 is downloaded at runtime, independently
of Fluorine builds and releases. The minimum supported version is 0.2.5, which
fixes texture extraction from nested FOMOD archives.

The updater downloads `clf3-linux-x64.zip`, verifies its SHA-256 digest against
GitHub's release metadata, extracts `clf3` and `7zz`, and checks the executable's
version before making it current. The existing host protocol handshake still
checks compatibility before the installation proceeds. Status and cancellation
use the installation dialog's normal controls.

Downloaded engines live in `$XDG_DATA_HOME/fluorine/tools/clf3` (normally
`~/.local/share/fluorine/tools/clf3`). `current.json` selects a verified version.
The manifest is replaced atomically; failed or cancelled updates preserve the
previous version. Older engine directories remain available to installations
that are already running. If GitHub cannot be reached, an already cached engine
at or above the minimum version can be used, with a message in the install log.
A first-time download requires network access. A bad download or invalid release
produces an error instead of silently using an older engine.

For development, `FLUORINE_CLF3_PATH=/absolute/path/to/clf3` explicitly bypasses
the updater. Without that override, bundled binaries and `clf3` on `PATH` are
not selected. Building Fluorine does not clone, compile, download, or package
CLF3.

Update Fluorine once to obtain this updater. Subsequent CLF3 releases are picked
up when starting or resuming an installation without rebuilding Fluorine.
