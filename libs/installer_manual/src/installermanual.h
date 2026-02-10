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

#ifndef INSTALLERMANUAL_H
#define INSTALLERMANUAL_H

#include <uibase/imoinfo.h>
#include <uibase/iplugininstallersimple.h>

class InstallerManual : public MOBase::IPluginInstallerSimple
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerSimple)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "org.tannin.InstallerManual")
#endif

public:
  InstallerManual();

  virtual bool init(MOBase::IOrganizer* moInfo) override;
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

  virtual unsigned int priority() const;
  virtual bool isManualInstaller() const;

  virtual bool isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const;
  virtual EInstallResult install(MOBase::GuessedValue<QString>& modName,
                                 std::shared_ptr<MOBase::IFileTree>& tree,
                                 QString& version, int& modID);

private:
  bool
  isSimpleArchiveTopLayer(const std::shared_ptr<const MOBase::IFileTree> tree) const;
  std::shared_ptr<const MOBase::IFileTree>
  getSimpleArchiveBase(const std::shared_ptr<const MOBase::IFileTree> tree) const;

private slots:

  /**
   * @brief Opens a file from the archive in the (system-)default editor/viewer.
   *
   * @param entry Entry corresponding to the file to open.
   */
  void openFile(const MOBase::FileTreeEntry* entry);

private:
  const MOBase::IOrganizer* m_MOInfo;
};

#endif  // INSTALLERMANUAL_H
