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

#include "base_script.h"

#include <filesystem>
#include <map>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QVersionNumber>

#include <uibase/game_features/igamefeatures.h>
#include <uibase/game_features/scriptextender.h>
#include <uibase/iinstallationmanager.h>
#include <uibase/imoinfo.h>
#include <uibase/iplugingame.h>
#include <uibase/ipluginlist.h>
#include <uibase/log.h>

#include "csharp_interface.h"
#include "csharp_utils.h"
#include "installer_fomod_postdialog.h"
#include "psettings.h"

using namespace MOBase;

// clang-format off

namespace CSharp {

  // Pointer to object:
  static MOBase::IOrganizer* g_Organizer = nullptr;

  // Per-install globals:
  struct Globals {
    IInstallationManager* InstallManager;
    QWidget* ParentWidget;
    std::shared_ptr<const IFileTree> SourceTree;
    std::shared_ptr<IFileTree> DestinationTree;

    // Map from path in destination entry to the original entry:
    std::map<std::shared_ptr<const FileTreeEntry>,
      std::shared_ptr<const FileTreeEntry>> InstalledEntries;

    // Map extracted entries (in the original tree) to (temporary) paths:
    std::map<std::shared_ptr<const FileTreeEntry>, QString> ExtractedEntries;

    // Map creted entries (in the destination tree) to (temporary) paths:
    std::map<std::shared_ptr<const FileTreeEntry>, QString> CreatedEntries;

    // List of modified settings values:
    std::map<QString, PSettings> Settings;

    Globals() { }
    Globals(
      IPlugin const* plugin, MOBase::IInstallationManager* manager, QWidget* parentWidget,
      std::shared_ptr<MOBase::IFileTree> tree, std::map<std::shared_ptr<const FileTreeEntry>, QString> entries) :
      m_Plugin(plugin), InstallManager(manager), ParentWidget(parentWidget), SourceTree(tree), DestinationTree(tree->createOrphanTree()), ExtractedEntries(std::move(entries)) {

    }

    Globals(Globals const&) = delete;
    Globals(Globals&&) = default;

    Globals& operator=(Globals const&) = delete;
    Globals& operator=(Globals&&) = default;

  private:
    IPlugin const* m_Plugin;
  };
  static Globals g;



  void init(MOBase::IOrganizer* moInfo) {

    // Do this only once:
    if (g_Organizer == nullptr) {
      Application::EnableVisualStyles();
      Application::SetCompatibleTextRenderingDefault(false);
    }

    g_Organizer = moInfo;
  }

  void beforeInstall(IPlugin const* plugin, MOBase::IInstallationManager* manager, QWidget* parentWidget,
    std::shared_ptr<MOBase::IFileTree> tree, std::map<std::shared_ptr<const FileTreeEntry>, QString> entries) {
    g = { plugin, manager, parentWidget, tree, std::move(entries) };
  }

  IPluginInstaller::EInstallResult postInstall(std::shared_ptr<MOBase::IFileTree>& tree) {

    if (!g.Settings.empty()) {

      InstallerFomodPostDialog* dialog = new InstallerFomodPostDialog(g.ParentWidget);

      dialog->setIniSettings(g.Settings);

      // Installation cancelled:
      if (dialog->exec() == QDialog::Rejected) {
        return IPluginInstaller::EInstallResult::RESULT_CANCELED;
      }

      switch (dialog->result()) {

        // Discard, nothing do to:
      case InstallerFomodPostDialog::Result::DISCARD: break;

        // Apply, must fetch the profile INI settings and apply the settings:
      case InstallerFomodPostDialog::Result::APPLY: {
        for (auto& p : g.Settings) {
          QDir path(g_Organizer->profilePath());
          if (!g_Organizer->profile()->localSettingsEnabled()) {
            path = QDir(g_Organizer->managedGame()->documentsDirectory());
          }

          QSettings settings(path.filePath(p.first), QSettings::IniFormat);

          if (settings.status() != QSettings::NoError) {
            return IPluginInstaller::EInstallResult::RESULT_FAILED;
          }

          p.second.update(settings);
        }
      } break;

                                                  // Move, must create the INI files and apply the settings:
      case InstallerFomodPostDialog::Result::MOVE: {
        for (auto& p : g.Settings) {
          auto e = g.DestinationTree->addFile("INI Tweaks/" + p.first, false);
          if (e == nullptr) {
            return IPluginInstaller::EInstallResult::RESULT_FAILED;
          }
          QString path = g.InstallManager->createFile(e);
          if (path.isEmpty()) {
            return IPluginInstaller::EInstallResult::RESULT_FAILED;
          }
          QSettings settings(path, QSettings::IniFormat);
          if (settings.status() != QSettings::NoError) {
            return IPluginInstaller::EInstallResult::RESULT_FAILED;
          }
          p.second.update(settings);
        }
      } break;
      }

    }

    tree = g.DestinationTree;

    // Clear up:
    g = Globals();


    return IPluginInstaller::EInstallResult::RESULT_SUCCESS;
  }

}

