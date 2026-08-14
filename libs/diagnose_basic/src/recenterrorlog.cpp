#include "recenterrorlog.h"

#include <QBuffer>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace diagnose_basic
{
namespace
{

constexpr qint64 MaxBytes    = 4 * 1024 * 1024;
constexpr qint64 MaxLineSize = 16 * 1024;
constexpr int ContextLines   = 5;

bool isErrorLine(const QString& line)
{
  static const QRegularExpression current(
      QStringLiteral(R"(^\[[^\]\r\n]+ E\])"));
  static const QRegularExpression legacy(QStringLiteral(R"(^ERROR(?:\b|:))"));
  return current.match(line).hasMatch() || legacy.match(line).hasMatch();
}

QString cleanLine(QByteArray line)
{
  while (line.endsWith('\n') || line.endsWith('\r')) {
    line.chop(1);
  }
  return QString::fromUtf8(line);
}

QString readBoundedLine(QIODevice& device)
{
  QByteArray line = device.readLine(MaxLineSize);
  while (!line.endsWith('\n') && !device.atEnd()) {
    const QByteArray remainder = device.readLine(MaxLineSize);
    if (remainder.isEmpty() || remainder.endsWith('\n')) {
      break;
    }
  }
  return cleanLine(std::move(line));
}

}  // namespace

std::optional<QString> recentErrorContext(const QString& logPath)
{
  QFile file(logPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }

  const qint64 start = std::max<qint64>(0, file.size() - MaxBytes);
  if (!file.seek(start)) {
    return std::nullopt;
  }
  QByteArray tail = file.read(MaxBytes);
  QBuffer buffer(&tail);
  if (!buffer.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  if (start != 0) {
    readBoundedLine(buffer);  // discard the first potentially partial line
  }

  QStringList previous;
  QStringList candidate;
  int following = 0;

  while (!buffer.atEnd()) {
    const QString line    = readBoundedLine(buffer);
    const QString escaped = line.toHtmlEscaped();

    if (isErrorLine(line)) {
      candidate = previous;
      candidate.append(QStringLiteral("<b>%1</b>").arg(escaped));
      following = ContextLines;
    } else if (following > 0) {
      candidate.append(escaped);
      --following;
    }

    previous.append(escaped);
    while (previous.size() > ContextLines) {
      previous.removeFirst();
    }
  }

  if (candidate.isEmpty()) {
    return std::nullopt;
  }
  return candidate.join(QStringLiteral("<br>"));
}

}  // namespace diagnose_basic
