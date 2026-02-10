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

#ifndef INSTALLER_FOMOD_CSHARP_H
#define INSTALLER_FOMOD_CSHARP_H

#include <uibase/iplugininstallersimple.h>

class InstallerFomodCSharp : public MOBase::IPluginInstallerSimple
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerSimple)
  Q_PLUGIN_METADATA(IID "org.holt59.InstallerFomodCSharp")

public:
  InstallerFomodCSharp() {}

  virtual bool init(MOBase::IOrganizer* moInfo) override;

  virtual QString name() const override { return "Fomod Installer C#"; }

  virtual QString localizedName() const override { return tr("Fomod Installer C#"); }

  virtual QString author() const override { return "Holt59"; }

  virtual QString description() const override
  {
    return tr("Installer for C# based FOMOD archives.");
  }

  virtual MOBase::VersionInfo version() const override
  {
    return MOBase::VersionInfo(1, 0, 0, MOBase::VersionInfo::RELEASE_BETA);
  }

  virtual QList<MOBase::PluginSetting> settings() const override
  {
    return {
        MOBase::PluginSetting("enabled", "check to enable this plugin", QVariant(true)),
        MOBase::PluginSetting("prefer", "prefer this over the NCC based plugin",
                              QVariant(true))};
  }

  virtual unsigned int priority() const override
  {
    // It's the same priority as the FOMOD installer but those should never conflict:
    return m_MOInfo->pluginSetting(name(), "prefer").toBool() ? 110 : 90;
  }

  virtual bool isManualInstaller() const override { return false; }

  virtual bool
  isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const override;

  virtual EInstallResult install(MOBase::GuessedValue<QString>& modName,
                                 std::shared_ptr<MOBase::IFileTree>& tree,
                                 QString& version, int& modID) override;

private:
  MOBase::IOrganizer* m_MOInfo;

  std::shared_ptr<const MOBase::IFileTree>
  findFomodDirectory(std::shared_ptr<const MOBase::IFileTree> tree) const;
  std::shared_ptr<const MOBase::FileTreeEntry>
  findScriptFile(std::shared_ptr<const MOBase::IFileTree> tree) const;
  std::shared_ptr<const MOBase::FileTreeEntry>
  findInfoFile(std::shared_ptr<const MOBase::IFileTree> tree) const;
};

#endif