namespace CSharp {

  using namespace System::IO;

  bool isFomodEntry(std::shared_ptr<const FileTreeEntry> entry) {
    return entry->pathFrom(g.SourceTree).compare("fomod", Qt::CaseInsensitive) == 0;
  }

  bool BaseScriptImpl::PerformBasicInstall() {
    for (auto e : *g.SourceTree) {
      if (!isFomodEntry(e)) {
        auto ce = g.DestinationTree->copy(e, "", IFileTree::InsertPolicy::MERGE);
        g.InstalledEntries[ce] = e;
      }
    }
    return true;
  }

  bool BaseScriptImpl::InstallFileFromMod(String^ p_strFrom, String^ p_strTo) {
    auto sourceEntry = g.SourceTree->find(to_qstring(p_strFrom));

    if (!sourceEntry) {
      log::warn("File '{}' not found in the archive.", to_wstring(p_strFrom));
      return false;
    }

    if (auto ce = g.DestinationTree->copy(sourceEntry, to_qstring(p_strTo)); ce != nullptr) {
      g.InstalledEntries[ce] = sourceEntry;
      return true;
    }

    return false;
  }

  array<String^>^ BaseScriptImpl::GetModFileList() {
    // Cannot directly fill a, e.g., List<String^>^ because I cannot capture it:
    std::vector<QString> paths;
    g.SourceTree->walk([&](QString const& path, std::shared_ptr<const FileTreeEntry> entry) {
      // Discard fomod folder:
      if (isFomodEntry(entry)) {
        return IFileTree::WalkReturn::SKIP;
      }
      if (entry->isFile()) {
        paths.push_back(path + entry->name());
      }
      return IFileTree::WalkReturn::CONTINUE;
      }, "/");

    // Convert to C#:
    const auto size = static_cast<int>(paths.size());
    array<String^>^ result = gcnew array<String^>(size);
    for (int i = 0; i < size; ++i) {
      result[i] = from_string(paths[i].toStdWString());
    }

    return result;
  }


  /**
   * @brief Extract the given entry.
   *
   * If the entry has already been extracted, the existing paths is returned.
   *
   * @param entry The entry to extract.
   *
   * @return path to the temporary file corresponding to the entry.
   */
  QString extractFile(std::shared_ptr<const FileTreeEntry> entry) {

    QString qPath;
    if (auto it = g.ExtractedEntries.find(entry); it != g.ExtractedEntries.end()) {
      qPath = it->second;
    }
    else {
      qPath = g.InstallManager->extractFile(entry, true);

      if (qPath.isEmpty()) {
        return QString();
      }

      g.ExtractedEntries[entry] = qPath;
    }

    return qPath;
  }

  array<Byte>^ BaseScriptImpl::GetFileFromMod(String^ p_strFile) {
    auto entry = g.SourceTree->find(to_qstring(p_strFile));

    if (!entry) {
      return gcnew array<Byte>(0);
    }

    QString qPath = extractFile(entry);
    if (qPath.isEmpty()) {
      return gcnew array<Byte>(0);
    }

    String^ path = from_string(qPath.toStdWString());
    return File::ReadAllBytes(path);
  }

  QStringList getDataFiles(QString folder, QString pattern, bool allFolders) {
    QStringList files = g_Organizer->findFiles(folder, [pattern](QString const& filepath) {
      return QDir::match(pattern, QFileInfo(filepath).fileName());
      });
    if (allFolders) {
      QStringList directories = g_Organizer->listDirectories(folder);
      for (QString directory : directories) {
        // MO2 does not like path with . or / (I think), so creating the path manually:
        files.append(getDataFiles((folder.isEmpty() ? "" : folder + QDir::separator()) + directory, pattern, allFolders));
      }
    }
    return files;
  }

  array<String^>^ BaseScriptImpl::GetExistingDataFileList(String^ p_strPath, String^ p_strPattern, bool p_booAllFolders) {
    QStringList files = getDataFiles(to_qstring(p_strPath), to_qstring(p_strPattern), p_booAllFolders);
    array<String^>^ result = gcnew array<String^>(files.size());

    QDir modsDir(g_Organizer->modsPath());
    for (int i = 0; i < files.size(); ++i) {
      // We need to trim the path to the actual folder containing the mod (did not find a better way):
      QString rpath = modsDir.relativeFilePath(files[i]).replace("/", QDir::separator());
      result[i] = from_string(rpath.mid(rpath.indexOf(QDir::separator()) + 1).toStdWString());
    }
    return result;
  }

