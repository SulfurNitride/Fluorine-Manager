# FAQ

## Where Are Logs Stored?
Logs are written next to the app binary in a `logs/` folder.

## Does Removing an Instance Delete My Files?
Not by default. It removes the profile from the menu, and gives you the option to delete it if you want to.

## Do I Need UMU and 7ZZ?
Yes, both are bundled in release artifacts under `bin/`.

## Do I Need to Install 9 Million Different Dependencies?
No, the dependencies are handled by NaK! If there is something missing I will gladly add it to the list. This also includes WINEDLLOVERWRITES as well!

## Getting Started!

In the release archive you will find a bin folder and `fluorine-manager`, you might need to make it an executable and this can be done by `chmod +x fluorine-manager`. And then it can be ran by either running `./fluorine-manager` or double-clicking the executable. You will be greeted with this window:
<img width="2993" height="1931" alt="image" src="https://github.com/user-attachments/assets/2643a214-8759-4573-aeed-fb8a6a6d4531" />

## Is It Safe to Import an MO2 I Already Have Setup?
Absolutely in the bottom of the screen there is an Import MO2 button which all you will need to do, is point it to the root of your MO2 folder.

A note about this though, is so far I haven't had any issues with it corrupting my list. I would highly recommend making a backup of your mods and profiles folder just to be on the safe side.

## What Do I Do After I Made an Instance or Imported an MO2?
After you make an instance or import an MO2, the first step is to go to the settings. And go to Wine/Proton, and pick a proton. Currently only Proton 10+ is supported. Once you select your proton, press the create prefix button. This will generate a non steam game with NaK, and install all dependencies with NaK. Ontop of this any compatdata documents or appdata should be symlinked to the prefix.

<img width="2993" height="1931" alt="image" src="https://github.com/user-attachments/assets/75b4f65d-3b6f-4f4c-999c-538fe1150e81" />
<img width="2993" height="1931" alt="Screenshot_20260208_223203" src="https://github.com/user-attachments/assets/1682fd4c-291d-46a4-8b86-e5a2da5f8211" />

## How Do I Enable NXM Handling?
In the settings at the bottom you will provide your Nexus Mods API key, and below that click on `Register as NXM Handler`. The Nexus API Key is the personal API key at the bottom of: https://www.nexusmods.com/settings/api-keys
<img width="2993" height="1931" alt="image" src="https://github.com/user-attachments/assets/9d6dd347-b436-407b-b33b-d26cc5fdfe79" />

## How Do I Start Playing the Game?
The first step will to be clicking on Edit and setup executables.

<img width="2993" height="1931" alt="image" src="https://github.com/user-attachments/assets/1c3e7a6b-6b70-4713-ab0b-301842362c18" />

Examples including Root Builder.
<img width="1574" height="1110" alt="image" src="https://github.com/user-attachments/assets/236ce11e-5acf-4755-92cf-15d282cf917c" />
<img width="1574" height="1110" alt="image" src="https://github.com/user-attachments/assets/fee847cb-80e2-47d5-8c45-22a11c17c305" />

I have not done extensive testing with other tools yet, but if you review the first screenshot and apply them that way they should work.


