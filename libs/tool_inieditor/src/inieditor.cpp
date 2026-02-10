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

#include "inieditor.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QUrl>
#include <QtPlugin>

#include <uibase/imoinfo.h>
#include <uibase/iplugingame.h>
#include <uibase/textviewer.h>
#include <uibase/utility.h>

using namespace MOBase;

IniEditor::IniEditor() : m_MOInfo(nullptr) {}

bool IniEditor::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;
  return true;
}

QString IniEditor::name() const
{
  return "INI Editor";
}

QString IniEditor::localizedName() const
{
  return tr("INI Editor");
}

QString IniEditor::author() const
{
  return "Tannin";
}

QString IniEditor::description() const
{
  return tr("Very basic editor for INI files");
}

VersionInfo IniEditor::version() const
{
  return VersionInfo(1, 1, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> IniEditor::settings() const
{
  QList<PluginSetting> result;
  result.push_back(PluginSetting("external", "Use an external editor to open the files",
                                 QVariant(false)));
  result.push_back(PluginSetting(
      "associated",
      "When using an external editor, use the application associated with \"INI\" "
      "files. "
      "If false, uses the \"edit\" command which usually invokes regular notepad.",
      QVariant(true)));
  return result;
}

QString IniEditor::displayName() const
{
  return tr("INI Editor");
}

QString IniEditor::tooltip() const
{
  return tr("Edit the INI files of the current profile");
}

QIcon IniEditor::icon() const
{
  return QIcon(":/inieditor/icon_document");
}

void IniEditor::display() const
{
  if (m_MOInfo == nullptr) {
    throw MyException(tr("plugin not initialized"));
  }

  QStringList iniFiles = m_MOInfo->managedGame()->iniFiles();
  if (m_MOInfo->pluginSetting(name(), "external").toBool()) {
    for (QString const& file : iniFiles) {
      QString fileName = m_MOInfo->profile()->absoluteIniFilePath(file);
#ifdef _WIN32
      ::ShellExecuteW(nullptr,
                      m_MOInfo->pluginSetting(name(), "associated").toBool() ? L"open"
                                                                             : L"edit",
                      ToWString(fileName).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
      QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
#endif
    }
  } else {
    TextViewer* viewer = new TextViewer("INI Files", parentWidget());
    if (m_MOInfo->profile()->localSettingsEnabled()) {
      viewer->setDescription(tr(
          " Editing profile-specific INIs. Profile-specific game INI files are "
          "Enabled.\n \n You can change the INI Editor settings (in the main Mod "
          "Organizer settings under the plugins tab) to use an external text editor,\n "
          "either notepad or your windows associated program to .INI files."));
    } else {
      viewer->setDescription(tr(
          " Editing global INIs (in 'myGames' folder). Profile-specific game INI files "
          "are Disabled.\n \n You can change the INI Editor settings (in the main Mod "
          "Organizer settings under the plugins tab) to use an external text editor,\n "
          "either notepad or your windows associated program to .INI files."));
    }
    for (QString const& file : iniFiles) {
      QString fileName = m_MOInfo->profile()->absoluteIniFilePath(file);
      QFileInfo fileInfo(fileName);
      if (fileInfo.exists()) {
        if (fileInfo.size() < 1024 * 1024) {
          viewer->addFile(fileName, true);
        } else {
          QMessageBox::warning(parentWidget(), tr("File too big"),
                               tr("Sorry, the file %1 is too large"
                                  " to be handled by the INI Editor")
                                   .arg(fileName));
        }
      }
    }
    viewer->show();
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(iniEditor, IniEditor)
#endif
