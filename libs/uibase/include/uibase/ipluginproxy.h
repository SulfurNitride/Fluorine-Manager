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

#ifndef IPLUGINPROXY_H
#define IPLUGINPROXY_H

#include <QDir>
#include <QList>
#include <QString>

#include "iplugin.h"

namespace MOBase
{

class IPluginProxy : public IPlugin
{
public:
  IPluginProxy() : m_ParentWidget(nullptr) {}

  /**
   * @brief List the plugins managed by this proxy in the given
   *   folder.
   *
   * @param pluginPath Path containing the plugins.
   *
   * @return list of plugin identifiers that supported by this proxy.
   */
  virtual QStringList pluginList(const QDir& pluginPath) const = 0;

  /**
   * @brief Load the plugins corresponding to the given identifier.
   *
   * @param identifier Identifier of the proxied plugin to load.
   *
   * @return a list of QObject, one for each plugins in the given identifier.
   */
  virtual QList<QObject*> load(const QString& identifier) = 0;

  /**
   * @brief Unload the plugins corresponding to the given identifier.
   *
   * @param identifier Identifier of the proxied plugin to unload.
   */
  virtual void unload(const QString& identifier) = 0;

  /**
   * @brief Sets the widget that the tool should use as the parent whenever
   *   it creates a new modal dialog.
   *
   * @param widget The new parent widget.
   */
  void setParentWidget(QWidget* widget) { m_ParentWidget = widget; }

protected:
  QWidget* parentWidget() const { return m_ParentWidget; }

private:
  QWidget* m_ParentWidget;
};

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPluginProxy, "com.tannin.ModOrganizer.PluginProxy/1.0")

#endif  // IPLUGINPROXY_H