  /**
   * @brief Find the data-file path corresponding to the given path.
   *
   * This methods first lookup the file in the destination tree of the mod (for files
   * that have been extracted / created by the mod), and if it does not find it there
   * lookup files from other mods.
   *
   * @param p_strPath Path to the file to lookup, relative to the data folder.
   *
   * @return a path to an actual corresponding file, or a null pointer if the
   *   file was not found.
   */
  String^ getDataFilePath(String^ p_strPath) {

    // Check if the file is in the output tree:
    QString qPath = to_qstring(p_strPath);
    if (auto e = g.DestinationTree->find(qPath); e != nullptr) {

      // Check if it's a created entry:
      if (auto it = g.CreatedEntries.find(e); it != g.CreatedEntries.end()) {
        return from_string(g.CreatedEntries[e]);
      }

      // Find the source entry - We need to check for parent:
      std::shared_ptr<const FileTreeEntry> originalEntry;
      if (auto it = g.InstalledEntries.find(e); it != g.InstalledEntries.end()) {
        originalEntry = it->second;
      }
      else {
        std::shared_ptr<const IFileTree> tree = e->parent();
        decltype(tree) oTree = nullptr;
        while (oTree == nullptr && tree != nullptr) {
          auto it = g.InstalledEntries.find(tree);
          if (it != g.InstalledEntries.end()) {
            oTree = it->second->astree();
          }
          else {
            tree = tree->parent();
          }
        }
        originalEntry = oTree->find(e->pathFrom(tree));
      }
      QString path = extractFile(originalEntry);
      if (path.isEmpty()) {
        return nullptr;
      }
      return from_string(path);
    }

    // Convert to path and normalize separator:
    auto path = std::filesystem::path(qPath.toStdWString()).make_preferred();
    QStringList paths = g_Organizer->findFiles(ToQString(path.parent_path().native()), [name = ToQString(path.filename())](QString const& filepath) {
      return QFileInfo(filepath).fileName().compare(name, Qt::CaseInsensitive) == 0;
      });

    if (paths.isEmpty()) {
      return nullptr;
    }

    return from_string(paths[0]);
  }

  bool BaseScriptImpl::DataFileExists(String^ p_strPath) {
    return getDataFilePath(p_strPath) != nullptr;
  }

  array<Byte>^ BaseScriptImpl::GetExistingDataFile(String^ p_strPath) {

    // Convert to path and normalize separator:
    String^ datapath = getDataFilePath(p_strPath);

    if (datapath == nullptr) {
      return nullptr;
    }

    // Read the first file (should be only one):
    return File::ReadAllBytes(datapath);
  }

  bool BaseScriptImpl::GenerateDataFile(String^ p_strPath, array<Byte>^ p_bteData) {

    // Check if we already have created an entry for this:
    QString qPath = to_qstring(p_strPath);
    auto entry = g.DestinationTree->find(qPath);

    QString qAbsPath;
    // If the entry is in the list of created files (note: if the entry does not exist,
    // find return a nullptr, which is never in g.CreatedEntries).
    if (auto it = g.CreatedEntries.find(entry); it != g.CreatedEntries.end()) {
      qAbsPath = g.CreatedEntries.at(entry);
    }
    // Otherwize: Create the entry and the temporary file:
    else {
      entry = g.DestinationTree->addFile(qPath, true);
      qAbsPath = g.InstallManager->createFile(entry);
      if (qAbsPath.isEmpty()) {
        // Remove the entry from the tree:
        entry->detach();
        return false;
      }

      // Store the created entry:
      g.CreatedEntries[entry] = qAbsPath;
    }

    String^ absPath = from_string(qAbsPath);
    File::WriteAllBytes(absPath, p_bteData);
    return true;
  }

  // UI methods:

