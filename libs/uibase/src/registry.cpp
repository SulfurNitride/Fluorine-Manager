/*
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

#include <uibase/registry.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <QApplication>
#include <QFileInfo>
#include <QList>
#include <QMessageBox>
#include <QSettings>
#include <QString>

namespace MOBase
{

bool WriteRegistryValue(const QString& appName, const QString& keyName,
                        const QString& value, const QString& fileName)
{
  // Use QSettings to write INI files cross-platform
  QSettings settings(fileName, QSettings::IniFormat);
  settings.beginGroup(appName);
  settings.setValue(keyName, value);
  settings.endGroup();
  settings.sync();

  if (settings.status() != QSettings::NoError) {
    QFileInfo fileInfo(fileName);

    QMessageBox::StandardButton result =
        MOBase::TaskDialog(qApp->activeModalWidget(),
                           QObject::tr("INI file is read-only"))
            .main(QObject::tr("INI file is read-only"))
            .content(QObject::tr("Mod Organizer is attempting to write to \"%1\" "
                                 "which is currently set to read-only.")
                         .arg(fileInfo.fileName()))
            .icon(QMessageBox::Warning)
            .button({QObject::tr("Clear the read-only flag"), QMessageBox::Yes})
            .button({QObject::tr("Allow the write once"),
                     QObject::tr("The file will be set to read-only again."),
                     QMessageBox::Ignore})
            .button({QObject::tr("Skip this file"), QMessageBox::No})
            .remember("clearReadOnly", fileInfo.fileName())
            .exec();

    if (result & (QMessageBox::Yes | QMessageBox::Ignore)) {
      // Make the file writable
      QFile file(fileName);
      file.setPermissions(file.permissions() | QFile::WriteUser | QFile::WriteOwner);

      // Try writing again
      QSettings retrySettings(fileName, QSettings::IniFormat);
      retrySettings.beginGroup(appName);
      retrySettings.setValue(keyName, value);
      retrySettings.endGroup();
      retrySettings.sync();

      if (result == QMessageBox::Ignore) {
        // Set back to read-only
        file.setPermissions(file.permissions() & ~(QFile::WriteUser | QFile::WriteOwner));
      }

      return retrySettings.status() == QSettings::NoError;
    }

    return false;
  }

  return true;
}

#ifdef _WIN32
bool WriteRegistryValue(const wchar_t* appName, const wchar_t* keyName,
                        const wchar_t* value, const wchar_t* fileName)
{
  return WriteRegistryValue(
    QString::fromWCharArray(appName),
    QString::fromWCharArray(keyName),
    QString::fromWCharArray(value),
    QString::fromWCharArray(fileName));
}
#endif

}  // namespace MOBase
