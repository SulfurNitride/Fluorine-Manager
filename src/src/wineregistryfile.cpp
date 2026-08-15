/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "wineregistryfile.h"

#include <QByteArray>
#include <QRegularExpression>
#include <uibase/transactionalwritefile.h>

#include <algorithm>

namespace WineRegistryFile
{
namespace
{

struct Line
{
  QByteArray content;
  QByteArray ending;
};

Result fail(QString error, Status status = Status::Failure)
{
  return {status, std::move(error), false};
}

Result ok(bool changed = false)
{
  return {Status::Success, {}, changed};
}

QList<Line> splitLines(const QByteArray& bytes)
{
  QList<Line> result;
  qsizetype offset = 0;
  while (offset < bytes.size())
  {
    const qsizetype newline = bytes.indexOf('\n', offset);
    const qsizetype end = newline < 0 ? bytes.size() : newline;
    qsizetype contentEnd = end;
    if (contentEnd > offset && bytes[contentEnd - 1] == '\r')
    {
      --contentEnd;
    }
    result.append({bytes.mid(offset, contentEnd - offset),
                   bytes.mid(contentEnd, newline < 0 ? end - contentEnd
                                                     : newline + 1 - contentEnd)});
    if (newline < 0)
    {
      break;
    }
    offset = newline + 1;
  }
  return result;
}

QByteArray joinLines(const QList<Line>& lines)
{
  QByteArray bytes;
  for (const Line& line : lines)
  {
    bytes += line.content;
    bytes += line.ending;
  }
  return bytes;
}

QByteArray defaultEnding(const QList<Line>& lines)
{
  for (const Line& line : lines)
  {
    if (!line.ending.isEmpty())
    {
      return line.ending;
    }
  }
  return QByteArrayLiteral("\n");
}

QString textForParsing(const Line& line, int index)
{
  QByteArray bytes = line.content.trimmed();
  if (index == 0 && bytes.startsWith("\xEF\xBB\xBF"))
  {
    bytes.remove(0, 3);
    bytes = bytes.trimmed();
  }
  return QString::fromUtf8(bytes);
}

QString escapedKey(QString value)
{
  value.replace('\\', QStringLiteral("\\\\"));
  return value;
}

QString escapedQuoted(QString value)
{
  QString escaped;
  escaped.reserve(value.size());
  for (const QChar character : value)
  {
    switch (character.unicode())
    {
    case '\a':
      escaped += QStringLiteral("\\a");
      break;
    case '\b':
      escaped += QStringLiteral("\\b");
      break;
    case '\t':
      escaped += QStringLiteral("\\t");
      break;
    case '\n':
      escaped += QStringLiteral("\\n");
      break;
    case '\v':
      escaped += QStringLiteral("\\v");
      break;
    case '\f':
      escaped += QStringLiteral("\\f");
      break;
    case '\r':
      escaped += QStringLiteral("\\r");
      break;
    case 0x1b:
      escaped += QStringLiteral("\\e");
      break;
    case '\\':
      escaped += QStringLiteral("\\\\");
      break;
    case '"':
      escaped += QStringLiteral("\\\"");
      break;
    default:
      if (character.unicode() < 0x20)
      {
        escaped += QStringLiteral("\\x%1").arg(
            static_cast<uint>(character.unicode()), 4, 16, QLatin1Char('0'));
      }
      else
      {
        escaped += character;
      }
      break;
    }
  }
  return escaped;
}

int hexDigit(QChar character)
{
  const ushort value = character.unicode();
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

bool decodedQuoted(const QString& line, QString& value)
{
  if (!line.startsWith('"'))
    return false;
  qsizetype nameEnd = -1;
  bool nameEscape = false;
  for (qsizetype index = 1; index < line.size(); ++index)
  {
    if (nameEscape)
    {
      nameEscape = false;
    }
    else if (line[index] == '\\')
    {
      nameEscape = true;
    }
    else if (line[index] == '"')
    {
      nameEnd = index;
      break;
    }
  }
  if (nameEnd < 0)
    return false;
  const qsizetype equals = line.indexOf('=', nameEnd + 1);
  if (equals < 0)
  {
    return false;
  }
  const QString rhs = line.mid(equals + 1).trimmed();
  const qsizetype opening = rhs.indexOf('"');
  if (opening < 0)
  {
    return false;
  }

  QString decoded;
  for (qsizetype index = opening + 1; index < rhs.size(); ++index)
  {
    const QChar character = rhs[index];
    if (character == '"')
    {
      value = std::move(decoded);
      return true;
    }
    if (character != '\\')
    {
      decoded += character;
      continue;
    }

    if (++index >= rhs.size())
      return false;
    const QChar escape = rhs[index];
    if (escape == 'x')
    {
      ushort codePoint = 0;
      int digits = 0;
      while (index + 1 < rhs.size() && digits < 4)
      {
        const int digit = hexDigit(rhs[index + 1]);
        if (digit < 0)
          break;
        codePoint = static_cast<ushort>(codePoint * 16 + digit);
        ++index;
        ++digits;
      }
      if (digits == 0)
        return false;
      decoded += QChar(codePoint);
      continue;
    }
    if (escape >= '0' && escape <= '7')
    {
      ushort codePoint = escape.unicode() - '0';
      int digits = 1;
      while (index + 1 < rhs.size() && digits < 3 && rhs[index + 1] >= '0' &&
             rhs[index + 1] <= '7')
      {
        codePoint = static_cast<ushort>(codePoint * 8 +
                                        rhs[index + 1].unicode() - '0');
        ++index;
        ++digits;
      }
      decoded += QChar(codePoint);
      continue;
    }

    switch (escape.unicode())
    {
    case 'a':
      decoded += QChar('\a');
      break;
    case 'b':
      decoded += QChar('\b');
      break;
    case 't':
      decoded += QChar('\t');
      break;
    case 'n':
      decoded += QChar('\n');
      break;
    case 'v':
      decoded += QChar('\v');
      break;
    case 'f':
      decoded += QChar('\f');
      break;
    case 'r':
      decoded += QChar('\r');
      break;
    case 'e':
      decoded += QChar(0x1b);
      break;
    default:
      decoded += escape;
      break;
    }
  }
  return false;
}

bool isSectionLine(const QString& line)
{
  return line.startsWith('[');
}

bool matchesSection(const QString& line, const QString& section)
{
  const QString header = QStringLiteral("[") + escapedKey(section) + ']';
  if (!line.startsWith(header, Qt::CaseInsensitive))
  {
    return false;
  }
  const QString suffix = line.mid(header.size()).trimmed();
  if (suffix.isEmpty())
  {
    return true;
  }
  bool validTimestamp = false;
  suffix.toLongLong(&validTimestamp);
  return validTimestamp;
}

QString valuePrefix(const QString& name)
{
  return '"' + escapedQuoted(name) + QStringLiteral("\"=");
}

QList<int> matchingValues(const QList<Line>& lines, const QString& section,
                          const QString& name)
{
  QList<int> result;
  bool inSection = false;
  const QString prefix = valuePrefix(name);
  for (int index = 0; index < lines.size(); ++index)
  {
    const QString text = textForParsing(lines[index], index);
    if (isSectionLine(text))
    {
      inSection = matchesSection(text, section);
      continue;
    }
    if (inSection && text.startsWith(prefix, Qt::CaseInsensitive))
    {
      result.append(index);
    }
  }
  return result;
}

int firstSection(const QList<Line>& lines, const QString& section)
{
  for (int index = 0; index < lines.size(); ++index)
  {
    if (matchesSection(textForParsing(lines[index], index), section))
    {
      return index;
    }
  }
  return -1;
}

Result resolveQuery(const QList<Line>& lines, Query& query)
{
  query.present = false;
  query.value.clear();
  const QList<int> matches = matchingValues(lines, query.section, query.name);
  for (int index : matches)
  {
    QString decoded;
    if (!decodedQuoted(textForParsing(lines[index], index), decoded))
    {
      return fail(QStringLiteral("Malformed Wine registry string value [%1] %2.")
                      .arg(query.section, query.name));
    }
    if (query.present && query.value != decoded)
    {
      return fail(QStringLiteral("Conflicting duplicate Wine registry value [%1] %2.")
                      .arg(query.section, query.name));
    }
    query.present = true;
    query.value = std::move(decoded);
  }
  return ok();
}

void preserveFinalEnding(QList<Line>& lines, bool originallyEmpty, bool hadFinalEnding,
                         const QByteArray& ending)
{
  if (lines.isEmpty() || originallyEmpty)
  {
    return;
  }
  if (hadFinalEnding && lines.last().ending.isEmpty())
  {
    lines.last().ending = ending;
  }
  else if (!hadFinalEnding)
  {
    lines.last().ending.clear();
  }
}

void insertValue(QList<Line>& lines, const Update& update, const QByteArray& ending,
                 bool originallyEmpty, bool hadFinalEnding)
{
  const QByteArray newLine =
      ('"' + escapedQuoted(update.name) + QStringLiteral("\"=\"") +
       escapedQuoted(update.value) + '"')
          .toUtf8();
  const int section = firstSection(lines, update.section);
  if (section >= 0)
  {
    if (lines[section].ending.isEmpty())
    {
      lines[section].ending = ending;
    }
    const bool atEnd = section + 1 == lines.size();
    lines.insert(section + 1,
                 {newLine, atEnd && !hadFinalEnding ? QByteArray{} : ending});
    return;
  }

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
  lines.append({('[' + escapedKey(update.section) + ']').toUtf8(), ending});
  lines.append({newLine, originallyEmpty || hadFinalEnding ? ending : QByteArray{}});
}

Result readSnapshot(MOBase::TransactionalWriteFile& transaction, QByteArray& original,
                    bool& present)
{
  if (!transaction.readOriginal(original, present))
  {
    return fail(transaction.errorString());
  }
  return ok();
}

bool writableByPolicy(QFileDevice::Permissions permissions)
{
  return permissions.testAnyFlags(
      QFileDevice::WriteOwner | QFileDevice::WriteUser |
      QFileDevice::WriteGroup | QFileDevice::WriteOther);
}

} // namespace

Result readValues(const QString& path, QList<Query>& queries)
{
  MOBase::TransactionalWriteFile transaction(path);
  QByteArray original;
  bool present = false;
  Result result = readSnapshot(transaction, original, present);
  if (!result)
  {
    return result;
  }
  if (!present)
  {
    return fail(QStringLiteral("Wine registry file '%1' is missing.").arg(path));
  }
  const QList<Line> lines = splitLines(original);
  for (Query& query : queries)
  {
    result = resolveQuery(lines, query);
    if (!result)
    {
      return result;
    }
  }
  return ok();
}

Result updateValues(const QString& path, const QList<Update>& updates)
{
  if (updates.isEmpty())
  {
    return ok();
  }
  MOBase::TransactionalWriteFile transaction(path);
  QByteArray original;
  bool present = false;
  Result result = readSnapshot(transaction, original, present);
  if (!result)
  {
    return result;
  }
  if (!present)
  {
    return fail(QStringLiteral("Wine registry file '%1' is missing.").arg(path));
  }
  QFileDevice::Permissions originalPermissions;
  if (!transaction.readPermissions(originalPermissions))
  {
    return fail(transaction.errorString());
  }
  QList<Line> lines = splitLines(original);
  const bool originallyEmpty = original.isEmpty();
  const bool hadFinalEnding =
      !original.isEmpty() && (original.endsWith('\r') || original.endsWith('\n'));
  const QByteArray ending = defaultEnding(lines);

  for (const Update& update : updates)
  {
    Query current{update.section, update.name};
    result = resolveQuery(lines, current);
    if (!result)
    {
      return result;
    }
    if (update.compareExisting &&
        (current.present != update.expectedPresent ||
         (current.present && current.value != update.expectedValue)))
    {
      return fail(QStringLiteral("Wine registry value [%1] %2 changed while "
                                 "confirmation was pending.")
                      .arg(update.section, update.name),
                  Status::Conflict);
    }

    const QList<int> matches = matchingValues(lines, update.section, update.name);
    const QByteArray newLine =
        ('"' + escapedQuoted(update.name) + QStringLiteral("\"=\"") +
         escapedQuoted(update.value) + '"')
            .toUtf8();
    if (matches.isEmpty())
    {
      insertValue(lines, update, ending, originallyEmpty, hadFinalEnding);
    }
    else
    {
      for (int index : matches)
      {
        lines[index].content = newLine;
      }
    }
  }

  preserveFinalEnding(lines, originallyEmpty, hadFinalEnding, ending);
  const QByteArray replacement = joinLines(lines);
  if (replacement == original)
  {
    return ok();
  }
  if (!writableByPolicy(originalPermissions))
  {
    return fail(QStringLiteral("Wine registry file '%1' is read-only.").arg(path));
  }
  if (!transaction.replaceWith(replacement))
  {
    return fail(transaction.errorString());
  }
  return ok(true);
}

Result removeDriveMappings(const QString& path, QStringList& removed)
{
  removed.clear();
  MOBase::TransactionalWriteFile transaction(path);
  QByteArray original;
  bool present = false;
  Result result = readSnapshot(transaction, original, present);
  if (!result)
  {
    return result;
  }
  if (!present)
  {
    return ok();
  }
  QFileDevice::Permissions originalPermissions;
  if (!transaction.readPermissions(originalPermissions))
  {
    return fail(transaction.errorString());
  }
  QList<Line> lines = splitLines(original);
  const bool originallyEmpty = original.isEmpty();
  const bool hadFinalEnding =
      !original.isEmpty() && (original.endsWith('\r') || original.endsWith('\n'));
  const QByteArray ending = defaultEnding(lines);
  static const QRegularExpression driveExpression(
      QStringLiteral(R"(^"([A-Za-z]):"\s*=)"));
  bool inDrives = false;
  QList<int> retire;
  for (int index = 0; index < lines.size(); ++index)
  {
    const QString text = textForParsing(lines[index], index);
    if (isSectionLine(text))
    {
      inDrives = matchesSection(text, QStringLiteral("Software\\Wine\\Drives"));
      continue;
    }
    if (!inDrives)
    {
      continue;
    }
    const QRegularExpressionMatch match = driveExpression.match(text);
    if (!match.hasMatch())
    {
      continue;
    }
    const QChar drive = match.captured(1).at(0).toUpper();
    if (drive == 'C' || drive == 'Z')
    {
      continue;
    }
    const QString label = drive + QStringLiteral(":");
    if (!removed.contains(label))
    {
      removed.append(label);
    }
    retire.prepend(index);
  }
  if (retire.isEmpty())
  {
    return ok();
  }
  if (!writableByPolicy(originalPermissions))
  {
    removed.clear();
    return fail(QStringLiteral("Wine registry file '%1' is read-only.").arg(path));
  }
  for (int index : retire)
  {
    lines.removeAt(index);
  }
  preserveFinalEnding(lines, originallyEmpty, hadFinalEnding, ending);
  if (!transaction.replaceWith(joinLines(lines)))
  {
    removed.clear();
    return fail(transaction.errorString());
  }
  return ok(true);
}

} // namespace WineRegistryFile
