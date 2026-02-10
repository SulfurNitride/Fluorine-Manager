/*
Mod Organizer ini bakery plugin

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <uibase/iplugin.h>
#include <uibase/ipluginfilemapper.h>

/**
 * @brief The IniBakery class
 */
class IniBakery : public QObject,
                  public MOBase::IPlugin,
                  public MOBase::IPluginFileMapper
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginFileMapper)
  Q_PLUGIN_METADATA(IID "org.tannin.IniBakey")

public:
  virtual bool init(MOBase::IOrganizer* moInfo);
  virtual QString name() const;
  virtual QString localizedName() const;
  virtual QString author() const;
  virtual QString description() const;
  virtual MOBase::VersionInfo version() const;
  virtual QList<MOBase::PluginSetting> settings() const;

  virtual MappingType mappings() const;

private:
  QStringList iniFileNames() const;
  bool prepareIni(const QString& executable);

private:
  MOBase::IOrganizer* m_MOInfo;
};
