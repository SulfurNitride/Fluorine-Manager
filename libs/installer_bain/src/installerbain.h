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

#ifndef INSTALLERBAIN_H
#define INSTALLERBAIN_H

#include <uibase/iplugininstallersimple.h>

class InstallerBAIN : public MOBase::IPluginInstallerSimple
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerSimple)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "org.tannin.InstallerBAIN")
#endif

public:
  InstallerBAIN();

  virtual bool init(MOBase::IOrganizer* moInfo) override;
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

  virtual unsigned int priority() const;
  virtual bool isManualInstaller() const;

  virtual void onInstallationStart(QString const& archive, bool reinstallation,
                                   MOBase::IModInterface* currentMod) override;
  virtual void onInstallationEnd(EInstallResult result,
                                 MOBase::IModInterface* newMod) override;

  virtual bool isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const;
  virtual EInstallResult install(MOBase::GuessedValue<QString>& modName,
                                 std::shared_ptr<MOBase::IFileTree>& tree,
                                 QString& version, int& modID);

protected:
  /**
   * @brief Retrieve the entries corresponding to subpackages in the given tree.
   *
   * @param tree The tree to check for subpackages.
   * @param invalidFolders If not null, will contain the number of invalid folders.
   *
   * @return the list of entries corresponding to subpackages.
   */
  std::vector<std::shared_ptr<const MOBase::FileTreeEntry>>
  findSubpackages(std::shared_ptr<const MOBase::IFileTree> tree,
                  std::size_t* invalidFolders = nullptr) const;

private:
  const MOBase::IOrganizer* m_MOInfo;

  // Indicates if the installer was used:
  bool m_InstallerUsed;

  // List of options currently installed and being installed:
  QStringList m_PreviousOptions, m_SelectedOptions;
};

#endif  // INSTALLERBAIN_H
