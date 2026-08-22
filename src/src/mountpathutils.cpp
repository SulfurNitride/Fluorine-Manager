#include "mountpathutils.h"

#include <QDir>
#include <QFileInfo>

namespace
{
QString decodeMountField(const QString& input)
{
  QString output;
  output.reserve(input.size());

  for (qsizetype i = 0; i < input.size();) {
    if (input[i] == QLatin1Char('\\') && i + 3 < input.size() &&
        input[i + 1].isDigit() && input[i + 2].isDigit() &&
        input[i + 3].isDigit()) {
      bool valid = false;
      const ushort value = input.mid(i + 1, 3).toUShort(&valid, 8);
      if (valid) {
        output.append(QChar(value));
        i += 4;
        continue;
      }
    }

    output.append(input[i]);
    ++i;
  }

  return output;
}
}

QString mountPathIdentity(const QString& path)
{
  const QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));

  // This is the most accurate result while the mount is healthy.
  const QString canonical = info.canonicalFilePath();
  if (!canonical.isEmpty()) {
    return QDir::cleanPath(canonical);
  }

  // canonicalFilePath() fails on a dead FUSE endpoint. Resolve its accessible
  // parent instead, then append the mount directory without touching it.
  const QString canonicalParent = QFileInfo(info.absolutePath()).canonicalFilePath();
  if (!canonicalParent.isEmpty()) {
    return QDir::cleanPath(QDir(canonicalParent).filePath(info.fileName()));
  }

  return QDir::cleanPath(info.absoluteFilePath());
}

bool mountPathsEquivalent(const QString& left, const QString& right)
{
  const QString cleanLeft  = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
  const QString cleanRight = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
  return cleanLeft == cleanRight ||
         mountPathIdentity(cleanLeft) == mountPathIdentity(cleanRight);
}

bool mountTableContainsPath(const QString& mountTable, const QString& path)
{
  const auto lines = mountTable.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString& line : lines) {
    const auto fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (fields.size() >= 2 &&
        mountPathsEquivalent(decodeMountField(fields[1]), path)) {
      return true;
    }
  }
  return false;
}
