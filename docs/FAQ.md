# FAQ

## Where Are Logs Stored?
Logs are written next to the app binary in a `logs/` folder.

## Does Removing an Instance Delete My Files?
Not by default. It removes the profile from the menu, and gives you the option to delete it if you want to.

## Do I Need to Install 9 Million Different Dependencies?
No, the dependencies are handled by NaK! If there is something missing I will gladly add it to the list. This also includes WINEDLLOVERWRITES as well!

## How Do I Set Up Fluorine Before Playing?
Open **Settings > Wine/Proton**, select a Proton version, choose the prefix
location (or keep the default), and click **Set Up Fluorine**. Wait for setup
to finish installing the Windows components before launching a game or tool.

## Do I Need to Configure FUSE Permissions?

FUSE mounts are accessible only to the mounting user by default; no change to
`/etc/fuse.conf` is needed. To share an instance's mounts with other users
(including root), enable **Settings > Wine/Proton > VFS > Allow other users to
access FUSE mounts (allow_other)**. This takes effect on the next FUSE mount,
enforces file permissions, and does not affect USVFS launches.

For this optional setting, an administrator must uncomment `user_allow_other`
in the host's `/etc/fuse.conf` (or add that line if missing). Fluorine does not
modify the host configuration.

## Does It Work with Existing Modlists?
Yes, it can phrase wine paths and read them out as Linux paths in the GUI. It will also save the paths as wine paths in case you move to MO2 via proton/wine.

To use a portable install you can run this as an example. `flatpak run com.fluorine.manager --instance /home/luke/Games/Skyrim/` and it should pick right up where you left off.

And all the buttons like associate with mod manager downloads button and MO2 OAuth also works.

FAQ is going to be updated with more info in the future.
