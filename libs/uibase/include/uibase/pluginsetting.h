/*
Mod Organizer shared UI functionality

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

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

#ifndef PLUGINSETTING_H
#define PLUGINSETTING_H

#include <QList>
#include <QString>
#include <QVariant>

namespace MOBase
{

/**
 * @brief struct to hold the user-configurable parameters a plugin accepts. The purpose
 * of this struct is only to inform the application what settings to offer to the user,
 * it does not hold the actual value
 */
struct PluginSetting
{
  PluginSetting(const QString& key, const QString& description,
                const QVariant& defaultValue)
      : key(key), description(description), defaultValue(defaultValue)
  {}

  QString key;
  QString description;
  QVariant defaultValue;
};

}  // namespace MOBase

#endif  // PLUGINSETTING_H
