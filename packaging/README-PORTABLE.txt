Fluorine Manager relocatable release bundle
============================================

Start Fluorine with:

    ./fluorine-manager

Run only that launcher. ModOrganizer-core and the files under lib/ are internal
bundle components and are not supported entry points.

The extracted directory is a publication source, not a no-install application.
On launch, Fluorine transactionally publishes the verified runtime to:

    ~/.local/share/fluorine/bin

The managed copy runs from there so instances keep a stable launcher after the
release archive or extraction directory is removed. Global instances, prefixes,
logs, tools and migration state also live under ~/.local/share/fluorine; do not
delete that whole directory to uninstall the application runtime.

See INSTALLATION.md for update, log, migration and recoverable removal details.
Project support: https://github.com/SulfurNitride/Fluorine-Manager
