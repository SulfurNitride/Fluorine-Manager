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

#include <QApplication>
#include <QByteArray>
#include <QFileInfo>
#include <QList>
#include <QMessageBox>
#include <QString>
#include <uibase/log.h>
#include <uibase/registry.h>
#include <uibase/report.h>
#include <uibase/transactionalwritefile.h>

#include <optional>

namespace MOBase
{

namespace
{
enum class MutationStatus
{
  Success,
  ReadOnly,
  Failure,
};

enum class PermissionPolicy
{
  RespectReadOnly,
  PreserveReadOnly,
  MakeWritable,
};

struct MutationResult
{
  MutationStatus status{MutationStatus::Failure};
  QString error;
};

struct IniLine
{
  QByteArray content;
  QByteArray ending;
};

QList<IniLine> splitLines(QByteArrayView bytes)
{
  QList<IniLine> result;
  qsizetype start = 0;
  for (qsizetype index = 0; index < bytes.size(); ++index)
  {
    if (bytes[index] != '\r' && bytes[index] != '\n')
    {
      continue;
    }
    const qsizetype endingStart = index;
    if (bytes[index] == '\r' && index + 1 < bytes.size() && bytes[index + 1] == '\n')
    {
      ++index;
    }
    result.append({QByteArray(bytes.sliced(start, endingStart - start)),
                   QByteArray(bytes.sliced(endingStart, index - endingStart + 1))});
    start = index + 1;
  }
  if (start < bytes.size())
  {
    result.append({QByteArray(bytes.sliced(start)), {}});
  }
  return result;
}

QByteArray joinLines(const QList<IniLine>& lines)
{
  QByteArray result;
  for (const IniLine& line : lines)
  {
    result += line.content;
    result += line.ending;
  }
  return result;
}

QByteArray defaultEnding(const QList<IniLine>& lines)
{
  for (const IniLine& line : lines)
  {
    if (!line.ending.isEmpty())
    {
      return line.ending;
    }
  }
  return QByteArrayLiteral("\n");
}

QByteArray normalizedForMatch(const IniLine& line, int index)
{
  QByteArray value = line.content.trimmed();
  if (index == 0 && value.startsWith("\xEF\xBB\xBF"))
  {
    value.remove(0, 3);
    value = value.trimmed();
  }
  return value;
}

bool containsSyntaxControl(const QString& value)
{
  return value.contains('\0') || value.contains('\r') || value.contains('\n');
}

bool hasWriteBits(QFileDevice::Permissions permissions)
{
  return permissions.testAnyFlags(
      QFileDevice::WriteOwner | QFileDevice::WriteUser |
      QFileDevice::WriteGroup | QFileDevice::WriteOther);
}

MutationResult mutateIni(const QString& section, const QString& key,
                         const std::optional<QString>& value,
                         const QString& fileName,
                         PermissionPolicy permissionPolicy)
{
  if (section.trimmed().isEmpty() || key.trimmed().isEmpty() ||
      section != section.trimmed() || key != key.trimmed() ||
      section.contains('[') || section.contains(']') || key.contains('=') ||
      containsSyntaxControl(section) || containsSyntaxControl(key) ||
      (value.has_value() && containsSyntaxControl(*value)))
  {
    return {MutationStatus::Failure,
            QObject::tr("Refusing invalid INI section, key, or value.")};
  }

  TransactionalWriteFile transaction(fileName);
  QByteArray original;
  bool present = false;
  if (!transaction.readOriginal(original, present))
  {
    return {MutationStatus::Failure, transaction.errorString()};
  }
  QFileDevice::Permissions originalPermissions;
  if (!transaction.readPermissions(originalPermissions))
  {
    return {MutationStatus::Failure, transaction.errorString()};
  }
  if (!present && !value.has_value())
  {
    return {MutationStatus::Success, {}};
  }

  QList<IniLine> lines = splitLines(original);
  const bool originallyEmpty = original.isEmpty();
  const bool hadFinalEnding =
      !original.isEmpty() && (original.endsWith('\r') || original.endsWith('\n'));
  const QByteArray ending = defaultEnding(lines);
  const QByteArray sectionHeader =
      QByteArrayLiteral("[") + section.toUtf8() + QByteArrayLiteral("]");
  const QByteArray encodedKey = key.toUtf8();

  int sectionStart = -1;
  int sectionEnd = lines.size();
  int keyLine = -1;
  for (int index = 0; index < lines.size(); ++index)
  {
    const QByteArray trimmed = normalizedForMatch(lines[index], index);
    if (QString::fromUtf8(trimmed).compare(QString::fromUtf8(sectionHeader),
                                           Qt::CaseInsensitive) != 0)
    {
      continue;
    }
    sectionStart = index;
    for (int next = index + 1; next < lines.size(); ++next)
    {
      const QByteArray candidate = normalizedForMatch(lines[next], next);
      if (candidate.startsWith('[') && candidate.endsWith(']'))
      {
        sectionEnd = next;
        break;
      }
    }
    break;
  }

  if (sectionStart >= 0)
  {
    for (int index = sectionStart + 1; index < sectionEnd; ++index)
    {
      const QByteArray trimmed = normalizedForMatch(lines[index], index);
      if (trimmed.isEmpty() || trimmed.startsWith(';') || trimmed.startsWith('#'))
      {
        continue;
      }
      const qsizetype equals = trimmed.indexOf('=');
      if (equals <= 0)
      {
        continue;
      }
      if (QString::fromUtf8(trimmed.left(equals).trimmed())
              .compare(key, Qt::CaseInsensitive) == 0)
      {
        keyLine = index;
        break;
      }
    }
  }

  if (!value.has_value())
  {
    if (keyLine < 0)
    {
      return {MutationStatus::Success, {}};
    }
    lines.removeAt(keyLine);
  }
  else if (keyLine >= 0)
  {
    const qsizetype equals = lines[keyLine].content.indexOf('=');
    if (equals < 0)
    {
      return {MutationStatus::Failure,
              QObject::tr("INI key changed while it was parsed.")};
    }
    lines[keyLine].content = lines[keyLine].content.left(equals + 1) + value->toUtf8();
  }
  else if (sectionStart >= 0)
  {
    if (lines[sectionStart].ending.isEmpty())
    {
      lines[sectionStart].ending = ending;
    }
    const bool insertsAtEnd = sectionStart + 1 == lines.size();
    lines.insert(sectionStart + 1,
                 {encodedKey + '=' + value->toUtf8(),
                  insertsAtEnd && !hadFinalEnding ? QByteArray{} : ending});
  }
  else
  {
    if (!lines.isEmpty())
    {
      if (lines.last().ending.isEmpty())
      {
        lines.last().ending = ending;
      }
      if (!lines.last().content.trimmed().isEmpty())
      {
        lines.append({{}, ending});
      }
    }
    lines.append({sectionHeader, ending});
    lines.append({encodedKey + '=' + value->toUtf8(),
                  originallyEmpty || hadFinalEnding ? ending : QByteArray{}});
  }

  if (!lines.isEmpty() && !originallyEmpty)
  {
    if (hadFinalEnding && lines.last().ending.isEmpty())
    {
      lines.last().ending = ending;
    }
    else if (!hadFinalEnding)
    {
      lines.last().ending.clear();
    }
  }

  const QByteArray replacement = joinLines(lines);
  if (replacement == original)
  {
    return {MutationStatus::Success, {}};
  }
  if (present && !hasWriteBits(originalPermissions) &&
      permissionPolicy == PermissionPolicy::RespectReadOnly)
  {
    return {MutationStatus::ReadOnly, {}};
  }
  if (permissionPolicy == PermissionPolicy::MakeWritable)
  {
    const QFileDevice::Permissions permissions =
        originalPermissions | QFileDevice::WriteUser | QFileDevice::WriteOwner;
    if (!transaction.setPermissions(permissions))
    {
      return {MutationStatus::Failure, transaction.errorString()};
    }
  }
  else if (permissionPolicy == PermissionPolicy::PreserveReadOnly && present &&
           !transaction.setPermissions(originalPermissions))
  {
    return {MutationStatus::Failure, transaction.errorString()};
  }
  if (!transaction.replaceWith(replacement))
  {
    return {MutationStatus::Failure, transaction.errorString()};
  }
  return {MutationStatus::Success, {}};
}

MutationResult writeIniValueDirect(const QString& section, const QString& key,
                                   const QString& value, const QString& fileName,
                                   PermissionPolicy permissionPolicy)
{
  return mutateIni(section, key, value, fileName, permissionPolicy);
}

} // namespace

bool WriteRegistryValue(const QString& appName, const QString& keyName,
                        const QString& value, const QString& fileName)
{
  MutationResult mutation = writeIniValueDirect(
      appName, keyName, value, fileName, PermissionPolicy::RespectReadOnly);
  if (mutation.status == MutationStatus::Success)
  {
    return true;
  }
  if (mutation.status == MutationStatus::Failure)
  {
    log::error("Could not update INI '{}': {}", fileName, mutation.error);
    return false;
  }

  // Write failed, check if the file is read-only
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

  if (result & (QMessageBox::Yes | QMessageBox::Ignore))
  {
    mutation = writeIniValueDirect(
        appName, keyName, value, fileName,
        result == QMessageBox::Yes ? PermissionPolicy::MakeWritable
                                   : PermissionPolicy::PreserveReadOnly);

    if (mutation.status != MutationStatus::Success && !mutation.error.isEmpty())
    {
      log::error("Could not update INI '{}': {}", fileName, mutation.error);
    }
    return mutation.status == MutationStatus::Success;
  }

  return false;
}

bool RemoveRegistryValue(const QString& section, const QString& key,
                         const QString& fileName)
{
  const MutationResult mutation =
      mutateIni(section, key, std::nullopt, fileName,
                PermissionPolicy::RespectReadOnly);
  if (mutation.status != MutationStatus::Success && !mutation.error.isEmpty())
  {
    log::error("Could not remove INI value from '{}': {}", fileName, mutation.error);
  }
  return mutation.status == MutationStatus::Success;
}

#ifdef _WIN32
bool WriteRegistryValue(const wchar_t* appName, const wchar_t* keyName,
                        const wchar_t* value, const wchar_t* fileName)
{
  return WriteRegistryValue(
      QString::fromWCharArray(appName), QString::fromWCharArray(keyName),
      QString::fromWCharArray(value), QString::fromWCharArray(fileName));
}
#endif

} // namespace MOBase
