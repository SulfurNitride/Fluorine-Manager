/*
Copyright (C) 2020 Holt59. All rights reserved.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

// clang-format off

#ifndef BASE_SCRIPT_H
#define BASE_SCRIPT_H

#using <System.dll>
#using <System.Drawing.dll>
#using <System.Windows.Forms.dll>

#include <uibase/iplugininstaller.h>

/**
 * Note: The specification of BaseScript where taken from the Nexus-Mods installer_fomod extension
 * for Vortex: https://github.com/Nexus-Mods/fomod-installer
 */
namespace CSharp {

  using namespace System;
  using namespace System::Drawing;
  using namespace System::Windows::Forms;

  /// <summary>
  /// Describes the options to display in a select form.
  /// </summary>
  public ref struct SelectOption {
  public:
    /// <summary>
    /// The name of the selection item.
    /// </summary>
    String^ Item;

    /// <summary>
    /// The path to the preview image of the item.
    /// </summary>
    String^ Preview;

    /// <summary>
    /// The description of the selection item.
    /// </summary>
    String^ Desc;

    /// <summary>
    /// A simple constructor that initializes the struct with the given values.
    /// </summary>
    /// <param name="item">The name of the selection item.</param>
    /// <param name="preview">The path to the preview image of the item.</param>
    /// <param name="desc">The description of the selection item.</param>
    SelectOption(String^ item, String^ preview, String^ desc) {
      Item = item;
      Preview = preview;
      Desc = desc;
    }
  };

