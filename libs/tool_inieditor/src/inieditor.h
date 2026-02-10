/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of ini editor plugin for MO

Ini editor plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Ini editor plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Ini editor plugin.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef INIEDITOR_H
#define INIEDITOR_H

#include <uibase/iplugintool.h>

class IniEditor : public MOBase::IPluginTool
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginTool)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "org.tannin.IniEditor")
#endif

public:
  IniEditor();

  virtual bool init(MOBase::IOrganizer* moInfo) override;
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

  virtual QString displayName() const;
  virtual QString tooltip() const;
  virtual QIcon icon() const;

public slots:
  virtual void display() const;

private:
  const MOBase::IOrganizer* m_MOInfo;
};

#endif  // INIEDITOR_H
