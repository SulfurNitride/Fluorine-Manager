#include "nxmrequest.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <limits>

namespace
{
bool containsControlCharacter(const QString& value)
{
  for (const QChar character : value) {
    if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
      return true;
    }
  }
  return false;
}

bool isRepositoryLink(const QString& message)
{
  static const QRegularExpression pattern(
      QStringLiteral(
          R"(^(?:nxm|modl)://[a-z0-9]+/mods/([0-9]+)/files/([0-9]+)(?:\?.*)?$)"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = pattern.match(message);
  if (!match.hasMatch()) {
    return false;
  }

  bool modOk  = false;
  bool fileOk = false;
  const qulonglong modId  = match.captured(1).toULongLong(&modOk);
  const qulonglong fileId = match.captured(2).toULongLong(&fileOk);
  return modOk && fileOk && modId > 0 && fileId > 0 &&
         modId <= static_cast<qulonglong>(std::numeric_limits<int>::max()) &&
         fileId <= static_cast<qulonglong>(std::numeric_limits<int>::max());
}
}  // namespace

std::optional<NxmRequest> NxmRequest::parse(const QString& message)
{
  if (message.isEmpty() || containsControlCharacter(message)) {
    return std::nullopt;
  }

  const QUrl parsed(message);
  const QString scheme = parsed.scheme();
  if (!parsed.isValid() ||
      (scheme.compare(QStringLiteral("nxm"), Qt::CaseInsensitive) != 0 &&
       scheme.compare(QStringLiteral("modl"), Qt::CaseInsensitive) != 0)) {
    return std::nullopt;
  }

  const QString host = parsed.host().trimmed();
  if (host.isEmpty()) {
    return std::nullopt;
  }

  if (parsed.fragment().isEmpty() && isRepositoryLink(message)) {
    return NxmRequest{Kind::RepositoryFile, message};
  }

  if (scheme.compare(QStringLiteral("modl"), Qt::CaseInsensitive) == 0) {
    if ((!parsed.path().isEmpty() && parsed.path() != QStringLiteral("/")) ||
        !parsed.userName().isEmpty() || !parsed.password().isEmpty() ||
        parsed.port(-1) != -1 || !parsed.fragment().isEmpty()) {
      return std::nullopt;
    }

    const QString directUrl =
        QUrlQuery(parsed).queryItemValue(QStringLiteral("url"),
                                         QUrl::FullyDecoded);
    const QUrl target(directUrl);
    const QString targetScheme = target.scheme();
    const bool http =
        targetScheme.compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0;
    const bool https = targetScheme.compare(QStringLiteral("https"),
                                            Qt::CaseInsensitive) == 0;
    if (!directUrl.isEmpty() && !containsControlCharacter(directUrl) &&
        target.isValid() && !target.isRelative() && (http || https) &&
        !target.host().isEmpty() && target.userName().isEmpty() &&
        target.password().isEmpty()) {
      return NxmRequest{Kind::DirectDownload, directUrl};
    }
  }

  return std::nullopt;
}
