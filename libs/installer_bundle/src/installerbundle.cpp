/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

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

#include "installerbundle.h"

#include <QtPlugin>

#include <uibase/iinstallationmanager.h>

#include "multiarchivedialog.h"

using namespace MOBase;

InstallerBundle::InstallerBundle() {}

bool InstallerBundle::init(IOrganizer* organizer)
{
  m_Organizer = organizer;
  return true;
}

QString InstallerBundle::name() const
{
  return "Bundle Installer";
}

QString InstallerBundle::localizedName() const
{
  return tr("Bundle Installer");
}

QString InstallerBundle::author() const
{
  return "Tannin";
}

QString InstallerBundle::description() const
{
  return tr("Proxy-Installer to handle bundles containing the actual mod archive");
}

VersionInfo InstallerBundle::version() const
{
  return VersionInfo(1, 1, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> InstallerBundle::settings() const
{
  return {PluginSetting("auto_reinstall",
                        "when reinstalling from an archive containing multiple mods, "
                        "automatically select the previously installed",
                        true)};
}

unsigned int InstallerBundle::priority() const
{
  return 10;
}

bool InstallerBundle::isManualInstaller() const
{
  return false;
}

void InstallerBundle::onInstallationStart(QString const& archive, bool reinstallation,
                                          IModInterface* currentMod)
{
  // We reset some field and fetch the previously installed file:
  m_InstallationFile = archive;
  m_InstallerUsed    = false;
  m_SelectedFile     = "";
  m_PreviousFile     = "";

  if (reinstallation && m_Organizer->pluginSetting(name(), "auto_reinstall").toBool()) {
    m_PreviousFile = currentMod->pluginSetting(name(), "archive", QString()).toString();
  }
}
void InstallerBundle::onInstallationEnd(EInstallResult result, IModInterface* newMod)
{
  if (result == EInstallResult::RESULT_SUCCESS && m_InstallerUsed) {
    newMod->setInstallationFile(m_InstallationFile);
    if (!m_SelectedFile.isEmpty()) {
      newMod->setPluginSetting(name(), "archive", m_SelectedFile);
    }
  }
}

std::vector<std::shared_ptr<const MOBase::FileTreeEntry>>
InstallerBundle::findObjects(std::shared_ptr<const IFileTree> tree) const
{
  std::vector<std::shared_ptr<const MOBase::FileTreeEntry>> entries;
  // Check if we have an archive:
  auto managerExtensions = manager()->getSupportedExtensions();

  // Look for a file with a valid extension:
  int nDirs = 0;
  for (auto entry : *tree) {
    if (entry->isFile()) {
      if (managerExtensions.contains(entry->suffix(),
                                     FileNameComparator::CaseSensitivity)) {
        entries.push_back(entry);
      }
    } else {
      nDirs++;
    }
  }

  if (!entries.empty()) {
    return entries;
  }

  // Arguably this should also check the name of the directory we're looking at
  // is the same as the module name:
  if (nDirs != 1) {
    return {};
  }

  // Retrieve the first folder which is the first item:
  return findObjects(tree->at(0)->astree());
}

bool InstallerBundle::isArchiveSupported(std::shared_ptr<const IFileTree> tree) const
{
  return !findObjects(tree).empty();
}

IPluginInstaller::EInstallResult
InstallerBundle::install(GuessedValue<QString>& modName,
                         std::shared_ptr<IFileTree>& tree, QString&, int& modId)
{
  // Find a valid "object":
  auto entries = findObjects(tree);
  if (entries.empty()) {
    return IPluginInstaller::RESULT_NOTATTEMPTED;
  }

  std::shared_ptr<const FileTreeEntry> entry = nullptr;
  if (entries.size() == 1) {
    entry = entries[0];
  } else {
    if (!m_PreviousFile.isEmpty()) {
      entry = tree->find(m_PreviousFile);
    }

    if (entry == nullptr) {
      MultiArchiveDialog dialog(entries, parentWidget());
      if (dialog.exec() == QDialog::Accepted) {
        entry          = dialog.selectedEntry();
        m_SelectedFile = entry->pathFrom(tree);
      } else {
        if (dialog.manualRequested()) {
          return IPluginInstaller::RESULT_MANUALREQUESTED;
        } else {
          return IPluginInstaller::RESULT_CANCELED;
        }
      }
    }
  }

  if (entry == nullptr) {
    return IPluginInstaller::RESULT_CANCELED;
  }

  m_InstallerUsed = true;

  // Extract it:
  QString tempFile = manager()->extractFile(entry);
  IPluginInstaller::EInstallResult res =
      manager()->installArchive(modName, tempFile, modId);
  if (res == IPluginInstaller::RESULT_SUCCESS) {
    res = IPluginInstaller::RESULT_SUCCESSCANCEL;
  }
  return res;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(installerBundle, InstallerBundle)
#endif
