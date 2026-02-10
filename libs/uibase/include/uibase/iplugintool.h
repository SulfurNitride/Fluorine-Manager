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

#ifndef IPLUGINTOOL_H
#define IPLUGINTOOL_H

#include "iplugin.h"
#include <QIcon>

namespace MOBase
{

class IPluginTool : public QObject, public virtual IPlugin
{
  Q_INTERFACES(IPlugin)
public:
  IPluginTool() : m_ParentWidget(nullptr) {}

  /**
   * @return name of the tool as displayed in the ui
   */
  virtual QString displayName() const = 0;

  /**
   * @brief For IPluginTool, this returns displayName().
   *
   */
  virtual QString localizedName() const { return displayName(); }

  /**
   * @return tooltip string
   */

  virtual QString tooltip() const = 0;
  /**
   * @return icon to be displayed with the tool
   */
  virtual QIcon icon() const = 0;

  /**
   * @brief sets the widget that the tool should use as the parent whenever
   *        it creates a new modal dialog
   * @param widget the new parent widget
   */
  virtual void setParentWidget(QWidget* widget) { m_ParentWidget = widget; }

public Q_SLOTS:

  /**
   * @brief called when the user clicks to start the tool.
   * @note This must not throw an exception!
   */
  virtual void display() const = 0;

protected:
  /**
   * @brief getter for the parent widget
   * @return parent widget
   */
  QWidget* parentWidget() const { return m_ParentWidget; }

private:
  QWidget* m_ParentWidget;
};

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPluginTool, "com.tannin.ModOrganizer.PluginTool/1.0")

#endif  // IPLUGINTOOL_H
