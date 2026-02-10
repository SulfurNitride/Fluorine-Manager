/*
Mod Organizer shared UI functionality

Copyright (C) 2013 Sebastian Herbord. All rights reserved.

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

#ifndef IPLUGINMODPAGE_H
#define IPLUGINMODPAGE_H

#include "iplugin.h"
#include "modrepositoryfileinfo.h"
#include <QIcon>

namespace MOBase
{

class IPluginModPage : public QObject, public IPlugin
{
  Q_INTERFACES(IPlugin)
public:
  /**
   * @return name of the Page as displayed in the ui
   */
  virtual QString displayName() const = 0;

  /**
   * @return icon to be displayed with the page
   */
  virtual QIcon icon() const = 0;

  /**
   * @return url to open when the user wants to visit this mod page
   */
  virtual QUrl pageURL() const = 0;

  /**
   * @return true if the page should be opened in the integrated browser
   * @note unless the page provides a special means of starting downloads (like the
   * nxm:// url schema on nexus) it will not be possible to handle downloads unless the
   * integrated browser is used!
   */
  virtual bool useIntegratedBrowser() const = 0;

  /**
   * @brief test if the plugin handles a download
   * @param pageURL url of the page that contained the donwload link
   * @param downloadURL the actual download link
   * @param fileInfo if the plugin can derive information from the urls it can store
   * them here and they will be passed along with the download and be made available on
   * installation
   * @return true if this plugin wants to handle the specified download
   */
  virtual bool handlesDownload(const QUrl& pageURL, const QUrl& downloadURL,
                               ModRepositoryFileInfo& fileInfo) const = 0;

  /**
   * @brief sets the widget that the tool should use as the parent whenever
   *        it creates a new modal dialog
   * @param widget the new parent widget
   */
  virtual void setParentWidget(QWidget* widget) { m_ParentWidget = widget; }

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

Q_DECLARE_INTERFACE(MOBase::IPluginModPage, "com.tannin.ModOrganizer.PluginModPage/1.0")

#endif  // IPLUGINMODPAGE_H
