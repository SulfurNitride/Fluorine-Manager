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

#ifndef INSTALLERBUNDLE_H
#define INSTALLERBUNDLE_H

#include <uibase/iplugininstallersimple.h>

class InstallerBundle : public MOBase::IPluginInstallerSimple
{

  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerSimple)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "org.tannin.InstallerBundle")
#endif

public:
  InstallerBundle();

  virtual bool init(MOBase::IOrganizer* moInfo) override;
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

  virtual unsigned int priority() const override;
  virtual bool isManualInstaller() const override;

  virtual void onInstallationStart(QString const& archive, bool reinstallation,
                                   MOBase::IModInterface* currentMod) override;
  virtual void onInstallationEnd(EInstallResult result,
                                 MOBase::IModInterface* newMod) override;

  virtual bool
  isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const override;
  virtual EInstallResult install(MOBase::GuessedValue<QString>& modName,
                                 std::shared_ptr<MOBase::IFileTree>& tree,
                                 QString& version, int& modID) override;

private:
  /**
   * @brief Find the entries that can be extracted from this archive.
   *
   * @param tree The tree to look the entry in.
   *
   * @return the entry, if one was found, or a null pointer.
   */
  std::vector<std::shared_ptr<const MOBase::FileTreeEntry>>
  findObjects(std::shared_ptr<const MOBase::IFileTree> tree) const;

private:
  MOBase::IOrganizer* m_Organizer;

  // The archive being installed:
  QString m_InstallationFile;

  // Indicates if this installer was used during the installation process:
  bool m_InstallerUsed;

  // Contains the file installed (when multiple archives are present), or an
  // empty string:
  QString m_SelectedFile;

  // Contains the file previously installed (when multiple archives are present), or an
  // empty string:
  QString m_PreviousFile;
};

#endif  // INSTALLERBUNDLE_H
