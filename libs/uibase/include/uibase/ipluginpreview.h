/*
Mod Organizer shared UI functionality

Copyright (C) 2014 Sebastian Herbord. All rights reserved.

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

#ifndef IPLUGINPREVIEW_H
#define IPLUGINPREVIEW_H

#include "iplugin.h"

namespace MOBase
{

class IPluginPreview : public QObject, public IPlugin
{
  Q_INTERFACES(IPlugin)
public:
  /**
   * @return returns a set of file extensions that may be supported
   */
  virtual std::set<QString> supportedExtensions() const = 0;

  /**
   * @return return whether or not the preview supports raw data previews (likely
   * sourced from an archive)
   */
  virtual bool supportsArchives() const { return false; }

  /**
   * @brief generate a preview widget for the specified file
   * @param fileName name of the file to preview
   * @param maxSize maximum size of the generated widget
   * @return a widget showing the file
   */
  virtual QWidget* genFilePreview(const QString& fileName,
                                  const QSize& maxSize) const = 0;

  /**
   * @brief generate a preview widget from an archive file loaded into memory
   * @param fileData data of file to preview
   * @param fileName path of file in virtual filesystem
   * @param maxSize maxiumum size of the generated widget
   */
  virtual QWidget* genDataPreview(const QByteArray& fileData, const QString& fileName,
                                  const QSize& maxSize) const
  {
    return nullptr;
  }

private:
};

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPluginPreview, "com.tannin.ModOrganizer.PluginPreview/1.0")

#endif  // IPLUGINPREVIEW_H