  DialogResult BaseScriptImpl::ExtendedMessageBox(String^ p_strMessage, String^ p_strTitle, String^ p_strDetails, MessageBoxButtons p_mbbButtons, MessageBoxIcon p_mdiIcon) {
    QMessageBox messageBox(g.ParentWidget);
    if (!String::IsNullOrEmpty(p_strTitle)) {
      messageBox.setWindowTitle(to_qstring(p_strTitle));
    }
    messageBox.setText(to_qstring(p_strMessage));

    if (!String::IsNullOrEmpty(p_strDetails)) {
      messageBox.setDetailedText(to_qstring(p_strDetails));
    }

    // For whatever reason MessageBoxIcon has duplicated entries...
    switch (p_mdiIcon) {
    case MessageBoxIcon::Error:
      // case MessageBoxIcon::Stop:
      messageBox.setIcon(QMessageBox::Icon::Critical);
      break;
    case MessageBoxIcon::Asterisk:
      // case MessageBoxIcon::Information:
      messageBox.setIcon(QMessageBox::Icon::Information);
      break;
    case MessageBoxIcon::Question:
      messageBox.setIcon(QMessageBox::Icon::Question);
      break;
    case MessageBoxIcon::Exclamation:
      // case MessageBoxIcon::Hand:
      // case MessageBoxIcon::Warning:
      // case MessageBoxIcon::None:
      messageBox.setIcon(QMessageBox::Icon::Warning);
    case MessageBoxIcon::None:
      messageBox.setIcon(QMessageBox::Icon::NoIcon);
      break;
    }

    QMessageBox::StandardButtons buttons;

    switch (p_mbbButtons) {
    case MessageBoxButtons::AbortRetryIgnore:
      buttons = QMessageBox::StandardButton::Abort | QMessageBox::StandardButton::Retry | QMessageBox::StandardButton::Ignore;
      break;
    case MessageBoxButtons::OK:
      buttons = QMessageBox::StandardButton::Ok;
      break;
    case MessageBoxButtons::OKCancel:
      buttons = QMessageBox::StandardButton::Ok | QMessageBox::Cancel;
      break;
    case MessageBoxButtons::RetryCancel:
      buttons = QMessageBox::StandardButton::Cancel | QMessageBox::StandardButton::Retry;
      break;
    case MessageBoxButtons::YesNoCancel:
      buttons = QMessageBox::StandardButton::Yes | QMessageBox::No | QMessageBox::Cancel;
      break;
    case MessageBoxButtons::YesNo:
      buttons = QMessageBox::StandardButton::Yes | QMessageBox::No;
      break;
    }

    messageBox.setStandardButtons(buttons);

    // Only some case are possible here:
    switch (messageBox.exec()) {
    case QMessageBox::Button::Abort:
      return DialogResult::Abort;
    case QMessageBox::Button::Cancel:
      return DialogResult::Cancel;
    case QMessageBox::Button::Ignore:
      return DialogResult::Ignore;
    case QMessageBox::Button::No:
      return DialogResult::No;
    case QMessageBox::Button::Ok:
      return DialogResult::OK;
    case QMessageBox::Button::Retry:
      return DialogResult::Retry;
    case QMessageBox::Button::Yes:
      return DialogResult::Yes;
    }

    return DialogResult::None;
  }


  array<int>^ BaseScriptImpl::Select(array<SelectOption^>^ p_sopOptions, String^ p_strTitle, bool p_booSelectMany) {
    using namespace System::Collections::Generic;

    QDialog* inputDialog = new QDialog();
    QVBoxLayout* layout = new QVBoxLayout(inputDialog);
    inputDialog->setWindowTitle(to_qstring(p_strTitle));
    inputDialog->setLayout(layout);

    layout->setSizeConstraint(QLayout::SetFixedSize);

    if (p_booSelectMany) {
      layout->addWidget(new QLabel(QObject::tr("Choose any:"), inputDialog));
    }
    else {
      layout->addWidget(new QLabel(QObject::tr("Choose one:"), inputDialog));
    }

    QList<QAbstractButton*> items;
    for each (SelectOption ^ opt in p_sopOptions) {
      QAbstractButton* btn;
      if (p_booSelectMany) {
        btn = new QCheckBox(to_qstring(opt->Item), inputDialog);
      }
      else {
        btn = new QRadioButton(to_qstring(opt->Item), inputDialog);
      }

      if (!String::IsNullOrEmpty(opt->Desc)) {
        btn->setToolTip(to_qstring(opt->Desc));
      }

      layout->addWidget(btn);
      items.append(btn);
    }

    if (!p_booSelectMany && items.size() > 0) {
      items[0]->setChecked(true);
    }

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
      QDialogButtonBox::Cancel | QDialogButtonBox::Ok, inputDialog);
    layout->addWidget(buttonBox);

    // Using old signal/slot syntax since the new one does not work here (probably
    // due to the C++/CLR nature):
    QObject::connect(buttonBox, SIGNAL(accepted()), inputDialog, SLOT(accept()));
    QObject::connect(buttonBox, SIGNAL(rejected()), inputDialog, SLOT(reject()));

