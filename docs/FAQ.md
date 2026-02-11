# FAQ

## Where Are Logs Stored?
Logs are written next to the app binary in a `logs/` folder.

## Does Removing an Instance Delete My Files?
Not by default. It removes the profile from the menu, and gives you the option to delete it if you want to.

## Do I Need UMU and 7ZZ?
Yes, both are bundled in release artifacts under `bin/`.

## Do I Need to Install 9 Million Different Dependencies?
No, the dependencies are handled by NaK! If there is something missing I will gladly add it to the list. This also includes WINEDLLOVERWRITES as well!

## Make Sure to Select a Proton in the Settings Before Playing!
You will need to create a prefix before being able to play, I have added a Wine/Proton tab in the settings. Just let it install all the dependencies and then you are good to play.
<img width="2818" height="1688" alt="Screenshot_20260211_021905" src="https://github.com/user-attachments/assets/3437628f-7e75-4a07-b643-62b1cc130bbf" />

## Does It Work with Existing Modlists?
Yes, it can phrase wine paths and read them out as Linux paths in the GUI. It will also save the paths as wine paths in case you move to MO2 via proton/wine.

To use a portable install you can run this as an example. `flatpak run com.fluorine.manager --instance /home/luke/Games/Skyrim/` and it should pick right up where you left off.