  /// <summary>
  /// The base class for C# scripts.
  /// </summary>
  public ref class BaseScriptImpl {
  public:

    static String^ LastError;

    /// <summary>
    /// Returns the last error that occurred.
    /// </summary>
    /// <returns>The last error that occurred.</returns>
    static String^ GetLastError() {
        return LastError;
    }

    /// <summary>
    /// Performs a basic install of the mod.
    /// </summary>
    /// <remarks>
    /// A basic install installs all of the file in the mod to the Data directory
    /// or activates all esp and esm files.
    /// </remarks>
    /// <returns><c>true</c> if the installation succeed;
    /// <c>false</c> otherwise.</returns>
    static bool PerformBasicInstall();

    /// <summary>
    /// Installs the specified file from the mod to the specified location on the file system.
    /// </summary>
    /// <remarks>
    /// This is the legacy form of <see cref="InstallFileFromMod(string, string)"/>. It now just calls
    /// <see cref="InstallFileFromMod(string, string)"/>.
    /// </remarks>
    /// <param name="p_strFrom">The path of the file in the mod to install.</param>
    /// <param name="p_strTo">The path on the file system where the file is to be created.</param>
    /// <returns><c>true</c> if the file was written; <c>false</c> otherwise.</returns>
    /// <seealso cref="InstallFileFromMod(string, string)"/>
    static bool CopyDataFile(String^ p_strFrom, String^ p_strTo) {
      return InstallFileFromMod(p_strFrom, p_strTo);
    }

    /// <summary>
    /// Installs the specified file from the mod to the specified location on the file system.
    /// </summary>
    /// <param name="p_strFrom">The path of the file in the mod to install.</param>
    /// <param name="p_strTo">The path on the file system where the file is to be created.</param>
    /// <returns><c>true</c> if the file was written; <c>false</c> otherwise.</returns>
    static bool InstallFileFromMod(String^ p_strFrom, String^ p_strTo);

    /// <summary>
    /// Installs the speified file from the mod to the file system.
    /// </summary>
    /// <param name="p_strFile">The path of the file to install.</param>
    /// <returns><c>true</c> if the file was written; <c>false</c> otherwise.</returns>
    static bool InstallFileFromMod(String^ p_strFile) {
      return InstallFileFromMod(p_strFile, p_strFile);
    }

    /// <summary>
    /// Installs the specified file from the mod to the specified location on the file system.
    /// </summary>
    /// <param name="p_strFrom">The path of the file in the mod to install.</param>
    /// <param name="p_strTo">The path on the file system where the file is to be created.</param>
    /// <returns><c>true</c> if the file was written; <c>false</c> otherwise.</returns>
    static bool InstallFileFromFomod(String^ p_strFrom, String^ p_strTo) {
        return InstallFileFromMod(p_strFrom, p_strTo);
    }

    /// <summary>
    /// Installs the speified file from the mod to the file system.
    /// </summary>
    /// <param name="p_strFile">The path of the file to install.</param>
    /// <returns><c>true</c> if the file was written; <c>false</c> otherwise.</returns>
    static bool InstallFileFromFomod(String^ p_strFile) {
        return InstallFileFromMod(p_strFile, p_strFile);
    }

    /// <summary>
    /// Retrieves the list of files in the mod.
    /// </summary>
    /// <returns>The list of files in the mod.</returns>
    static array<String^>^ GetModFileList();

    /// <summary>
    /// Retrieves the list of files in the mod.
    /// </summary>
    /// <returns>The list of files in the mod.</returns>
    static array<String^>^ GetFomodFileList() {
        return GetModFileList();
    }

    /// <summary>
    /// Retrieves the specified file from the mod.
    /// </summary>
    /// <param name="p_strFile">The file to retrieve.</param>
    /// <returns>The requested file data.</returns>
    static array<Byte>^ GetFileFromMod(String^ p_strFile);

    /// <summary>
    /// Retrieves the specified file from the mod.
    /// </summary>
    /// <param name="p_strFile">The file to retrieve.</param>
    /// <returns>The requested file data.</returns>
    static array<Byte>^ GetFileFromFomod(String^ p_strFile) {
        return GetFileFromMod(p_strFile);
    }

    /// <summary>
    /// Gets a filtered list of all files in a user's Data directory.
    /// </summary>
    /// <param name="p_strPath">The subdirectory of the Data directory from which to get the listing.</param>
    /// <param name="p_strPattern">The pattern against which to filter the file paths.</param>
    /// <param name="p_booAllFolders">Whether or not to search through subdirectories.</param>
    /// <returns>A filtered list of all files in a user's Data directory.</returns>
    static array<String^>^ GetExistingDataFileList(String^ p_strPath, String^ p_strPattern, bool p_booAllFolders);

    /// <summary>
    /// Determines if the specified file exists in the user's Data directory.
    /// </summary>
    /// <param name="p_strPath">The path of the file whose existence is to be verified.</param>
    /// <returns><c>true</c> if the specified file exists; <c>false</c>
    /// otherwise.</returns>
    static bool DataFileExists(String^ p_strPath);

    /// <summary>
    /// Gets the speified file from the user's Data directory.
    /// </summary>
    /// <param name="p_strPath">The path of the file to retrieve.</param>
    /// <returns>The specified file, or <c>null</c> if the file does not exist.</returns>
    static array<Byte>^ GetExistingDataFile(String^ p_strPath);

    /// <summary>
    /// Writes the file represented by the given byte array to the given path.
    /// </summary>
    /// <remarks>
    /// This method writes the given data as a file at the given path. If the file
    /// already exists the user is prompted to overwrite the file.
    /// </remarks>
    /// <param name="p_strPath">The path where the file is to be created.</param>
    /// <param name="p_bteData">The data that is to make up the file.</param>
    /// <returns><c>true</c> if the file was written; <c>false</c> otherwise.</returns>
    static bool GenerateDataFile(String^ p_strPath, array<Byte>^ p_bteData);

    /// <summary>
    /// Shows a message box with the given message.
    /// </summary>
    /// <param name="p_strMessage">The message to display in the message box.</param>
    static void MessageBox(String^ p_strMessage) {
      MessageBox(p_strMessage, nullptr);
    }

    /// <summary>
    /// Shows a message box with the given message and title.
    /// </summary>
    /// <param name="p_strMessage">The message to display in the message box.</param>
    /// <param name="p_strTitle">The message box's title, display in the title bar.</param>
    static void MessageBox(String^ p_strMessage, String^ p_strTitle) {
      MessageBox(p_strMessage, p_strTitle, MessageBoxButtons::OK);
    }

    /// <summary>
    /// Shows a message box with the given message, title, and buttons.
    /// </summary>
    /// <param name="p_strMessage">The message to display in the message box.</param>
    /// <param name="p_strTitle">The message box's title, display in the title bar.</param>
    /// <param name="p_mbbButtons">The buttons to show in the message box.</param>
    static DialogResult MessageBox(String^ p_strMessage, String^ p_strTitle, MessageBoxButtons p_mbbButtons) {
      return MessageBox(p_strMessage, p_strTitle, p_mbbButtons, MessageBoxIcon::Information);
    }

    static DialogResult MessageBox(String^ p_strMessage, String^ p_strTitle, MessageBoxButtons p_mbbButtons, MessageBoxIcon p_mdiIcon) {
      return ExtendedMessageBox(p_strMessage, p_strTitle, nullptr, p_mbbButtons, p_mdiIcon);
    }

    /// <summary>
    /// Shows an extended message box with the given message, title, details, buttons, and icon.
    /// </summary>
    /// <param name="p_strMessage">The message to display in the message box.</param>
    /// <param name="p_strTitle">The message box's title, displayed in the title bar.</param>
    /// <param name="p_strDetails">The message box's details, displayed in the details area.</param>
    /// <param name="p_mbbButtons">The buttons to show in the message box.</param>
    /// <param name="p_mdiIcon">The icon to display in the message box.</param>
    static DialogResult ExtendedMessageBox(String^ p_strMessage, String^ p_strTitle, String^ p_strDetails, MessageBoxButtons p_mbbButtons, MessageBoxIcon p_mdiIcon);

    /// <summary>
    /// Displays a selection form to the user.
    /// </summary>
    /// <param name="p_sopOptions">The options from which to select.</param>
    /// <param name="p_strTitle">The title of the selection form.</param>
    /// <param name="p_booSelectMany">Whether more than one item can be selected.</param>
    /// <returns>The indices of the selected items.</returns>
    static array<int>^ Select(array<SelectOption^>^ p_sopOptions, String^ p_strTitle, bool p_booSelectMany);

    /// <summary>
    /// Displays a selection form to the user.
    /// </summary>
    /// <remarks>
    /// The items, previews, and descriptions are repectively ordered. In other words,
    /// the i-th item in <paramref name="p_strItems"/> uses the i-th preview in
    /// <paramref name="p_strPreviewPaths"/> and the i-th description in <paramref name="p_strDescriptions"/>.
    ///
    /// Similarly, the idices return as results correspond to the indices of the items in
    /// <paramref name="p_strItems"/>.
    /// </remarks>
    /// <param name="p_strItems">The items from which to select.</param>
    /// <param name="p_strPreviewPaths">The preview image file names for the items.</param>
    /// <param name="p_strDescriptions">The descriptions of the items.</param>
    /// <param name="p_strTitle">The title of the selection form.</param>
    /// <param name="p_booSelectMany">Whether more than one item can be selected.</param>
    /// <returns>The indices of the selected items.</returns>
    static array<int>^ Select(array<String^>^ p_strItems, array<String^>^ p_strPreviewPaths, array<String^>^ p_strDescriptions, String^ p_strTitle, bool p_booSelectMany) {
      const int size = p_strItems->GetLength(0);
      array<SelectOption^>^ options = gcnew array<SelectOption^>(size);
      for (int i = 0; i < size; ++i) {
        options[i] = gcnew SelectOption(p_strItems[i], p_strPreviewPaths[i], p_strDescriptions[i]);
      }
      return Select(options, p_strTitle, p_booSelectMany);
    }

    /// <summary>
    /// Displays a selection form to the user.
    /// </summary>
    /// <remarks>
    /// The items, previews, and descriptions are repectively ordered. In other words,
    /// the i-th item in <paramref name="p_strItems"/> uses the i-th preview in
    /// <paramref name="p_imgPreviews"/> and the i-th description in <paramref name="p_strDescriptions"/>.
    ///
    /// Similarly, the idices return as results correspond to the indices of the items in
    /// <paramref name="p_strItems"/>.
    /// </remarks>
    /// <param name="p_strItems">The items from which to select.</param>
    /// <param name="p_imgPreviews">The preview images for the items.</param>
    /// <param name="p_strDescriptions">The descriptions of the items.</param>
    /// <param name="p_strTitle">The title of the selection form.</param>
    /// <param name="p_booSelectMany">Whether more than one item can be selected.</param>
    /// <returns>The indices of the selected items.</returns>
    static array<int>^ ImageSelect(array<String^>^ p_strItems, array<Image^>^ /* p_imgPreviews */, array<String^>^ p_strDescriptions, String^ p_strTitle, bool p_booSelectMany) {
      return Select(p_strItems, gcnew array<String^>(p_strItems->Length), p_strDescriptions, p_strTitle, p_booSelectMany);
    }

    /// <summary>
    /// Creates a form that can be used in custom mod scripts.
    /// </summary>
    /// <returns>A form that can be used in custom mod scripts.</returns>
    static Form^ CreateCustomForm() {
      Form^ form = gcnew Form;
      return form;
    }

    /// <summary>
    /// Gets the version of the mod manager.
    /// </summary>
    /// <returns>The version of the mod manager.</returns>
    static System::Version^ GetModManagerVersion();

    /// <summary>
    /// Gets the version of the game that is installed.
    /// </summary>
    /// <returns>The version of the game, or <c>null</c> if Fallout
    /// is not installed.</returns>
    static System::Version^ GetGameVersion();

    // This is not in the spec., but I do not see a point in checking each
    // script extender in a different way.
    static System::Version^ GetScriptExtenderVersion();

    /// <summary>
    /// Gets the script extender version or null if it's not installed
    /// </summary>
    /// <returns></returns>
    static System::Version^ GetSkseVersion() {
      return GetScriptExtenderVersion();
    }

    /// <summary>
    /// Gets the script extender version or null if it's not installed
    /// </summary>
    /// <returns></returns>
    static System::Version^ GetFoseVersion() {
      return GetScriptExtenderVersion();
    }

    /// <summary>
    /// Gets the script extender version or null if it's not installed
    /// </summary>
    /// <returns></returns>
    static System::Version^ GetNvseVersion() {
      return GetScriptExtenderVersion();
    }

    /// <summary>
    /// Gets the version of the mod manager.
    /// </summary>
    /// <returns>The version of the mod manager.</returns>
    static System::Version^ GetFommVersion() {
      return GetModManagerVersion();
    }

    /// <summary>
    /// Gets the version of the game that is installed.
    /// </summary>
    /// <returns>The version of the game, or <c>null</c> if Fallout
    /// is not installed.</returns>
    static System::Version^ GetFalloutVersion() {
      // I think this is an old function... What Fallout anyway? So just going
      // to return the game version, whatever current game.
      return GetGameVersion();
    }

    /// <summary>
    /// Determins if the script extender for the game is installed
    /// (this checks for the script extender of the game for which the
    /// mod is being installed)
    /// </summary>
    /// <returns></returns>
    static bool ScriptExtenderPresent();

    /// <summary>
    /// Gets a list of all install plugins.
    /// </summary>
    /// <returns>A list of all install plugins.</returns>
    static array<String^>^ GetAllPlugins();

    /// <summary>
    /// Retrieves a list of currently active plugins.
    /// </summary>
    /// <returns>A list of currently active plugins.</returns>
    static array<String^>^ GetActivePlugins();

    /// <summary>
    /// Sets the activated status of a plugin (i.e., and esp or esm file).
    /// </summary>
    /// <param name="p_strPluginPath">The path to the plugin to activate or deactivate.</param>
    /// <param name="p_booActivate">Whether to activate the plugin.</param>
    static void SetPluginActivation(String^ /* p_strPluginPath */, bool /* p_booActivate */) {
      // throw gcnew NotImplementedException("SetPluginActivation");
    }

    /// <summary>
    /// Sets the load order of the specifid plugin.
    /// </summary>
    /// <param name="p_strPlugin">The path to the plugin file whose load order is to be set.</param>
    /// <param name="p_intNewIndex">The new load order index of the plugin.</param>
    static void SetPluginOrderIndex(String^ /* p_strPlugin */, int /* p_intNewIndex */) {
      // throw gcnew NotImplementedException("SetPluginOrderIndex");
    }

    /// <summary>
    /// Sets the load order of the plugins.
    /// </summary>
    /// <remarks>
    /// Each plugin will be moved from its current index to its indices' position
    /// in <paramref name="p_intPlugins"/>.
    /// </remarks>
    /// <param name="p_intPlugins">The new load order of the plugins. Each entry in this array
    /// contains the current index of a plugin. This array must contain all current indices.</param>
    static void SetLoadOrder(array<int>^ /* p_intPlugins */) {
      // throw gcnew NotImplementedException("SetLoadOrder");
    }

    /// <summary>
    /// Moves the specified plugins to the given position in the load order.
    /// </summary>
    /// <remarks>
    /// Note that the order of the given list of plugins is not maintained. They are re-ordered
    /// to be in the same order as they are in the before-operation load order. This, I think,
    /// is somewhat counter-intuitive and may change, though likely not so as to not break
    /// backwards compatibility.
    /// </remarks>
    /// <param name="p_intPlugins">The list of plugins to move to the given position in the
    /// load order. Each entry in this array contains the current index of a plugin.</param>
    /// <param name="p_intPosition">The position in the load order to which to move the specified
    /// plugins.</param>
    static void SetLoadOrder(array<int>^ /* p_intPlugins */, int /* p_intPosition */) {
      // throw gcnew NotImplementedException("SetLoadOrder");
    }

    /// <summary>
    /// Retrieves the specified settings value as a string.
    /// </summary>
    /// <param name="settingsFileName">The name of the settings file from which to retrieve the value.</param>
    /// <param name="section">The section containing the value to retrieve.</param>
    /// <param name="key">The key of the value to retrieve.</param>
    /// <returns>The specified value as a string.</returns>
    static String^ GetIniString(String^ settingsFileName, String^ section, String^ key);

    /// <summary>
    /// Retrieves the specified settings value as an integer.
    /// </summary>
    /// <param name="settingsFileName">The name of the settings file from which to retrieve the value.</param>
    /// <param name="section">The section containing the value to retrieve.</param>
    /// <param name="key">The key of the value to retrieve.</param>
    /// <returns>The specified value as an integer.</returns>
    static int GetIniInt(String^ settingsFileName, String^ section, String^ key);

    /// <summary>
    /// Retrieves the specified Fallout.ini value as a string.
    /// </summary>
    /// <param name="section">The section containing the value to retrieve.</param>
    /// <param name="key">The key of the value to retrieve.</param>
    /// <returns>The specified value as a string.</returns>
    /// <seealso cref="GetFalloutIniInt(string, string)"/>
    static String^ GetFalloutIniString(String^ section, String^ key) {
      return GetIniString("Fallout.ini", section, key);
    }

    /// <summary>
    /// Retrieves the specified Fallout.ini value as an integer.
    /// </summary>
    /// <param name="section">The section containing the value to retrieve.</param>
    /// <param name="key">The key of the value to retrieve.</param>
    /// <seealso cref="GetFalloutIniString(string, string)"/>
    static int GetFalloutIniInt(String^ section, String^ key) {
      return GetIniInt("Fallout.ini", section, key);
    }

    /// <summary>
    /// Retrieves the specified FalloutPrefs.ini value as a string.
    /// </summary>
    /// <param name="p_strSection">The section containing the value to retrieve.</param>
    /// <param name="p_strKey">The key of the value to retrieve.</param>
    /// <returns>The specified value as a string.</returns>
    /// <seealso cref="GetPrefsIniInt(string, string)"/>
    static String^ GetPrefsIniString(String^ p_strSection, String^ p_strKey) {
      // This looks wrong? Yes! But that's what used in other mod managers...
      return GetIniString("FalloutPrefs.ini", p_strSection, p_strKey);
    }

    /// <summary>
    /// Retrieves the specified FalloutPrefs.ini value as an integer.
    /// </summary>
    /// <param name="p_strSection">The section containing the value to retrieve.</param>
    /// <param name="p_strKey">The key of the value to retrieve.</param>
    /// <returns>The specified value as an integer.</returns>
    /// <seealso cref="GetPrefsIniString(string, string)"/>
    static int GetPrefsIniInt(String^ p_strSection, String^ p_strKey) {
      // This looks wrong? Yes! But that's what used in other mod managers...
      return GetIniInt("FalloutPrefs.ini", p_strSection, p_strKey);
    }

    /// <summary>
    /// Sets the specified value in the specified Ini file to the given value.
    /// </summary>
    /// <param name="p_strSettingsFileName">The name of the settings file to edit.</param>
    /// <param name="p_strSection">The section in the Ini file to edit.</param>
    /// <param name="p_strKey">The key in the Ini file to edit.</param>
    /// <param name="p_strValue">The value to which to set the key.</param>
    /// <returns><c>true</c> if the value was set; <c>false</c>
    /// if the user chose not to overwrite the existing value.</returns>
    static bool EditIni(String^ p_strSettingsFileName, String^ p_strSection, String^ p_strKey, String^ p_strValue);

    /// <summary>
    /// Sets the specified value in the Fallout.ini file to the given value.
    /// </summary>
    /// <param name="p_strSection">The section in the Ini file to edit.</param>
    /// <param name="p_strKey">The key in the Ini file to edit.</param>
    /// <param name="p_strValue">The value to which to set the key.</param>
    /// <param name="p_booSaveOld">Not used.</param>
    /// <returns><c>true</c> if the value was set; <c>false</c>
    /// if the user chose not to overwrite the existing value.</returns>
    static bool EditFalloutINI(String^ p_strSection, String^ p_strKey, String^ p_strValue, bool /* p_booSaveOld */) {
      return EditIni("Fallout.ini", p_strSection, p_strKey, p_strValue);
    }

  };

  /**
   * @brief Post-install script.
   */
  MOBase::IPluginInstaller::EInstallResult postInstall(std::shared_ptr<MOBase::IFileTree>& tree);
}

// BaseScript cannot be in a namespace:
public ref struct SelectOption: public CSharp::SelectOption {
  SelectOption(System::String^ item, System::String^ preview, System::String^ desc) :
    CSharp::SelectOption(item, preview, desc) { }
};
public ref class BaseScript: public CSharp::BaseScriptImpl { };

#endif