    inputDialog->setModal(true);
    if (inputDialog->exec() != QDialog::Accepted) {
      return gcnew array<int>(0);
    }

    List<int>^ selected = gcnew List<int>(items.size());
    for (int i = 0; i < items.size(); ++i) {
      if (items[i]->isChecked()) {
        selected->Add(i);
      }
    }

    return selected->ToArray();
  }

  // Versioning / INIs:

  // Convert to Version^ from a MO2 version:
  inline System::Version^ make_version(VersionInfo version) {
    auto qversion = version.asQVersionNumber();
    return gcnew System::Version(qversion.majorVersion(), qversion.minorVersion(), qversion.microVersion());

  }

  System::Version^ BaseScriptImpl::GetModManagerVersion() {
    const auto version = g_Organizer->version();
    return gcnew System::Version(version.major(), version.minor(), version.patch());
  }

  System::Version^ BaseScriptImpl::GetGameVersion() {
    return make_version(g_Organizer->managedGame()->gameVersion());
  }

  System::Version^ BaseScriptImpl::GetScriptExtenderVersion() {
    auto scriptExtender = g_Organizer->gameFeatures()->gameFeature<ScriptExtender>();

    if (!scriptExtender || !scriptExtender->isInstalled()) {
      return nullptr;
    }

    return gcnew System::Version(msclr::interop::marshal_as<String^>(scriptExtender->getExtenderVersion().toStdString()));
  }

  bool BaseScriptImpl::ScriptExtenderPresent() {
    auto scriptExtender = g_Organizer->gameFeatures()->gameFeature<ScriptExtender>();
    return scriptExtender && scriptExtender->isInstalled();
  }

  // Plugins:
  array<String^>^ BaseScriptImpl::GetAllPlugins() {
    QStringList names = g_Organizer->pluginList()->pluginNames();
    array<String^>^ result = gcnew array<String^>(names.size());
    for (int i = 0; i < names.size(); ++i) {
      result[i] = from_string(names[i].toStdString());
    }
    return result;
  }

  array<String^>^ BaseScriptImpl::GetActivePlugins() {
    auto pluginList = g_Organizer->pluginList();
    QStringList names = pluginList->pluginNames();
    QStringList activeNames;
    for (auto& name : names) {
      if (pluginList->state(name) == IPluginList::STATE_ACTIVE) {
        activeNames.append(name);
      }
    }
    array<String^>^ result = gcnew array<String^>(activeNames.size());
    for (int i = 0; i < activeNames.size(); ++i) {
      result[i] = from_string(activeNames[i].toStdString());
    }
    return result;
  }

  // INIs:
  String^ BaseScriptImpl::GetIniString(String^ settingsFileName, String^ section, String^ key) {

    // Check if we have already set this within this installation:
    auto fIt = g.Settings.find(to_qstring(settingsFileName));
    if (fIt != g.Settings.end()) {
      QString value = fIt->second.value(to_qstring(section), to_qstring(key));
      if (!value.isEmpty()) {
        return from_string(value);
      }
    }

    // Otherwize, look-up the file:
    QDir path(g_Organizer->profilePath());
    if (!g_Organizer->profile()->localSettingsEnabled()) {
      path = QDir(g_Organizer->managedGame()->documentsDirectory());
    }

    QSettings settings(path.filePath(to_qstring(settingsFileName)), QSettings::IniFormat);

    if (settings.status() != QSettings::NoError) {
      return nullptr;
    }

    QString name = to_qstring(section + "/" + key);
    if (section->Equals("General", System::StringComparison::CurrentCultureIgnoreCase)) {
      name = to_qstring(key);
    }

    QVariant value = settings.value(name);
    if (!value.isValid()) {
      return nullptr;
    }

    return from_string(value.toString().toStdString());
  }

  int BaseScriptImpl::GetIniInt(String^ settingsFileName, String^ section, String^ key) {
    return Convert::ToInt32(GetIniString(settingsFileName, section, key));
  }

  bool BaseScriptImpl::EditIni(String^ p_strSettingsFileName, String^ p_strSection, String^ p_strKey, String^ p_strValue) {
    // Check that the file is supported:
    bool iniFound = false;
    for (auto ini : g_Organizer->managedGame()->iniFiles()) {
      if (ini.compare(to_qstring(p_strSettingsFileName), Qt::CaseInsensitive) == 0) {
        iniFound = true;
      }
    }

    if (!iniFound) {
      return false;
    }

    g.Settings[to_qstring(p_strSettingsFileName)].setValue(to_qstring(p_strSection), to_qstring(p_strKey), to_qstring(p_strValue));
    return true;
  }

}
